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
SPARSE_MAGIC = 0xED26FF3A   # Android sparse 的真 magic

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
    """把主机文件写到镜像内绝对路径 dest，必要时逐级建目录；写完读回校验。

    debugfs 的 `mkdir`/`write` 第二个参数都是**相对当前目录**的名字，不是绝对路径：
    直接喂 `/vendor/etc` 这种多级新目录时，它会静默把条目写成根下一个怪名字，
    于是读回 0 B（第一次拿真 A9 GSI 投 /vendor/etc/fstab.z1 就是这样 ABORT 的）。
    所以这里逐级 cd/mkdir，每级先用 exists() 判存在，最后 write + set_inode_field。
    """
    parts = [x for x in dest.split('/') if x]
    name = parts.pop()
    cwd = '/'
    for seg in parts:
        target = ('' if cwd == '/' else cwd) + '/' + seg
        if not exists(image, target):
            dbg_script(image, ['cd %s' % cwd, 'mkdir %s' % seg])
            if not exists(image, target):
                raise RuntimeError('建目录失败 %s' % target)
        cwd = target
    dbg_script(image, ['cd %s' % cwd, 'write %s %s' % (hostfile, name)])
    dbg_script(image, img_mode(image, dest, mode))
    return dbg(image, 'cat %s' % dest)


def img_mode(image, dest, mode):
    """set_inode_field 的 mode 要写成八进制数(不带前导 0 也行)，且必须用绝对路径。"""
    return ['set_inode_field %s mode %s' % (dest, mode)]


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


def dir_kind(image, path):
    """'dir' / 'link' / 'missing'。GSI 的 /vendor 是**符号链接**，
    debugfs 的 `cd` 进不去、`write` 会静默落到别处，读回 0 B —— 必须先分辨。"""
    r = subprocess.run(['debugfs', '-R', 'stat %s' % path, str(image)],
                       capture_output=True, timeout=30)
    blob = (r.stdout + r.stderr).decode(errors='replace')
    if 'Type:' not in blob:
        return 'missing'
    m = re.search(r'Type:\s+(\S+)', blob)
    return (m.group(1) if m else 'unknown')


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


def to_raw(image, workdir):
    """LOS16/GSI 常交回 Android sparse image。真实格式是 magic 0xED26FF3A + 28B 头 +
    12B 块头，且 DONT_CARE **必须推进输出偏移** —— 本仓原先那份手写实现把两者都写错了
    (magic 0x3AFF26AF、按 9 个 u32 解头、DONT_CARE 不 seek)，第一次拿真镜像跑就把根
    inode 写坏、e2fsck 直接把 /system 清成空壳。现在统一走 z1_a9_sparse2raw。"""
    import z1_a9_sparse2raw as s2r
    if not s2r.is_sparse(image):
        return Path(image), False
    raw = Path(workdir) / 'system_raw.img'
    s2r.to_raw(str(image), str(raw), verbose=False)
    return raw, True


def policy_is_loadable(path):
    """投进 precompiled_sepolicy 前先验内核能不能吃：magic 必须是 SELINUX_MAGIC(0x45524f53)，
    version 必须落在内核的 [POLICYDB_VERSION_MIN, POLICYDB_VERSION_MAX] 区间。

    本轮实测教训：r2 ramdisk 里那个 384 KB 的 `sepolicy` 其实是 libselinux 编译后的
    file_contexts 二进制(magic 0xF97CFF8C, ver 8)。投进去只会被 /sys/fs/selinux/load 拒，
    比老实报 NOPOLICY 更坏 —— 这种错必须在**出片阶段**把住，不能留给串口。
    """
    import struct
    head = Path(path).read_bytes()[:8]
    if len(head) < 8:
        return False, '不足 8 字节'
    magic, ver = struct.unpack('<II', head)
    hdr = WT / 'security' / 'selinux' / 'include' / 'security.h'
    lo = hi = None
    if hdr.is_file():
        defs = dict((m.group(1), int(m.group(2)))
                    for m in re.finditer(r'#define\s+(POLICYDB_VERSION_\w+)\s+(\d+)', hdr.read_text()))
        lo = defs.get('POLICYDB_VERSION_BASE')
        hi = (defs.get('POLICYDB_VERSION_NEVERAUDIT') or defs.get('POLICYDB_VERSION_XPERMS_IOCTL'))
    if magic != 0x45524F53:
        return False, 'magic=0x%08X 不是 SELINUX_MAGIC 0x45524F53，多半不是策略镜像' % magic
    if lo is not None and hi is not None and not (lo <= ver <= hi):
        return False, 'version=%d 不在内核区间 %d..%d' % (ver, lo, hi)
    return True, 'magic OK，version=%d（内核允许 %s..%s）' % (ver, lo, hi)


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
        # 镜像内 system 内容可能在根（A-only/Treble GSI：分区挂到 /system，所以根就是 system 内容），
        # 也可能在 /system 子目录（system-as-root）。前一种情况写死 /system/etc 会把文件投进
        # 一个新建的空目录，读回必然不一致 —— 第一次拿真 A9 GSI 跑就是这样 ABORT 的。
        if exists(img, '/build.prop') or exists(img, '/bin'):
            P = ''
        else:
            P = '/system'
        report['system_prefix'] = P or '(根即 system)'
        # 只有 /vendor 是**真目录**时才能当落点；GSI 里它常是符号链接（见 dir_kind）
        VP = None
        for cand in ('/vendor', '/system/vendor'):
            if dir_kind(img, cand) == 'dir' and not cand.startswith(P + '/vendor') or                (P == '' and dir_kind(img, cand) == 'dir'):
                if dir_kind(img, cand) == 'dir':
                    VP = cand
                    break
        report['vendor_prefix'] = VP
        report['vendor_kind'] = {c: dir_kind(img, c) for c in ('/vendor', '/system/vendor')}
        # 每个文件给出**多个候选落点**：A9 具体扫哪几个路径本机无法离线确认，所以两处都放，
        # 且一律用带 .z1 后缀的新文件，绝不覆盖 AOSP 自己的 ueventd.rc / fstab.*。
        TARGETS = {
            'init.z1.rc': [P + '/etc/init/hw/init.z1.rc'] + ([VP + '/etc/init/hw/init.z1.rc'] if VP else []),
            'init.z1.network.rc': [P + '/etc/init/z1_network.rc'],
            'ueventd.z1.rc': [P + '/etc/ueventd.z1.rc'] + ([VP + '/etc/ueventd.z1.rc'] if VP else []),
            'fstab.z1': ([VP + '/etc/fstab.z1'] if VP else []) + [P + '/etc/fstab.z1'],
            # 设备级 manifest：有真 /vendor 就投 A9 认的位置；否则投一个不会与
            # AOSP 框架 manifest(/system/etc/vintf/manifest.xml) 撞名的中转名，
            # 由交棒脚本在运行时把它放进可写的 /vendor/manifest.xml。
            'manifest.xml': ([VP + '/manifest.xml'] if VP else [P + '/etc/z1_device_manifest.xml']),
            'z1_usb_setup.sh': [P + '/bin/z1_usb_setup.sh'],
            # A9 的 precompiled 策略：只在本仓 rootdir/ 下**确实放了这份输入件**时才投；
            # GSI 配的是它自己的 cil（与 LOS16 策略不同源），所以默认不混投，见 A9_P11_MANIFEST。
            'precompiled_sepolicy': [P + '/etc/selinux/precompiled_sepolicy'],
        }
        # A-only 的 GSI 不带 A9 自己的 PID1（init/ueventd），而整机能不能起就卡在这两个二进制上。
        # 所以把它们做成**普通输入件**：仓里放了就一并投进 <P>/bin，没放就照常报缺。
        SEED = MAINLINE_ROOTDIR / 'bin'
        for nm in ('init', 'ueventd'):
            f = SEED / nm
            if f.is_file():
                TARGETS[nm] = [P + '/bin/' + nm]
            else:
                report['notes'].append('缺输入件 %s（A-only GSI 不提供 A9 的 PID1，'
                                       '补上后本机就能把 system 改成自足版）' % f.relative_to(WT))
        placed, skipped = [], []
        srcs = [x for x in MAINLINE_ROOTDIR.iterdir() if x.is_file()]
        for nm in ('init', 'ueventd'):
            f = SEED / nm
            if f.is_file():
                srcs.append(f)          # 输入件在 bin/ 子目录里，不一起列进来就永远投不出去
        for f in sorted(srcs, key=lambda x: x.name):
            if f.name == 'precompiled_sepolicy':
                ok, why = policy_is_loadable(f)
                if not ok:
                    report['notes'].append('拒绝投 %s：%s —— 交棒时报 NOPOLICY 更安全' % (f.name, why))
                    print('SKIP precompiled_sepolicy: %s' % why)
                    continue
                report['notes'].append('precompiled_sepolicy 可载入性：%s' % why)
            dests = TARGETS.get(f.name)
            if dests is None:
                skipped.append((f.name, '本工具没有为它定义落点'))
                continue
            content = f.read_bytes()
            tmp = Path(work) / f.name
            tmp.write_bytes(content)
            for dest in dests:
                entry = {'op': 'install', 'path': dest, 'src': str(f.relative_to(WT)),
                         'sha256': sha(content), 'size': len(content)}
                if a.dry_run:
                    entry['dry_run'] = True
                    report['actions'].append(entry)
                    continue
                if exists(img, dest):
                    # 权威 system（r2 那份）本来就带 /system/bin/init、/bin/ueventd(symlink)。
                    # 拿副本 rm+write 覆盖会把符号链接变成 1.6 MB 的普通文件 —— 功能等价但
                    # 白占 3 MB 并改动镜像原状。所以"已存在且看着可用"就跳过，只补真缺的。
                    st = dbg(img, 'stat %s' % dest).decode(errors='replace')
                    typ = (re.search(r'Type:\s+(\S+)', st) or ['', ''])[1]
                    size = int((re.search(r'Size:\s+(\d+)', st) or ['', '0'])[1])
                    PRESERVE = ('init', 'ueventd')   # 只有 ELF 副本让位给原件
                    if (typ in ('dir',) or size > 0) and os.path.basename(dest) in PRESERVE:
                        entry['skipped'] = '镜像里已有 %s(Type=%s Size=%d)，不用副本覆盖' % (dest, typ, size)
                        report['actions'].append(entry)
                        report['notes'].append('%s 已存在，跳过：%s' % (dest, entry['skipped']))
                        print('SKIP %s（镜像里已有，Type=%s Size=%d）' % (dest, typ, size))
                        continue
                    before = dbg(img, 'cat %s' % dest)
                    entry['overwrote'] = {'sha256_before': sha(before), 'bytes_before': len(before)}
                    dbg(img, 'rm %s' % dest, write=True)
                back = image_write(img, tmp, dest, mode='0755' if ('/bin/' in dest or f.name in ('init', 'ueventd')) else '0644')
                if back != content:
                    print('ABORT: %s 读回不一致（期望 %d B 实得 %d B）' % (dest, len(content), len(back)))
                    return 3
                entry['verified'] = 'readback_ok'
                report['actions'].append(entry)
                placed.append(dest)
            # 可执行位：init/ueventd 这类进 /bin 的东西必须是 0755 —— 投成 0644 的话
            # 交棒 exec 直接失败（本轮最终审计就是靠比对 debugfs 的 Mode 抓到的）。
            exe = f.name.endswith('.sh') or f.name in ('init', 'ueventd') or '/bin/' in ''.join(dests)
            for d in dests:
                if '/bin/' in d or exe:
                    dbg(img, 'set_inode_field %s mode 100755' % d, write=True)
        report['placed'] = placed
        report['skipped_src'] = skipped
        print('已投递 mainline device 层：源文件 %d 份 -> 落点 %d 处（system 前缀=%s，vendor=%s）'
              % (len(srcs), len(placed), P or '(根即 system)', VP or '无'))
        if skipped:
            report['notes'].append('未定义落点的源文件: %s' % skipped)
        report['notes'].append('A9 具体扫描的 rc/ueventd/manifest 路径本机无法离线确认，'
                               '故同一文件放了多个候选落点，且都用 .z1 后缀不覆盖 AOSP 原件')

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
