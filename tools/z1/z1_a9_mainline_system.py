#!/usr/bin/env python3
"""z1_a9_mainline_system.py —— 把一份 A9(LOS16) system.img 就地改造成 mainline 内核可用的版本。

为什么这件事能在本机做、且不需要 AOSP 源码树：
  stock 线那一整套"绕开 3.4.67 缺能力"的代偿，是靠镜像里的 **/z1_stock_kernel 标记文件** +
  `ro.z1.stock_kernel` 属性来门控的（ServiceManagement 先走本地 factory、hwservicemanager 被
  disabled、configstore 用本地 passthrough 工厂……）。标记一删，属性读空，那些分支全部落回
  AOSP 默认路径 —— 而这正是 mainline 线需要的。所以**镜像级改造就能撤代偿**，不必重编。
  （stock 分支的 tools/z1_stock_system_profile.py 已经证明这条技术路线可行：用 debugfs 原地重写
   单个已分配数据块，保住 inode 与 SELinux xattr，不碰块设备。）

做的事（每一步都读回校验，失败即中止并保留备份）：
  1. 删 /z1_stock_kernel（或它在 /system/etc 下的同名变体）——撤代偿的总开关；
  2. 把被 `disabled` 掉的 hwservicemanager / vndservicemanager 服务重新启用（只删 disabled 行）；
  3. 落 mainline 版 ueventd.z1.rc / init.z1.rc / fstab.z1（本仓 android9/mainline/rootdir/）；
  4. 校验 build.prop / default.prop 里没有残留 ro.z1.stock_kernel=1；
  5. e2fsck 复检 + 尺寸不得超过 android 分区 943,718,400 B；
  6. 输出 ADAPT_REPORT.json（前后 sha256 逐条）供 z1_a9_flashpkg.sh 装配。

明确不保证的事（别当成功能已验证）：
  - 不改任何 .so/.jar；S1 系列打在源码里的补丁仍在二进制里，只是分支被标记关掉。
  - SELinux 仍要靠 dts 的 androidboot.selinux=permissive 首轮收 denial。
  - 没上板。所有校验都是离线可读回的那种，不等于 A9 一定能起。
"""
import argparse, hashlib, json, os, re, shutil, stat, subprocess, sys, tempfile
from pathlib import Path

SELF = Path(__file__).resolve().parent
WT = SELF.parent.parent                    # .../a9_wt
ROOT = WT.parent                           # 工作区根
ANDROID_PART_LIMIT = 943_718_400           # android(p4) = 0x38400000
SPARSE_MAGIC = 0x3AFF26AF

# 本仓 mainline 版 device 层文件（本机已用 devcheck 核过与内核一致）
MAINLINE_ROOTDIR = WT / 'android9' / 'mainline' / 'rootdir'


def sha(b):
    return hashlib.sha256(b if isinstance(b, (bytes, bytearray)) else Path(b).read_bytes()).hexdigest()


def dbg(image, text, write=False, timeout=60):
    r = subprocess.run(['debugfs'] + (['-w'] if write else []) + ['-R', text, str(image)],
                       capture_output=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError('debugfs "%s" 失败: %s' % (text, r.stderr.decode(errors='replace')[:200]))
    return r.stdout


def dbg_script(image, lines, write=True, timeout=120):
    """debugfs 多条命令走 -f 脚本。注意 debugfs 的 `write` 第二个参数是
    **当前目录下的新文件名**，不是镜像内绝对路径 —— 直接传绝对路径会写出
    一个名字里带斜杠的奇怪条目（本工具第一版就踩了这个）。"""
    with tempfile.NamedTemporaryFile('w', suffix='.dbg', delete=False) as fh:
        fh.write('\n'.join(lines) + '\n')
        cf = fh.name
    try:
        r = subprocess.run(['debugfs'] + (['-w'] if write else []) + ['-f', cf, str(image)],
                           capture_output=True, timeout=timeout)
        if r.returncode != 0:
            raise RuntimeError('debugfs 脚本失败: %s' % r.stderr.decode(errors='replace')[:300])
        return r.stdout
    finally:
        os.unlink(cf)


def image_write(image, hostfile, dest, mode='0644'):
    """把主机文件写到镜像内绝对路径 dest，必要时逐级建目录；写完读回校验。"""
    parent = os.path.dirname(dest) or '/'
    name = os.path.basename(dest)
    lines, cur = [], '/'
    for seg in [x for x in parent.split('/') if x]:
        cur = cur + seg
        lines.append('mkdir %s' % cur)        # 已存在时 debugfs 会报错，用 cd 兜住
    lines.append('cd %s' % parent)
    lines.append('write %s %s' % (hostfile, name))
    lines.append('set_inode_field /%s %s mode %s' % (dest.lstrip('/'), name, mode))
    dbg_script(image, lines)
    return dbg(image, 'cat %s' % dest)


def exists(image, path):
    """注意：debugfs 找不到文件时退出码仍是 0，错误只打到 stderr。
    只看 returncode 会把"不存在"判成"存在"（本工具第一版就这样假中止了一次）。"""
    r = subprocess.run(['debugfs', '-R', 'stat %s' % path, str(image)],
                       capture_output=True, timeout=30)
    blob = (r.stdout + r.stderr).decode(errors='replace')
    if r.returncode != 0:
        return False
    return not re.search(r'File not found|does not exist|while trying to resolve', blob, re.I)


NAME_RE = re.compile(r'(\d+)\s+\((\d+)\)\s+(\S+)')


def ls_dir(image, path):
    """返回 {name: (inode, size)}。debugfs 的 ls 没有 -R，只能逐目录走。"""
    try:
        out = dbg(image, 'ls -d %s' % path).decode(errors='replace')
    except Exception:
        return {}
    res = {}
    for m in NAME_RE.finditer(out):
        ino, size, name = int(m.group(1)), int(m.group(2)), m.group(3)
        if name in ('.', '..') or name.startswith('.'):
            continue
        res[name] = (ino, size)
    return res


def walk(image, max_dirs=4000, max_depth=8):
    """广度遍历，返回 {路径: (inode, size)}，并单独给出目录集合。
    必须真遍历：原厂 A4.4 镜像里 build.prop 在根而不在 /system/，靠猜路径会漏。"""
    found, dirs = {}, ['/']
    seen = set(['/'])
    depth = {'/': 0}
    n = 0
    while dirs and n < max_dirs:
        d = dirs.pop(0)
        n += 1
        for name, (ino, size) in ls_dir(image, d).items():
            full = ('' if d == '/' else d) + '/' + name
            found[full] = (ino, size)
            # size==12 且能列出内容的是目录；用一次 ls 探测（数量受 max_dirs 约束）
            if depth.get(d, 0) < max_depth and full not in seen:
                sub = ls_dir(image, full)
                if sub:
                    seen.add(full)
                    dirs.append(full)
                    depth[full] = depth.get(d, 0) + 1
    return found


def ls_all(image, timeout=180):
    return ''


CHUNK_RAW, CHUNK_FILL, CHUNK_DC, CHUNK_CRC = 0xCAC1, 0xCAC2, 0xCAC3, 0xCAC4


def to_raw(image, workdir):
    """LOS16 常交回 sparse image；本机没有 simg2img，所以内置一个只读转换器。"""
    import struct
    head = open(image, 'rb').read(4)
    if struct.unpack('<I', head)[0] != SPARSE_MAGIC:
        return Path(image), False
    raw = Path(workdir) / 'system_raw.img'
    with open(image, 'rb') as f, open(raw, 'wb') as o:
        (magic, major, minor, fh_sz, ch_sz, blk_sz, total_blks, total_chunks, _m) = \
            struct.unpack('<9I', f.read(36))
        f.seek(fh_sz)
        pos, chunks = 0, 0
        while chunks < total_chunks:
            hdr = f.read(ch_sz)
            if len(hdr) < ch_sz:
                break
            ctype, _rsv, csize, cblocks = struct.unpack('<4I', hdr[:16])
            payload = csize - ch_sz
            if ctype == CHUNK_RAW:
                data = f.read(payload)
                o.seek(pos); o.write(data); pos += len(data)
            elif ctype == CHUNK_FILL:
                data = f.read(payload)
                o.seek(pos); o.write(data * cblocks); pos += blk_sz * cblocks
            elif ctype == CHUNK_DC:
                f.read(payload)
                pos += blk_sz * cblocks
            elif ctype == CHUNK_CRC:
                f.read(payload)
            else:
                raise RuntimeError('未知 sparse chunk 类型 0x%X' % ctype)
            chunks += 1
        # sparse 里 RAW chunk 通常已按块对齐；补零到 total_blks*blk_sz 保证 ext4 完整
        want = total_blks * blk_sz
        if pos < want:
            o.seek(want - 1); o.write(b'\0')
    return raw, True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--system', required=True, help='A9 system.img（sparse 或 raw ext4）')
    ap.add_argument('--out', required=True, help='输出目录')
    ap.add_argument('--dry-run', action='store_true', help='只检查不改')
    ap.add_argument('--inspect-only', action='store_true', help='只做结构勘察（可用于任意 Android ext4）')
    a = ap.parse_args()

    src = Path(a.system)
    if not src.is_file():
        print('找不到 %s' % src); return 2
    outdir = Path(a.out)
    outdir.mkdir(parents=True, exist_ok=True)
    report = {'input': str(src), 'input_sha256': sha(src), 'actions': [], 'notes': []}

    work = tempfile.mkdtemp(prefix='a9sys_')
    try:
        # sparse 转换
        raw, converted = to_raw(src, work)
        report['sparse_converted'] = converted
        img = Path(outdir) / ('system_mainline.img' if not a.inspect_only else raw)
        if not a.inspect_only and raw != img:
            shutil.copyfile(raw, img)
        print('工作镜像: %s (%d B)%s' % (img, img.stat().st_size, '  [由 sparse 转换]' if converted else ''))

        # 必须是常规文件、4096 块、无 metadata_csum（否则原地改块会破坏 checksum）
        sb = subprocess.run(['dumpe2fs', '-h', str(img)], capture_output=True, text=True, timeout=60).stdout
        if 'metadata_csum' in sb:
            print('ABORT: 该 ext4 启用 metadata_csum，原地改块会破坏校验和；需改用 debugfs write 全量替换并等 fs_metadata checksum 重算(本机不可靠)')
            return 3
        bs = int(re.search(r'^Block size:\s+(\d+)$', sb, re.M).group(1))
        report['block_size'] = bs
        print('ext4 block size=%d  filesystem state=%s' % (
            bs, (re.search(r'Filesystem state:\s*(.+)', sb) or ['', '?'])[1].strip()))

        allfiles = walk(img)
        report['entry_count'] = len(allfiles)
        print('镜像内路径数=%d' % len(allfiles))
        base = os.path.basename

        if a.inspect_only:
            for probe in ('z1_stock_kernel', 'build.prop', 'ueventd.rc', 'init.z1.rc',
                          'hwservicemanager', 'vintf', 'manifest.xml'):
                hits = [k for k in allfiles if probe in k]
                report['notes'].append('勘察 %s: %s' % (probe, (', '.join(hits[:4]) + (' ...' if len(hits) > 4 else '')) if hits else '未出现'))
            print(json.dumps(report, ensure_ascii=False, indent=1))
            return 0

        # --- 1) 删 stock 标记 ---
        markers = [k for k in allfiles if k.endswith('/z1_stock_kernel')]
        report['markers_found'] = markers
        for cand in markers or ['/z1_stock_kernel']:
            if exists(img, cand):
                before = dbg(img, 'cat %s' % cand)
                if a.dry_run:
                    report['actions'].append({'op': 'rm', 'path': cand, 'dry_run': True,
                                              'before_sha256': sha(before)})
                else:
                    dbg(img, 'rm %s' % cand, write=True)
                    if exists(img, cand):
                        print('ABORT: 删除 %s 后仍存在' % cand); return 3
                    report['actions'].append({'op': 'rm', 'path': cand, 'before_sha256': sha(before),
                                              'verified': 'gone'})
                    print('已删除 %s（stock 代偿总开关）' % cand)
        if not any(x['op'] == 'rm' for x in report['actions']):
            report['notes'].append('没找到 /z1_stock_kernel 标记 —— 若这份 system 不是 stock 线产物，属正常')

        # --- 2) 重新启用被 disabled 的 hwservicemanager/vndservicemanager ---
        rc_paths = [k for k in allfiles if k.endswith('.rc') and ('hwservice' in k or 'vndservice' in k)]
        if not rc_paths:
            # 兜底：在 system/etc/init 目录里按名字找
            rc_paths = ['/system/etc/init/hwservicemanager.rc', '/system/vendor/etc/init/hwservicemanager.rc']
        for p in rc_paths:
            if not exists(img, p):
                continue
            data = dbg(img, 'cat %s' % p)
            text = data.decode(errors='replace')
            disabled = re.findall(r'(?m)^\s*disabled\s*$', text)
            if not disabled:
                report['notes'].append('%s 无 disabled 行' % p)
                continue
            new = re.sub(r'(?m)^\s*disabled\s*$\n', '', text).encode()
            entry = {'op': 'enable_services', 'path': p, 'removed_disabled_lines': len(disabled),
                     'before_sha256': sha(data), 'after_sha256': sha(new)}
            if a.dry_run:
                entry['dry_run'] = True
            else:
                blocks = dbg(img, 'blocks %s' % p).decode().split()
                if len(blocks) != 1 or not blocks[0].isdigit():
                    entry['skipped'] = '文件不是单块(用 debugfs write 全量替换需要设 xattr，本机不自动做)'
                    report['actions'].append(entry)
                    print('SKIP %s: 非单块，需人工用 write+ea_set' % p)
                    continue
                if len(new) > bs:
                    entry['skipped'] = '改写后 %d B 超过一块 %d B' % (len(new), bs)
                    report['actions'].append(entry); continue
                pad = new + b'\0' * (bs - len(new))
                with open(img, 'r+b') as fh:
                    fh.seek(int(blocks[0]) * bs)
                    fh.write(pad)
                dbg(img, 'set_inode_field %s size %d' % (p, len(new)), write=True)
                back = dbg(img, 'cat %s' % p)
                if back != new:
                    print('ABORT: %s 读回与预期不一致' % p); return 3
                entry['verified'] = 'readback_ok'
                print('已重新启用 %s 的服务（删掉 %d 行 disabled）' % (p, len(disabled)))
            report['actions'].append(entry)

        # --- 3) 落 mainline device 层文件 ---
        for f in sorted(MAINLINE_ROOTDIR.iterdir()):
            dest = '/system/etc/%s' % f.name
            content = f.read_bytes()
            entry = {'op': 'install', 'path': dest, 'src': str(f.relative_to(WT)),
                     'sha256': sha(content), 'size': len(content)}
            if a.dry_run:
                entry['dry_run'] = True
            else:
                tmp = Path(work) / f.name
                tmp.write_bytes(content)
                if exists(img, dest):
                    dbg(img, 'rm %s' % dest, write=True)
                back = image_write(img, tmp, dest)
                if back != content:
                    print('ABORT: %s 读回不一致' % dest); return 3
                entry['verified'] = 'readback_ok'
            report['actions'].append(entry)
        print('已投递 mainline device 层文件 %d 份' % len(list(MAINLINE_ROOTDIR.iterdir())))

        # --- 4) 属性残留检查 ---
        props = [k for k in allfiles if k.endswith('build.prop') or k.endswith('default.prop')]
        report['prop_files'] = props
        for p in (props or ['/system/build.prop', '/default.prop']):
            if not exists(img, p):
                continue
            data = dbg(img, 'cat %s' % p)
            t = data.decode(errors='replace')
            hit = re.findall(r'(?m)^.*z1\.stock_kernel.*$', t)
            if not hit:
                continue
            # 光删标记文件不够：属性本身由 build.prop 写死时，ro.z1.stock_kernel 仍为 1，
            # 那些 HIDL 门控会一直走 stock 的 passthrough 分支。必须一起改掉。
            new_t = re.sub(r'(?m)^.*z1\.stock_kernel.*$\n?', '', t)
            entry = {'op': 'strip_stock_prop', 'path': p, 'removed': hit,
                     'before_sha256': sha(data), 'after_sha256': sha(new_t.encode())}
            blocks = dbg(img, 'blocks %s' % p).decode().split()
            if len(blocks) != 1 or not blocks[0].isdigit() or len(new_t.encode()) > bs:
                entry['skipped'] = '非单块或改写后超块(%d B)，需人工处理' % len(new_t.encode())
                report['actions'].append(entry)
                print('SKIP %s: %s' % (p, entry['skipped']))
                continue
            if a.dry_run:
                entry['dry_run'] = True
                report['actions'].append(entry); continue
            pad = new_t.encode() + b'\0' * (bs - len(new_t.encode()))
            with open(img, 'r+b') as fh:
                fh.seek(int(blocks[0]) * bs)
                fh.write(pad)
            dbg(img, 'set_inode_field %s size %d' % (p, len(new_t.encode())), write=True)
            back = dbg(img, 'cat %s' % p)
            if back.rstrip(b'\0') != new_t.encode().rstrip(b'\0'):
                print('ABORT: %s 改写后读回不一致' % p); return 3
            entry['verified'] = 'readback_ok'
            report['actions'].append(entry)
            print('已从 %s 移除 stock 门控属性: %s' % (p, hit))

        # --- 5) 完整性与尺寸 ---
        if not a.dry_run:
            r = subprocess.run(['e2fsck', '-fn', str(img)], capture_output=True, text=True, timeout=180)
            report['e2fsck_rc'] = r.returncode
            report['e2fsck_tail'] = r.stdout.strip().splitlines()[-3:]
            print('e2fsck rc=%d' % r.returncode)
            sz = img.stat().st_size
            report['output_bytes'] = sz
            report['output_sha256'] = sha(img)
            if sz > ANDROID_PART_LIMIT:
                print('ABORT: 输出 %d B 超过 android 分区 %d B' % (sz, ANDROID_PART_LIMIT))
                return 3
            print('输出: %s (%d B, sha=%s)' % (img, sz, report['output_sha256'][:16]))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    rp = Path(outdir) / 'ADAPT_REPORT.json'
    rp.write_text(json.dumps(report, ensure_ascii=False, indent=1))
    print('报告: %s   动作=%d 条  备注=%d 条' % (rp, len(report['actions']), len(report['notes'])))
    print('下一步: tools/z1/z1_a9_flashpkg.sh --boot <a9 boot.img> --system %s' % (
        (Path(a.out) / 'system_mainline.img')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
