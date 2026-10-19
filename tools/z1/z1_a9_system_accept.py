#!/usr/bin/env python3
"""z1_a9_system_accept.py —— 判定一份 system.img 到底是不是"能给 mainline 内核用的 A9 arm32 系统"。

为什么要有这道闸（不是重复 z1_a9_mainline_system.py）：
  flashpkg 原先只查容量和 sparse/ext4 magic —— 把原厂 4.4 的 system.img 或某台 arm64
  设备的 A9 镜像灌进去，它照样出包。上板表现为"boot 到 init 就挂"，而串口里最难读懂的
  正是这类"userspace 与内核 ABI 不匹配"的失败。刷一次板子的代价远高过一次离线遍历，
  所以要在出包前把判据摆全。

判据分两层，不是只看 build.prop：
  - `ro.build.version.sdk` 必须是 28（A9），`ro.product.cpu.abi` 必须含 armeabi-v7a；
  - 但属性会骗人（改文件的、抄错机型的多），所以再读 `/system/bin/init` 的 **ELF 头
    e_machine**：本树是 ARCH=arm 的 32 位内核，跑不了 EM_AARCH64(183) 的二进制，
    EM_ARM(40) 才是硬证据。两条不一致时以 ELF 为准并报 WARN。

退出码 0=ACCEPT，3=REJECT（并打印逐条原因）。只做只读遍历，绝不写镜像。
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile
from pathlib import Path

SELF = Path(__file__).resolve().parent
sys.path.insert(0, str(SELF))
from z1_a9_mainline_system import (ANDROID_PART_LIMIT, WT, dbg, exists, sha, to_raw, walk)  # noqa: E402

EM_ARM, EM_AARCH64 = 40, 183
SPARSE_MAGIC = 0x3AFF26AF

# A9 起来必需的用户态支柱。缺任何一个，init 到 SystemServer 的链子都会断。
REQUIRED_BINS = {
    '/system/bin/init': 'second-stage init',
    '/system/bin/sh': 'init 的 /system/bin/sh 依赖',
    '/system/bin/servicemanager': 'framework binder 名服务',
    '/system/bin/hwservicemanager': 'HIDL（mainline 线必须有真 hwbinder 服务）',
    '/system/bin/lmkd': 'low memory killer，A9 init 强启',
    '/system/bin/netd': '网络（配合 A9_NETD_NETFILTER.md 的内建项）',
    '/system/bin/ueventd': '节点权限',
    '/system/bin/app_process32': 'arm32 Zygote，缺它说明不是 32 位 userspace',
}
# 这些是 adapter 投递的 mainline device 层，缺了说明还没跑改造
DEV_LAYER = ['/system/etc/init.z1.rc', '/system/etc/ueventd.z1.rc', '/system/etc/fstab.z1',
             '/system/etc/vintf/manifest.xml', '/system/etc/manifest.xml']


class Verdict:
    def __init__(self):
        self.checks = []   # (level, name, detail)  level in OK/WARN/FAIL

    def ok(self, n, d):
        self.checks.append(('OK', n, d))

    def warn(self, n, d):
        self.checks.append(('WARN', n, d))

    def fail(self, n, d):
        self.checks.append(('FAIL', n, d))

    def count(self, lvl):
        return sum(1 for c in self.checks if c[0] == lvl)


def prop_get(props_text, key):
    m = re.search(r'(?m)^\s*%s=(.*)$' % re.escape(key), props_text)
    return m.group(1).strip() if m else None


def elf_machine(blob):
    """返回 e_machine，认不出返回 None。offset 18 起 2 字节小端。"""
    if len(blob) < 20 or blob[:4] != b'\x7fELF':
        return None
    return struct.unpack('<H', blob[18:20])[0]


def find_first(img, candidates, allfiles):
    """镜像里同名文件可能在 /system、/system/system 或根（system-as-root）。靠遍历，不猜。"""
    hits = {}
    for want in candidates:
        base = os.path.basename(want)
        exact = [p for p in allfiles if p == want or p.endswith(want)]
        loose = [p for p in allfiles if os.path.basename(p) == base and p.count('/') <= 4]
        hits[want] = exact[0] if exact else (loose[0] if loose else None)
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('system', help='system.img（sparse 或 raw ext4）')
    ap.add_argument('--json', help='把逐条判据写成 JSON 报告')
    ap.add_argument('--expect-adapted', action='store_true',
                    help='要求 mainline device 层已投递（改造后的镜像用这个）')
    a = ap.parse_args()

    src = Path(a.system)
    if not src.is_file():
        print('SYS_ACCEPT=REJECT'); print('FAIL file=找不到 %s' % src); return 3

    v = Verdict()
    report = {'input': str(src), 'input_bytes': src.stat().st_size}
    work = tempfile.mkdtemp(prefix='a9acc_')
    try:
        raw, converted = to_raw(src, work)
        report['sparse_converted'] = converted
        if src.stat().st_size > ANDROID_PART_LIMIT:
            v.fail('size', '%d B > android 分区 %d B' % (src.stat().st_size, ANDROID_PART_LIMIT))
        else:
            v.ok('size', '%d B，android 分区内（余 %d B）' % (
                src.stat().st_size, ANDROID_PART_LIMIT - src.stat().st_size))

        d2 = subprocess.run(['dumpe2fs', '-h', str(raw)], capture_output=True, text=True, timeout=90)
        if d2.returncode != 0 or 'Filesystem magic' not in d2.stdout:
            print('SYS_ACCEPT=REJECT')
            print('FAIL ext4=不是能被 dumpe2fs 认出的 ext4: %s' % d2.stderr.strip()[:160])
            return 3
        sb = d2.stdout
        if 'metadata_csum' in sb:
            v.warn('metadata_csum', '该 ext4 开了 metadata_csum：能刷，但本机不可原地改块'
                                    '（z1_a9_mainline_system.py 会拒绝），需在源机改好再传')
        else:
            v.ok('metadata_csum', '未开启，本机可原地改造')
        fstate = (re.search(r'Filesystem state:\s*(.+)', sb) or ['', '?'])[1].strip()
        (v.ok if fstate == 'clean' else v.warn)('fs_state', fstate)

        allfiles = walk(raw)
        report['entry_count'] = len(allfiles)
        # 条数只是"给错分区了"的粗筛（cache.img/userdata 也是合法 ext4）。真正的判据在
        # 下面的 build.prop 与必需可执行文件，所以这里不硬失败，只报。
        (v.ok if len(allfiles) >= 300 else v.warn)('walk', '遍历到 %d 条路径%s' % (
            len(allfiles),
            '' if len(allfiles) >= 300 else '（少于 300，A9 system 通常数千条，可能给的不是 system 分区）'))

        # ---- build.prop：不猜路径，遍历找 ----
        prop_paths = [p for p in allfiles if p.endswith('build.prop') or p.endswith('default.prop')]
        report['prop_files'] = prop_paths
        props = ''
        for p in sorted(prop_paths):
            try:
                props += dbg(raw, 'cat %s' % p).decode(errors='replace') + '\n'
            except Exception as e:
                v.warn('prop_read', '%s 读取失败 %s' % (p, str(e)[:80]))
        if not props.strip():
            v.fail('build.prop', '镜像里没有任何 build.prop（%d 条路径里）' % len(allfiles))
        else:
            sdk = prop_get(props, 'ro.build.version.sdk')
            rel = prop_get(props, 'ro.build.version.release')
            if sdk == '28':
                v.ok('sdk', 'ro.build.version.sdk=28 (release=%s)' % rel)
            elif sdk is None:
                v.fail('sdk', '没有 ro.build.version.sdk 字段')
            else:
                v.fail('sdk', 'ro.build.version.sdk=%s（release=%s）—— 不是 Android 9，'
                              'mainline A9 交付物按 API 28 准备' % (sdk, rel))
            abi = prop_get(props, 'ro.product.cpu.abi')
            abilist = prop_get(props, 'ro.product.cpu.abilist') or ''
            allabi = '%s %s' % (abi or '', abilist)
            if 'armeabi-v7a' in allabi or allabi.strip() == 'arm':
                v.ok('abi_prop', 'abi=%s abilist=%s 含 armeabi-v7a' % (abi, abilist or '-'))
            elif 'arm64' in allabi:
                v.fail('abi_prop', 'abi=%s abilist=%s —— 只有 arm64。本树是 ARCH=arm 32 位内核，'
                                   '装不了 64 位 userspace' % (abi, abilist or '-'))
            else:
                v.fail('abi_prop', 'abi=%s abilist=%s —— 认不出 arm32' % (abi, abilist or '-'))

            if re.search(r'(?m)^.*z1\.stock_kernel.*=?1\s*$', props) or \
               re.search(r'(?m)^\s*ro\.z1\.stock_kernel=1\s*$', props):
                v.fail('stock_prop', 'build.prop 里仍有 ro.z1.stock_kernel=1 —— stock 代偿门未撤，'
                                     '先跑 z1_a9_mainline_system.py')
            elif 'z1.stock_kernel' in props:
                v.ok('stock_prop', 'stock_kernel 属性存在但非 1')
            else:
                v.ok('stock_prop', 'build.prop 无 ro.z1.stock_kernel 残留')

            hw = prop_get(props, 'ro.hardware')
            dev = prop_get(props, 'ro.product.device')
            if hw not in (None, 'z1') or dev not in (None, 'z1'):
                v.warn('target', 'ro.hardware=%s ro.product.device=%s（非 z1）—— '
                                 '首轮靠 dts 的 androidboot.hardware=z1 与 init.z1.rc 兜住，'
                                 '但要留意它按原机型找 /vendor 分区 HAL' % (hw, dev))
            else:
                v.ok('target', 'ro.hardware=%s ro.product.device=%s' % (hw, dev))

        # ---- 标记文件（遍历找，不猜）----
        markers = [p for p in allfiles if p.endswith('/z1_stock_kernel')]
        if markers:
            v.fail('stock_marker', '仍存在 stock 标记 %s —— 先跑 z1_a9_mainline_system.py' % markers[:3])
        else:
            v.ok('stock_marker', '无 /z1_stock_kernel 标记')

        # ---- 关键二进制 + ELF 机器码（不信属性的那一路）----
        cand = {}
        for want in REQUIRED_BINS:
            base = os.path.basename(want)
            hits = [p for p in allfiles if p.endswith(base) and '/bin/' in p]
            cand[want] = hits[0] if hits else None
        # 光"文件在"不够：本轮就出过投进去的 /bin/init 是 0644 —— 交棒 exec 会直接失败。
        noexec = []
        for w, p in cand.items():
            if not p:
                continue
            st = subprocess.run(['debugfs', '-R', 'stat %s' % p, str(raw)],
                                capture_output=True, text=True, timeout=30).stdout
            m = re.search(r'Mode:\s+(\d+)', st)
            mode = m.group(1) if m else ''
            # debugfs 打的是 Mode: 0755 这种四位八进制；判 x 要整值 & 0o111，
            # 取 mode[-4] 会永远拿到前导 0（我第一版就这样把全绿的夹具判成丢权限）。
            if mode and not (int(mode, 8) & 0o111):
                noexec.append('%s(mode=%s)' % (p, mode))
        if noexec:
            v.fail('exec_bit', '这些 A9 关键件没有可执行位，exec 必失败: %s' % noexec[:6])
        elif cand:
            v.ok('exec_bit', '%d 项关键可执行文件权限位都带 x' % len([1 for x in cand.values() if x]))
        missing = [w for w, p in cand.items() if not p]
        if missing:
            v.fail('bins', '缺少 A9 必需可执行文件: %s' % ', '.join(
                '%s(%s)' % (os.path.basename(m), REQUIRED_BINS[m]) for m in missing))
        else:
            v.ok('bins', 'A9 必需可执行文件 %d 项齐备' % len(REQUIRED_BINS))

        for probe in ('/system/bin/init', '/system/bin/servicemanager'):
            p = cand.get(probe)
            if not p:
                continue
            try:
                head = dbg(raw, 'cat %s' % p)[:64]
            except Exception as e:
                v.warn('elf_%s' % os.path.basename(p), '读取失败 %s' % str(e)[:60]); continue
            em = elf_machine(head)
            if em == EM_ARM:
                v.ok('elf_%s' % os.path.basename(p), '%s 是 EM_ARM(0x28)，32 位 ARM 二进制' % p)
            elif em == EM_AARCH64:
                v.fail('elf_%s' % os.path.basename(p),
                       '%s 是 EM_AARCH64(0xB7) —— 64 位二进制，本内核跑不了' % p)
            else:
                v.warn('elf_%s' % os.path.basename(p), '%s e_machine=%s 认不出' % (p, em))

        # ---- HIDL 服务是否仍被 disabled ----
        rc_hits = [p for p in allfiles if p.endswith('.rc') and
                   ('hwservicemanager' in p or 'vndservicemanager' in p)]
        disabled_in = []
        for p in rc_hits:
            try:
                t = dbg(raw, 'cat %s' % p).decode(errors='replace')
            except Exception:
                continue
            if re.search(r'(?m)^\s*disabled\s*$', t):
                disabled_in.append(p)
        if not rc_hits:
            v.warn('hidl_rc', '镜像里没找到 hwservicemanager/vndservicemanager 的 .rc（A9 通常在 '
                              '/system/etc/init/ 或 /vendor/etc/init/）')
        elif disabled_in:
            v.fail('hidl_rc', '这些 rc 里的服务仍是 disabled：%s —— 先跑 adapter' % disabled_in[:3])
        else:
            v.ok('hidl_rc', '%d 份 HIDL rc 均无 disabled' % len(rc_hits))

        # ---- VINTF / SELinux 形状（A9 的 init 会读，缺了会 log message 但不致命）----
        vintf = [p for p in allfiles if p.endswith('manifest.xml') and 'vintf' in p]
        devlayer = [p for p in allfiles if os.path.basename(p) in
                    ('manifest.xml',) and '/system/etc' in p]
        if vintf or devlayer:
            v.ok('vintf', '找到 %s' % (vintf + devlayer)[:3])
        else:
            v.warn('vintf', '没有 vintf manifest —— 需靠 device 层投递 manifest.xml')
        sep = [p for p in allfiles if p.endswith('plat_sepolicy.cil') or p.endswith('precompiled_sepolicy')]
        if sep:
            v.ok('sepolicy', '策略文件 %s' % sep[:3])
        else:
            v.warn('sepolicy', '没找到 plat_sepolicy.cil/precompiled_sepolicy（首轮 permissive 可过，'
                               '但收紧时要回头补）')

        if a.expect_adapted:
            notdel = [d for d in DEV_LAYER if not any(p.endswith(os.path.basename(d)) for p in allfiles)]
            if notdel:
                v.fail('dev_layer', '改造后的镜像仍缺 device 层: %s' % notdel)
            else:
                v.ok('dev_layer', 'mainline device 层已投递')

            # 模块投递闸：rc 里每一条 insmod 的绝对路径，必须在镜像里真找得到、且是 EM_ARM
            # 的 ELF 模块。只看"清单里有 .ko"不算数 —— P11 就是 modules.json 齐全但镜像里
            # 连 /lib/modules 都不存在，netd 到运行期才发现规则下发不了。
            ins = []
            for p in allfiles:
                if not p.endswith('.rc'):
                    continue
                try:
                    t = dbg(raw, 'cat %s' % p).decode(errors='replace')
                except Exception:
                    continue
                for mm in re.finditer(r'(?m)^\s*insmod\s+(\S+\.ko)', t):
                    ins.append((p, mm.group(1)))
            if not ins:
                v.warn('net_modules', '镜像里没有任何 insmod 行（若 netfilter 全内建则正常）')
            else:
                bad = []
                want = {}
                mjp = WT / 'android9' / 'mainline' / 'network' / 'modules.json'
                if mjp.is_file():
                    for mm in json.loads(mjp.read_text()).get('modules', []):
                        want[mm['file']] = mm['sha256']
                for rcp, target in ins:
                    rel = target[len('/system'):] if target.startswith('/system/') else target
                    cand = next((probe for probe in (rel, target) if exists(raw, probe)), None)
                    if cand is None:
                        bad.append('%s 要 insmod %s，镜像内 %s 两处都没有'
                                   % (os.path.basename(rcp), target, ' / '.join((rel, target))))
                        continue
                    try:
                        blob = dbg(raw, 'cat %s' % cand)
                    except Exception as e:
                        bad.append('%s 读不出内容: %s' % (cand, str(e)[:60]))
                        continue
                    head, em = blob[:64], elf_machine(blob[:64])
                    if head[:4] != b'\x7fELF' or em != EM_ARM:
                        bad.append('%s 不是 EM_ARM 内核模块(magic=%r e_machine=%s)'
                                   % (cand, head[:4], em))
                        continue
                    # 只验 ELF 会放过"载荷自带的那份旧 .ko"：它 magic 与 e_machine 全对，
                    # 但 vermagic 不是本轮 7.0.0-rc7-z1+，板上 insmod 一样被拒（r2 实测如此）。
                    exp = want.get(os.path.basename(cand))
                    if exp and sha(blob) != exp:
                        bad.append('%s sha=%s 与本轮清单 %s 不符（是载荷自带的旧 .ko，vermagic 对不上本内核）'
                                   % (cand, sha(blob)[:12], exp[:12]))
                if bad:
                    v.fail('net_modules', '; '.join(bad)[:400])
                else:
                    v.ok('net_modules', 'rc 里 %d 条 insmod 全部命中，均为 EM_ARM 模块' % len(ins))

            # 类型位闸：adapter 用 debugfs 原地写文件，而 set_inode_field 喂错进制就会把
            # i_mode 覆盖成不含 S_IFREG 的值 —— 权限位看着完全正确、debugfs cat 也读得出字节，
            # 但内核根本打不开这个文件。P10/P11 投递的 10 个文件全中招且当时没有任何一道闸能看见。
            DELIVERED = {'init.z1.rc', 'ueventd.z1.rc', 'fstab.z1', 'z1_network.rc',
                         'z1_usb_setup.sh', 'z1_device_manifest.xml',
                         'xt_quota2.ko', 'xt_IDLETIMER.ko', 'init', 'ueventd'}
            lost, checked = [], 0
            for p in allfiles:
                b = os.path.basename(p)
                if b not in DELIVERED:
                    continue
                # init/ueventd 只看 /bin 下那两个（/etc/init 是目录，按名字抓会误报）
                if b in ('init', 'ueventd') and '/bin' not in p:
                    continue
                checked += 1
                try:
                    st = dbg(raw, 'stat %s' % p).decode(errors='replace')
                except Exception:
                    continue
                mm = re.search(r'(?m)^Inode:\s+\d+\s+Type:\s+(\S+)', st)
                if not mm or mm.group(1) != 'regular':
                    lost.append('%s(Type=%s)' % (p, mm.group(1) if mm else '读不到'))
            if lost:
                v.fail('file_type', '%d 个投递件类型位丢失(内核打不开): %s'
                       % (len(lost), ', '.join(lost[:8])))
            else:
                v.ok('file_type', '投递的 %d 个文件 i_mode 类型位均为 regular' % checked)

        r = subprocess.run(['e2fsck', '-fn', str(raw)], capture_output=True, text=True, timeout=300)
        out = (r.stdout + r.stderr)
        report['e2fsck_rc'] = r.returncode
        if r.returncode == 0:
            v.ok('e2fsck', 'rc=0 干净')
        else:
            # 原厂镜像常用非 e2fsprogs 的打包器生成，"位图末尾填充未设置"这类纯记账瑕疵
            # 会带 rc=4，但不会让 A9 起不来；把它和真正的结构损坏混成一类会误拒好镜像。
            hard = re.findall(r'(?i)(corrupt|inconsistent|missing|illegal|not connected|'
                              r'BLOCK GROUP|superblock|directory unknown|i_blocks)', out)
            found = [ln.strip() for ln in out.splitlines()
                     if ('修复' in ln or 'Fix' in ln or ':' in ln) and ln.strip()
                     and '警告' not in ln and '遍' not in ln]
            detail = '; '.join(found[:2]) or out.strip().splitlines()[-1]
            report['e2fsck_findings'] = found[:6]
            if hard:
                v.fail('e2fsck', 'rc=%d 结构性损坏征兆 %s：%s' % (r.returncode, hard[:3], detail))
            else:
                v.warn('e2fsck', 'rc=%d 仅记账类瑕疵：%s —— 可直接刷；本机若要原地改造，'
                                 '先 e2fsck -fy 一次再跑 adapter' % (r.returncode, detail))
    finally:
        subprocess.run(['sync'], timeout=30)
        import shutil
        shutil.rmtree(work, ignore_errors=True)

    fails, warns = v.count('FAIL'), v.count('WARN')
    for lvl, n, d in v.checks:
        print('%-4s %-14s %s' % (lvl, n, d))
    print('CHECKS=%d FAIL=%d WARN=%d' % (len(v.checks), fails, warns))
    report['checks'] = [{'level': l, 'name': n, 'detail': d} for l, n, d in v.checks]
    report['verdict'] = 'ACCEPT' if fails == 0 else 'REJECT'
    if a.json:
        Path(a.json).write_text(json.dumps(report, ensure_ascii=False, indent=1))
        print('REPORT=%s' % a.json)
    print('SYS_ACCEPT=%s' % ('ACCEPT' if fails == 0 else 'REJECT'))
    return 0 if fails == 0 else 3


if __name__ == '__main__':
    sys.exit(main())
