#!/usr/bin/env python3
"""z1_a9_sepolicy_check.py —— 静态核 mainline sepolicy 交付物的一致性。

本机没有 LOS16 源树，跑不了 checkpolicy/sesearch，所以这个脚本只做**能在本机证实**的四类检查：
 1) 语法：genfscon/file_contexts 行格式、上下文 u:type:s0 形态；
 2) genfscon 引用的文件系统，本内核到底支持不支持（对着 a9_wt/.config 判）——
    引用一个内核里没有的 fs，表现为 restorecon 静默不生效，事后极难归因；
 3) device_file_contexts 里每个 /dev 节点是否有对应内核能力；
 4) 标签与权限两张表是否互相覆盖：device_file_contexts 有条目但 ueventd.z1.rc 没有权限行
    （enforcing 阶段会"标签对了但打不开"），以及反过来。
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(HERE, '..', '..'))
SEP = os.path.join(WT, 'android9', 'mainline', 'sepolicy', 'vendor')
CFG = os.path.join(WT, '.config')
UEV = os.path.join(WT, 'android9', 'mainline', 'rootdir', 'ueventd.z1.rc')

# genfscon 的 fsname -> 内核里让它存在的那个 config 符号
FS_NEEDS = {
    'securityfs': ['CONFIG_SECURITYFS'],
    'pstore': ['CONFIG_PSTORE'],
    'functionfs': ['CONFIG_USB_F_FS'],
    'configfs': ['CONFIG_CONFIGFS_FS'],
    'fusectl': ['CONFIG_FUSE_FS'],
    'devpts': ['CONFIG_DEVPTS_MULTIPLE_INSTANCES', 'CONFIG_UNIX'],
    'cgroup': ['CONFIG_CGROUPS'],
    'tmpfs': ['CONFIG_TMPFS'],
    'rootfs': ['CONFIG_TMPFS'],
    'debugfs': ['CONFIG_DEBUG_FS'],
    'selinuxfs': ['CONFIG_SECURITYFS'],
}
DEV_NEEDS = {
    '/dev/binder': ['CONFIG_ANDROID_BINDER_IPC'],
    '/dev/hwbinder': ['CONFIG_ANDROID_BINDER_IPC'],
    '/dev/vndbinder': ['CONFIG_ANDROID_BINDER_IPC'],
    '/dev/ashmem': ['CONFIG_ANDROID_ASHMEM'],
    '/dev/fb0': ['CONFIG_FB', 'CONFIG_FB_SIMPLE'],
    '/dev/graphics/fb0': ['CONFIG_FB'],
    '/dev/input': ['CONFIG_INPUT'],
    '/dev/net/tun': ['CONFIG_TUN'],
    '/dev/watchdog': ['CONFIG_MEDIATEK_WATCHDOG', 'CONFIG_WATCHDOG_CORE'],
    '/dev/ttyGS0': ['CONFIG_USB_CONFIGFS_ACM'],
    '/dev/ttyS0': ['CONFIG_SERIAL_8250', 'CONFIG_SERIAL_EARLYCON'],
    '/dev/usb-ffs/adb': ['CONFIG_USB_F_FS'],
    '/dev/iio:device0': ['CONFIG_IIO'],
    '/dev/dma_heap/system': ['CONFIG_DMABUF_HEAPS_SYSTEM'],
    '/dev/usb-ffs/adb(/.*)?': ['CONFIG_USB_F_FS'],
    '/dev/dma_heap/linux,cma': ['CONFIG_DMABUF_HEAPS_CMA'],
}
CTX_RE = re.compile(r'^u:[a-z0-9_]+:[a-z0-9_]+:s0$')
fails = []

cfg = open(CFG, encoding='utf-8', errors='replace').read()
def have(sym):
    return bool(re.search(r'^%s=[ym]' % re.escape(sym), cfg, re.M))

def note(ok, msg):
    if not ok:
        fails.append(msg)
    print('%s %s' % ('OK  ' if ok else 'FAIL', msg))

# --- 1) genfs_contexts ---
types_used = set()
lines = [l for l in open(os.path.join(SEP, 'genfs_contexts'), encoding='utf-8')
         if l.strip() and not l.lstrip().startswith('#')]
for l in lines:
    parts = l.split()
    note(len(parts) == 4 and parts[0] == 'genfscon', 'genfs 语法: %s' % l.strip())
    if len(parts) != 4:
        continue
    _, fs, path, ctx = parts
    note(path.startswith('/'), 'genfs 路径以 / 开头: %s' % path)
    note(bool(CTX_RE.match(ctx)), 'genfs 上下文形态: %s' % ctx)
    types_used.add(ctx.split(':')[2])
    req = FS_NEEDS.get(fs)
    if req is None:
        note(False, 'genfs fs=%s 本脚本没有内核映射，需人工确认内核是否支持' % fs)
    else:
        on = [s for s in req if have(s)]
        note(bool(on), 'genfs fs=%s 有内核支撑 (%s)' % (fs, ','.join(on) or '无'))

# --- 2/3) device_file_contexts ---
devs = []
for l in [x for x in open(os.path.join(SEP, 'device_file_contexts'), encoding='utf-8')
          if x.strip() and not x.lstrip().startswith('#')]:
    parts = l.split()
    note(len(parts) == 2, 'dev 语法(路径+上下文): %s' % l.strip())
    if len(parts) != 2:
        continue
    dev, ctx = parts
    note(dev.startswith('/dev/'), 'dev 路径: %s' % dev)
    note(bool(CTX_RE.match(ctx)), 'dev 上下文形态: %s (%s)' % (ctx, dev))
    types_used.add(ctx.split(':')[2])
    devs.append(dev)
    req = DEV_NEEDS.get(dev)
    if req is None:
        note(False, 'dev %s 没有内核映射，需人工确认' % dev)
    else:
        on = [s for s in req if have(s)]
        note(bool(on), 'dev %s 有内核支撑 (%s)' % (dev, ','.join(on) or '无'))

# --- 4) 与 ueventd 的互相覆盖 ---
uev_paths = set()
if os.path.exists(UEV):
    for l in open(UEV, encoding='utf-8'):
        if l.lstrip().startswith('#') or not l.strip():
            continue
        p = l.split()[0]
        if p.startswith('/dev/'):
            uev_paths.add(p)
else:
    note(False, '找不到 %s' % UEV)

def norm(p):
    """规范成 (前缀, 通配种类)。三种写法语义不同，混为一谈会假失败：
      exact      /dev/ttyS0
      dironly    /dev/dma_heap/*  与 file_contexts 的 (/.*)?  -> 只匹配该目录下的东西
      prefix     /dev/iio:device* -> 匹配任意后缀(iio:device0/iio:device1)
    """
    for suf in ('(/.*)?', '(/.*)'):
        if p.endswith(suf):
            return p[:-len(suf)].rstrip('/'), 'dironly'
    if p.endswith('/*'):
        return p[:-2], 'dironly'
    if p.endswith('*'):
        return p[:-1], 'prefix'
    return p.rstrip('/'), 'exact'

def match(a, b):
    """a 是否需要被 b 覆盖(两侧都可能是通配)。"""
    ap, ak = a; bp, bk = b
    if ap == bp:
        return True
    if ak == 'dironly' and (bp == ap or bp.startswith(ap + '/')):
        return True
    if bk == 'dironly' and (ap == bp or ap.startswith(bp + '/')):
        return True
    if ak == 'prefix' and bp.startswith(ap):
        return True
    if bk == 'prefix' and ap.startswith(bp):
        return True
    return False

nm = {d: norm(d) for d in devs}
up = {p: norm(p) for p in uev_paths}
unlabeled = [p for p in uev_paths if not any(match(up[p], nm[d]) for d in devs)]
no_perm   = [d for d in devs if not any(match(nm[d], up[p]) for p in uev_paths)]
note(not unlabeled, 'ueventd 给了权限但 sepolicy 没标签: %s' % (unlabeled or '无'))
note(not no_perm, 'sepolicy 有标签但 ueventd 没给权限: %s' % (no_perm or '无'))

# --- 5) 用到的 type 是否都登记在 required_types.txt ---
declared = {x.strip() for x in open(os.path.join(SEP, 'required_types.txt'), encoding='utf-8') if x.strip()}
missing = sorted(types_used - declared)
note(not missing, '引用但未登记的 type(构建机需确认其策略里已声明或本仓自定义): %s' % (missing or '无'))

print('\nSEP_POLICY_FAIL=%d' % len(fails))
sys.exit(1 if fails else 0)
