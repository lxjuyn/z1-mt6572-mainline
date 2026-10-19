#!/usr/bin/env python3
"""z1_a9_devlayer_check.py —— AOSP device 层内部对账（mainline 变体）。

四张表互相对，哪一张单独看都"没问题"，对不上才是真故障：
 A. manifest.xml 声明的 HIDL HAL  ->  device.mk 的 PRODUCT_PACKAGES 必须提供对应服务，
    否则 hwservicemanager 里没有该实例，客户端拿到 NOT_FOUND：SF 的 configstore 查询
    正是这条路径（stock 线 S1G 的段错就是它），而 mainline 分支把 fake 工厂撤了却没装
    AOSP 默认实现 —— 等于把同一个坑重新挖回来。
 B. rootdir/ 里我们投递的文件      ->  必须被某条 PRODUCT_COPY_FILES 引用（写了却没装=白写）
 C. 选 HAL 模块的属性              ->  mainline 分支必须有 ro.hardware.gralloc / hwcomposer，
    否则 allocator@2.0-service 会去找 gralloc.<ro.board.platform=mt6572>.so 而失败。
 D. sepolicy 目录                  ->  BOARD_SEPOLICY_DIRS 指过去的路径里得有文件。

stock 分支的 device.mk 在 origin/wip/android9-stock-s1 上；本机没有 LOS16 源树，
所以这里对的是"仓库里那份 device 层"，改完由构建机原样取用。
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(HERE, '..', '..'))
ROOT = os.path.normpath(os.path.join(WT, '..'))
BRANCH = 'origin/wip/android9-stock-s1'
DEV = BRANCH + ':android9/device/jiabo/z1'
MAIN = os.path.join(WT, 'android9', 'mainline')
fails = []


def sh(cmd):
    return subprocess.run(cmd, shell=True, cwd=WT, capture_output=True, text=True)


def report(ok, label, detail=''):
    print('%s %-46s %s' % ('OK  ' if ok else 'FAIL', label, detail))
    if not ok:
        fails.append(label)


def mainline_branch(text):
    """取 ifneq/else 的 mainline 侧内容：本文件里 mainline 逻辑分散在三处 if* 块，
    简单可靠的做法是——按行扫描，维护 in_stock 标志，收集"不在 stock 分支内"的行。"""
    out, in_stock, depth = [], False, 0
    for ln in text.splitlines():
        st = ln.strip()
        if st.startswith('ifeq') and 'stock' in st and 'ifneq' not in st:
            in_stock, depth = True, 1
            continue
        if st.startswith('ifneq') and 'stock' in st:
            in_stock, depth = False, 1   # ifneq(...,stock): 块内是 mainline
            continue
        if depth:
            if st.startswith('if'):
                depth += 1
            elif st == 'endif':
                depth, in_stock = 0, False
                continue
        if st.startswith('else'):
            in_stock = not in_stock if depth else not in_stock
            continue
        if not in_stock:
            out.append(ln)
    return '\n'.join(out)


def main():
    r = sh('git show %s/device.mk' % DEV)
    if r.returncode != 0:
        print('取不到 device.mk: %s' % r.stderr.strip()[:160]); return 2
    devmk_stock = r.stdout
    # 优先用本仓已修正的 mainline device.mk（若存在），否则用 stock 分支那份
    local = os.path.join(MAIN, 'device.mk')
    src, src_name = (open(local).read(), 'android9/mainline/device.mk') \
        if os.path.isfile(local) else (devmk_stock, 'stock 分支 device.mk(未修正)')
    ml = mainline_branch(src)
    print('对账对象: %s' % src_name)

    # A) manifest 声明 vs PRODUCT_PACKAGES
    mr = sh('git show %s:rootdir/manifest.xml' % DEV)
    mline = os.path.join(MAIN, 'rootdir', 'manifest.xml')
    man = open(mline).read() if os.path.isfile(mline) else (mr.stdout if mr.returncode == 0 else '')
    decl = set()
    for m in re.finditer(r'<name>\s*(android\.hardware\.[\w.]+)\s*</name>.*?<version>\s*([\d.]+)\s*</version>',
                         man, re.S):
        decl.add((m.group(1), m.group(2)))
    pkgs = set(re.findall(r'(android\.hardware\.[\w.@+-]+|gralloc\.z1|hwcomposer\.z1|sdcard\.z1_fuse)', ml))
    # HAL 名 -> 期望的包名片段
    NEED = {
        'android.hardware.graphics.allocator': 'graphics.allocator@2.0-service',
        'android.hardware.graphics.composer': 'graphics.composer@2.1-service',
        'android.hardware.graphics.mapper': 'graphics.mapper@2.0-impl',
        'android.hardware.configstore': 'configstore@1.1-service',
        'android.hardware.health': 'health@2.0-service.z1',
        'android.hardware.audio': 'audio@2.0-service',
        'android.hardware.audio.effect': 'audio.effect@2.0-impl',
        'android.hardware.keymaster': 'keymaster@3.0-service',
        'android.hardware.cas': 'cas@1.0-service',
        'android.hardware.media.omx': 'media.omx@1.0-service',
    }
    print('manifest 声明 %d 个 HAL；PRODUCT_PACKAGES 命中 %d 条' % (len(decl), len(pkgs)))
    for name, ver in sorted(decl):
        want = NEED.get(name)
        if want is None:
            report(True, '声明 %s(无需对应服务包)' % name)
            continue
        ok = any(want in p or name + '@' + ver in p for p in pkgs)
        report(ok, 'manifest %s@%s 有实现包' % (name, ver),
               '' if ok else '期望包含 %s；mainline 分支没有 -> hwservicemanager 里会缺该实例' % want)

    # B) rootdir 投递文件必须被引用
    rd = os.path.join(MAIN, 'rootdir')
    board = sh('git show %s/BoardConfig.mk' % DEV).stdout
    bl = os.path.join(MAIN, 'BoardConfig.mk')
    if os.path.isfile(bl):
        board = open(bl).read()
    dm = re.search(r'DEVICE_MANIFEST_FILE\s*:=\s*(\S+)', board)
    for f in sorted(os.listdir(rd)):
        if f == 'manifest.xml':
            # 由 DEVICE_MANIFEST_FILE 消费，不要求 COPY 规则
            continue
        if f == '.gitignore':
            # 版本库卫生文件，本来就不该被 COPY 进镜像；当载荷要求它只会掩掉真错位
            continue
        referenced = re.search(re.escape(f), src) is not None
        report(bool(referenced), 'rootdir/%s 被 COPY 引用' % f,
               '' if referenced else '文件写了但没装进镜像 = 白写')

    if dm:
        tgt = dm.group(1).replace('$(DEVICE_PATH)', 'rootdir').replace('device/jiabo/z1', 'rootdir')
        exists = os.path.exists(os.path.join(MAIN, 'rootdir', 'manifest.xml'))
        report(exists, 'DEVICE_MANIFEST_FILE 指向的 manifest 存在', '%s -> rootdir/manifest.xml' % dm.group(1))
    else:
        report(False, 'DEVICE_MANIFEST_FILE 未设置', 'manifest 声明不会进 vintf')

    # C) 关键属性
    for prop in ('ro.hardware.gralloc', 'ro.hardware.hwcomposer'):
        hit = re.search(r'^[^\n]*%s=z1' % re.escape(prop), ml, re.M)
        report(bool(hit), 'mainline 分支有 %s=z1' % prop,
               '' if hit else '缺它则 allocator/composer 会去找 gralloc.mt6572.so 之类不存在的模块')

    # D) sepolicy 目录
    sp = re.search(r'BOARD_SEPOLICY_DIRS.*\$\((\w+)\)/(\S+)', src)
    sp_local = os.path.join(MAIN, 'sepolicy', 'vendor')
    report(os.path.isdir(sp_local) and bool(os.listdir(sp_local)),
           'sepolicy 目录有内容', sp_local if not (os.path.isdir(sp_local) and os.listdir(sp_local)) else
           (sp.group(0) if sp else '注意: 需构建机把 BOARD_SEPOLICY_DIRS 指到落地路径'))

    # E) BoardConfig 的分区/页大小必须等于原厂 scatter 实测值（不是抄来的数字）
    scat = os.path.join(ROOT, '1', 'MT6572_Android_scatter.txt')
    if os.path.exists(scat):
        raw = open(scat, encoding='utf-8', errors='replace').read()
        parts = {}
        for b in re.split(r'(?=- partition_index:)', raw):
            nm = re.search(r'partition_name:\s*(\S+)', b)
            sz = re.search(r'partition_size:\s*(0x[0-9A-Fa-f]+)', b)
            if nm and sz:
                parts[nm.group(1)] = int(sz.group(1), 16)
        want = [('BOARD_BOOTIMAGE_PARTITION_SIZE', parts.get('bootimg')),
                ('BOARD_SYSTEMIMAGE_PARTITION_SIZE', parts.get('android')),
                ('BOARD_CACHEIMAGE_PARTITION_SIZE', parts.get('cache')),
                ('BOARD_RECOVERYIMAGE_PARTITION_SIZE', parts.get('recovery'))]
        for var, val in want:
            m = re.search(var + r'\s*:=\s*(\d+)', board)
            if val is None or not m:
                report(False, '%s vs scatter' % var, 'scatter 无该分区或 BoardConfig 未设')
                continue
            got = int(m.group(1))
            report(got == val, '%s == scatter' % var, '%d vs %d' % (got, val))
        m = re.search(r'BOARD_KERNEL_PAGESIZE\s*:=\s*(\d+)', board)
        report(bool(m) and m.group(1) == '2048', '页大小 2048(与 mkbootimg --pagesize 一致)',
               m.group(1) if m else '未设')
        report(bool(re.search(r'TARGET_NO_KERNEL\s*:=\s*true', board)),
               'TARGET_NO_KERNEL(内核树外编译)', '')
        report(bool(re.search(r'^BOARD_KERNEL_CMDLINE\s*:=\s*$', board, re.M)),
               'BOARD_KERNEL_CMDLINE 留空(LK 不转发)', '非空会给人"改了 cmdline 就生效"的错觉')
    else:
        report(False, 'scatter 文件', '找不到 %s' % scat)

    print('\nDEVLAYER_FAIL=%d' % len(fails))
    for x in fails:
        print('  - ' + x)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
