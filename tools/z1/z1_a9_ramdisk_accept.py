#!/usr/bin/env python3
"""z1_a9_ramdisk_accept.py —— 验收 A9 generic ramdisk，并（可选）直接接出片链。

为什么需要：LOS16 机交回来的东西形状对不对，靠"能不能 gunzip"是判不出来的。
本项目已经两次栽在 ramdisk 与引导参数不同步上（v13.1 initrd 截断、v13.4 覆盖），
所以交回来先做**内容级**验收，再谈打包。

判据分三层：
 L1 封装：要么带 512B MTK ROOTFS 头(magic 88 16 88 58 + 0x08 处 ROOTFS + 0x04 小端长度==body)，
         要么是裸 gzip/newc cpio。两种都认，但带头的必须长度自洽。
 L2 形状判据分两种（尺子不同，别混）：
     A) A9 first-stage：/init 是 32-bit ARM 静态 ELF；有 first_stage_*.rc 或 init.*.rc；
        有 sepolicy；有 fstab.*；有 ueventd.rc。
     B) 本机 bring-up + 交棒型：/init 是 busybox sh 脚本且含 [a9hand]，必须有 /bin/busybox、
        /sbin/z1_a9_handoff.sh(判定版)与 /sbin/z1_a9_probe；此时 sepolicy/fstab 由交棒从
        /system 侧取，所以不再按 A) 判死。
 L3 语义红线（出现即 REJECT，因为都是 stock 线或错误路线留下的东西）：
         解出文件里有 /z1_stock_kernel 标记、ro.z1.stock_kernel 字符串，
         或 rc 里出现 `mount binderfs`（本内核 ANDROID_BINDERFS=n，节点由驱动静态注册）。
"""
import argparse, os, re, shutil, struct, subprocess, sys, tempfile


def sh(cmd, **kw):
    kw.setdefault('shell', isinstance(cmd, str))
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def unwrap(path, workdir):
    """返回 (body_gz_path 或 cpio 路径, 是否有 MTK 头, 头声明长度, 实际 body 长度)"""
    raw = open(path, 'rb').read()
    if len(raw) < 512:
        raise SystemExit('L1-FAIL: 文件小于 512B，不可能带头')
    # MTK magic 是 4 字节，别拿 raw[:8] 去比(第一版就是这么全案 L1 挂掉的)
    if raw[:4] == bytes.fromhex('88168858') and raw[8:14] == b'ROOTFS':
        n = struct.unpack('<I', raw[4:8])[0]
        body = raw[512:]
        if n != len(body):
            raise SystemExit('L1-FAIL: MTK 头声明长度=%d 实际 body=%d（v13.1 同型的截断）' % (n, len(body)))
        p = os.path.join(workdir, 'body.gz')
        open(p, 'wb').write(body)
        return p, True, n, len(body)
    if raw[:3] == b'\x1f\x8b\x08':
        p = os.path.join(workdir, 'body.gz')
        open(p, 'wb').write(raw)
        return p, False, 0, len(raw)
    if raw[:6] in (b'070701', b'070702'):
        p = os.path.join(workdir, 'body.cpio')
        open(p, 'wb').write(raw)
        return p, False, 0, len(raw)
    raise SystemExit('L1-FAIL: 既不是带 MTK 头的 gzip，也不是 gzip/newc cpio')


def extract(body, workdir):
    d = os.path.join(workdir, 'root')
    os.makedirs(d, exist_ok=True)
    src = f'zcat "{body}"' if body.endswith('.gz') else f'cat "{body}"'
    r = sh(f'{src} | (cd "{d}" && cpio -idm --quiet --no-absolute-filenames)')
    if r.returncode != 0:
        raise SystemExit('L1-FAIL: cpio 解包失败: ' + (r.stderr or '')[-200:])
    return d


def walk(root):
    out = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            out[os.path.relpath(p, root)] = p
    return out


def is_arm_static_elf(p):
    o = sh(['file', '-b', p]).stdout
    return ('ELF' in o and 'ARM' in o and 'statically linked' in o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ramdisk')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--allow-stock-markers', action='store_true',
                    help='仅用于对照实验；正常验收不要加')
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix='a9rd_')
    bad, warn = [], []
    try:
        body, has_hdr, declared, blen = unwrap(a.ramdisk, work)
        print('L1 封装: %s  body=%d B' % ('带 512B MTK ROOTFS 头(长度自洽)' if has_hdr
                                          else '裸 gzip/cpio', blen))
        root = extract(body, work)
        files = walk(root)
        print('L2 条目数: %d' % len(files))

        top = {k for k in files if '/' not in k}
        def find(pat):
            return [k for k in files if re.search(pat, k)]

        # ---- 先分形状：两种合法形状，判据不同，别拿 A9 first-stage 的尺去量交棒型 ----
        # A) A9 generic ramdisk：/init 是 ARM 静态 ELF，带 first_stage_*.rc / sepolicy / fstab
        # B) 本机 bring-up + 交棒型：/init 是 busybox sh 脚本，靠 /sbin/z1_a9_handoff.sh
        #    顶掉 first-stage 职责（策略载入与 fstab 都从 /system 侧取，见 A9_HANDOFF_20261018.md）
        handoff = ('init' in files and any(k.endswith('z1_a9_handoff.sh') for k in files)
                   and b'[a9hand]' in open(files['init'], 'rb').read())
        if handoff:
            print('L2 形状: bring-up + 交棒型（非 A9 first-stage）')
            if is_arm_static_elf(files['init']):
                bad.append('交棒型的 /init 应当是 busybox sh 脚本，这里却是 ELF —— 形状判错')
            for need, why in (('/bin/busybox', '/init 全靠它'),
                              ('sbin/z1_a9_handoff.sh', '交棒判据与挂载动作'),
                              ('sbin/z1_a9_probe', '板级能力探针，交棒前 must run')):
                if need.lstrip('/') not in files:
                    bad.append('交棒型缺 %s（%s）' % (need, why))
            hpath = [k for k in files if k.endswith('z1_a9_handoff.sh')][0]
            if b'RESULT_STEP_handoff_probe=GO' not in open(files[hpath], 'rb').read():
                bad.append('%s 里没有 RESULT_STEP_handoff_probe=GO，不是判定版' % hpath)
        else:
            if 'init' not in files:
                bad.append('没有 /init')
            elif not is_arm_static_elf(files['init']):
                bad.append('/init 不是 ARM 静态 ELF: ' + sh(['file', '-b', files['init']]).stdout.strip())
            if not find(r'first_stage.*\.rc$') and not find(r'^init\..*\.rc$'):
                bad.append('既无 first_stage_*.rc 也无 init.*.rc，且没有交棒脚本 —— '
                           '既不像 A9 first-stage，也不是带 [a9hand] 的 bring-up')
            if not find(r'(^|/)sepolicy$'):
                bad.append('缺 sepolicy（A9 首阶段策略，缺了 init 加载不上）')
            if not find(r'(^|/)fstab\.'):
                bad.append('缺 fstab.*（mount_all 无来源）')
            if not find(r'(^|/)ueventd\.rc$'):
                warn.append('没有 ueventd.rc —— 若走 /system/etc/ueventd.rc 可接受，需人工确认')

        # L3 语义红线
        stockish, binderfs, ionrefs = [], [], []
        for k, p in files.items():
            try:
                if os.path.getsize(p) > 8 << 20:
                    continue
                data = open(p, 'rb').read()
            except OSError:
                continue
            if b'/z1_stock_kernel' in data or b'ro.z1.stock_kernel' in data:
                stockish.append(k)
            if b'binderfs' in data and re.search(rb'mount\s+binderfs', data):
                binderfs.append(k)
            if re.search(rb'/dev/ion\b', data) and not k.startswith('sbin/'):
                ionrefs.append(k)
        if stockish and not a.allow_stock_markers:
            bad.append('ramdisk 里残留 stock 门控(应撤销的对象): %s' % ', '.join(stockish[:5]))
        if binderfs:
            bad.append('rc 里有 `mount binderfs`，但本内核 ANDROID_BINDERFS=n: %s' % ', '.join(binderfs[:5]))
        if ionrefs:
            warn.append('引用了 mainline 上不存在的 /dev/ion: %s' % ', '.join(ionrefs[:5]))

        for w in warn:
            print('WARN: %s' % w)
        if bad:
            for m in bad:
                print('REJECT: %s' % m)
            print('RAMDISK_ACCEPT=REJECT')
            return 1
        if not has_hdr:
            # pack 的 rd_body() 要求 magic 88168858 + 0x08 ROOTFS + 0x04 长度自洽，
            # 裸 gzip 会被它直接拒收 —— 所以这一支的下一跳必须是"先重打带头版本"，
            # 不能把 pack 命令写在前面误导人。
            print('NEXT: 先重打带头版本，再出片：')
            print('      mkdir -p /tmp/rd_a9_final && (zcat %s | (cd /tmp/rd_a9_final && cpio -idm --quiet --no-absolute-filenames))' % a.ramdisk)
            print('      tools/z1_repack_a9_ramdisk.sh /tmp/rd_a9_final <工作区根>/out/ramdisk_v13_7_acm.gz <工作区根>/out/ramdisk_a9p5.gz')
            print('      A9_RAMDISK=<工作区根>/out/ramdisk_a9p5.gz tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p5.img')
            print('RAMDISK_ACCEPT=ACCEPT (需先重打封装)')
            return 0
        end = 0x84100000 + blen
        print('下一跳: A9_RAMDISK=%s tools/z1/z1_a9_p2.sh pack 1b out/boot_mainline_a9p5.img'
              '   (算得 linux,initrd-end=%s)' % (a.ramdisk, hex(end)))
        print('RAMDISK_ACCEPT=ACCEPT')
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
