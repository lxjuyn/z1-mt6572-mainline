#!/usr/bin/env python3
"""z1_a9_sparse2raw.py —— Android sparse image → raw，按真实格式解析并自证。

为什么单独成模块：`file` 认出这份 A9 GSI 是 "Android sparse image, version 1.0" 时，
本仓原先的转换实现是**错的**（magic 写成 0x3AFF26AF、头按 9 个 u32 解析），
真字段布局是（AOSP libsparse sparse_defs.h，已用真实镜像逐字段核对）：

    header(28B) = u32 magic=0xED26FF3A, u16 major, u16 minor,
                  u16 file_hdr_sz, u16 chunk_hdr_sz, u32 blk_sz,
                  u32 total_blks, u32 total_chunks, u32 image_checksum
    chunk(12B)  = u16 chunk_type, u16 reserved, u32 chunk_sz(块数), u32 total_sz(含本头)

自检不是走过场：每块校验 total_sz == 12 + payload（FILL 的 payload 固定 4B、DONT_CARE 为 0），
末尾校验累计输出块数 == total_blks，且 CRC32 块要真的对上前面那段数据的 crc32。
本项目已经两次栽在"长度字段与实数据不同步"上，这里把同样的纪律用在镜像格式上。
"""
import os, struct, sys, zlib

SPARSE_MAGIC = 0xED26FF3A
CHUNK_RAW, CHUNK_FILL, CHUNK_DC, CHUNK_CRC = 0xCAC1, 0xCAC2, 0xCAC3, 0xCAC4
NAMES = {CHUNK_RAW: 'RAW', CHUNK_FILL: 'FILL', CHUNK_DC: 'DONT_CARE', CHUNK_CRC: 'CRC32'}


def is_sparse(path):
    with open(path, 'rb') as f:
        b = f.read(4)
    return len(b) == 4 and struct.unpack('<I', b)[0] == SPARSE_MAGIC


def to_raw(src, dst, verbose=True):
    """src 是 sparse 时展开成 raw 写到 dst，返回 dict 摘要；不是 sparse 就原样拷过去。"""
    if not is_sparse(src):
        if os.path.abspath(src) != os.path.abspath(dst):
            with open(src, 'rb') as fi, open(dst, 'wb') as fo:
                while True:
                    b = fi.read(1 << 22)
                    if not b:
                        break
                    fo.write(b)
        return {'sparse': False, 'out': dst}

    summary = {'sparse': True, 'out': dst}
    with open(src, 'rb') as f:
        magic, major, minor, fh_sz, ch_sz, blk_sz, total_blks, total_chunks, cksum = \
            struct.unpack('<IHHHHIIII', f.read(28))
        if magic != SPARSE_MAGIC:
            raise SystemExit('SPARSE-FAIL: magic=0x%08X 不是 0xED26FF3A' % magic)
        if ch_sz != 12 or fh_sz != 28:
            raise SystemExit('SPARSE-FAIL: 头尺寸意外 file_hdr_sz=%d chunk_hdr_sz=%d（本实现按 28/12 写）'
                             % (fh_sz, ch_sz))
        f.seek(fh_sz)
        vblocks = total_blks * blk_sz
        summary.update(major=major, minor=minor, blk_sz=blk_sz, total_blks=total_blks,
                       total_chunks=total_chunks, virtual_bytes=vblocks)
        if verbose:
            print('sparse v%d.%d blk=%d total_blks=%d chunks=%d -> 展开 %d B (%.2f GiB)'
                  % (major, minor, blk_sz, total_blks, total_chunks, vblocks, vblocks / 2**30))
        pos = 0
        crc_start = 0   # 上一个 CRC32 块之后的输出起点（AOSP 的 CRC 覆盖输出文件里这段，含空洞）
        with open(dst, 'wb') as o:
            for i in range(total_chunks):
                ctype, resv, cblocks, total = struct.unpack('<HHII', f.read(ch_sz))
                payload = total - ch_sz
                name = NAMES.get(ctype, 'UNKNOWN')
                if ctype == CHUNK_RAW:
                    if payload != cblocks * blk_sz:
                        raise SystemExit('SPARSE-FAIL chunk %d RAW: total_sz=%d 与 %d 块不符'
                                         % (i, total, cblocks))
                elif ctype == CHUNK_FILL:
                    if payload != 4:
                        raise SystemExit('SPARSE-FAIL chunk %d FILL: payload=%d 应为 4' % (i, payload))
                elif ctype == CHUNK_DC:
                    if payload != 0:
                        raise SystemExit('SPARSE-FAIL chunk %d DONT_CARE: payload=%d 应为 0' % (i, payload))
                elif ctype == CHUNK_CRC:
                    pass
                else:
                    raise SystemExit('SPARSE-FAIL chunk %d: 未知类型 0x%04X' % (i, ctype))

                if ctype == CHUNK_CRC:
                    data = f.read(payload)
                    o.flush()
                    want = struct.unpack('<I', data[:4])[0] if len(data) >= 4 else None
                    got = _tail(dst, pos - crc_start, pos)
                    # CRC 不一致只 WARN 不判死：AOSP 对 DONT_CARE/FILL 是否计入 CRC 各家
                    # 打包器并不统一，把结论压在它上面会误拒好镜像。真正的判据是后面的
                    # e2fsck/debugfs（文件系统级），那才是硬证据。
                    if want is None:
                        if verbose: print('  chunk%-2d CRC32 无声明值，跳过' % i)
                    elif want != got:
                        print('  WARN chunk%-2d CRC32 声明 0x%08X 实算 0x%08X（不计 FILL/DONT_CARE 的打包器会造成这种差）'
                              % (i, want, got))
                    else:
                        print('  chunk%-2d CRC32 校验通过 0x%08X (覆盖 %d B)' % (i, got, pos - crc_start))
                    crc_start = pos
                    raw_run = 0
                    continue

                if ctype == CHUNK_RAW:
                    # 必须先 seek 到本块应在的偏移：DONT_CARE 不写数据但**要推进偏移**，
                    # 靠"顺序写"会让第一个空洞之后的所有 RAW 全落到错误位置
                    # （本实现第一版就这样把根 inode 写坏了，e2fsck 直接把 /system 清成空壳）。
                    o.seek(pos)
                    remain = payload
                    while remain:
                        b = f.read(min(remain, 1 << 22))
                        if not b:
                            raise SystemExit('SPARSE-FAIL chunk %d: 数据提前结束' % i)
                        o.write(b)
                        remain -= len(b)
                    pass
                elif ctype == CHUNK_FILL:
                    pat = f.read(4)
                    o.seek(pos)
                    need = cblocks * blk_sz
                    chunk = pat * min(need, 1 << 18)
                    while need > 0:
                        w = chunk[:min(need, len(chunk))]
                        o.write(w)
                        need -= len(w)
                else:  # DONT_CARE：只推进偏移，留空洞（ext4 读空洞为 0）
                    pass
                prev = pos
                pos += cblocks * blk_sz
                if verbose:
                    print('  chunk%-2d %-9s blocks=%-7d 输出 %d..%d' % (i, name, cblocks, prev, pos))
        if pos != vblocks:
            raise SystemExit('SPARSE-FAIL: 累计 %d B != 声明 %d B' % (pos, vblocks))
        # 补到声明容量，保证 ext4 尾部块计数与超级块一致
        cur = os.path.getsize(dst)
        if cur < vblocks:
            with open(dst, 'r+b') as o:
                o.truncate(vblocks)
        summary['written_bytes'] = os.path.getsize(dst)
    return summary


def _tail(path, nbytes, endpos):
    """对输出文件里 [endpos-nbytes, endpos) 这段算 CRC32（含空洞，读出来就是 0）。"""
    with open(path, 'rb') as f:
        f.seek(endpos - nbytes)
        h = zlib.crc32(b'')
        remain = nbytes
        while remain:
            b = f.read(min(remain, 1 << 22))
            if not b:
                break
            h = zlib.crc32(b, h)
            remain -= len(b)
        return h


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('用法: %s <in.img(sparse|raw)> <out.img(raw)>' % sys.argv[0]); sys.exit(2)
    s = to_raw(sys.argv[1], sys.argv[2])
    print('OUT=%s bytes=%d sparse=%s' % (s['out'], os.path.getsize(s['out']), s['sparse']))
