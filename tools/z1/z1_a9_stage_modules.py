#!/usr/bin/env python3
"""z1_a9_stage_modules.py —— 从**本轮内核构建产物**重算 A9 要随 rootfs 交付的 .ko 清单。

为什么必须重算而不是沿用 stock 那份：stock 分支的 network/modules.json 是按 3.4.67 时代的
配置写的，里面 `fuse`、`x_tables`、`ip_tables`、`ip6_tables`、`iptable_*`、`ip6t_*`、
`nf_reject_*` 都当模块装载；而 mainline 线上从 P3 起它们是 **内建**（A9 netd 开机就要用，
不能赌运行期 modprobe 顺序）。内建项不会产出 .ko，清单里留着它，构建机的装载步骤就会
去找一个不存在的文件（那份 JSON 还带 sha256 强校验，错了不会只是 warning）。

三类判定：
  KEPT     我们的树里有这个 .ko -> 保留，并用**我们的产物**重算 sha256
  BUILTIN  没有 .ko 但在 modules.builtin 里 -> 从清单删除（这是好事，netd 不需要等模块）
  MISSING  既没有 .ko 也不在内建表里 -> 真问题：说明该符号根本没开，必须报出来
"""
import hashlib, json, os, re, subprocess, sys

SELF = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(SELF, '..', '..'))
ROOT = os.path.normpath(os.path.join(WT, '..'))
BRANCH = 'origin/wip/android9-stock-s1'
SRC_JSON = BRANCH + ':android9/device/jiabo/z1/network/modules.json'
OUT = os.path.join(WT, 'android9', 'mainline', 'network', 'modules.json')
KREL = subprocess.run(['make', '-C', WT, '-s', 'ARCH=arm',
                       'CROSS_COMPILE=arm-linux-gnueabihf-', 'kernelrelease'],
                      capture_output=True, text=True).stdout.strip()


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def main():
    r = subprocess.run(['git', '-C', WT, 'show', SRC_JSON], capture_output=True, text=True)
    if r.returncode != 0:
        print('取不到 %s: %s' % (SRC_JSON, r.stderr.strip()[:200])); return 2
    stock = json.loads(r.stdout)['modules']

    # 我们树里的 .ko 索引（相对路径 -> 绝对路径）
    ours = {}
    for dirpath, _, files in os.walk(WT):
        if '/.git' in dirpath or dirpath.endswith('/usr'):
            continue
        for f in files:
            if f.endswith('.ko'):
                rel = os.path.relpath(os.path.join(dirpath, f), WT)
                ours[rel] = os.path.join(dirpath, f)
    by_name = {}
    for rel, ab in ours.items():
        by_name.setdefault(os.path.basename(rel), []).append((rel, ab))

    builtin_txt = os.path.join(WT, 'modules.builtin')
    builtin = set()
    if os.path.isfile(builtin_txt):
        builtin = {l.strip() for l in open(builtin_txt) if l.strip()}
    builtin_names = {os.path.basename(p) for p in builtin}

    kept, dropped_builtin, missing = [], [], []
    for e in stock:
        name, src = e.get('name'), e.get('source', '')
        fn = e.get('file') or (name.replace('-', '_') + '.ko')
        old_sha = e.get('sha256', '')
        cands = by_name.get(fn, [])
        # source 字段能对上就用它（同名 .ko 可能有多处，比如 usb 下）
        pick = None
        for rel, ab in cands:
            if src and rel == src:
                pick = (rel, ab); break
        if not pick and cands:
            pick = cands[0]
        if pick:
            rel, ab = pick
            new_sha = sha(ab)
            kept.append({'name': name, 'file': fn, 'source': rel,
                         'depends': e.get('depends', []),
                         'sha256': new_sha,
                         'sha256_changed_from_stock': bool(old_sha) and old_sha != new_sha})
            if rel in builtin:
                print('注意: %s 同时出现在 .ko 与 modules.builtin，按 .ko 处理' % name)
        elif fn in builtin_names or any(src and src in builtin for _ in [0]):
            dropped_builtin.append((name, fn, src or '(未给 source)'))
        else:
            missing.append((name, fn, src))

    kept_names = {k['name'] for k in kept}
    for k in kept:
        dangling = [d for d in k['depends'] if d not in kept_names]
        if dangling:
            # 依赖被删是因为它已成内建 —— 装载顺序上不再需要，标注清楚免得构建机以为漏了
            k['depends_note'] = '这些依赖已内建，无需装载: %s' % ', '.join(dangling)
            k['depends'] = [d for d in k['depends'] if d in kept_names]

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump({'generated_by': 'tools/z1/z1_a9_stage_modules.py',
               'kernelrelease': KREL,
               'source_manifest': SRC_JSON,
               'modules': kept},
              open(OUT, 'w'), ensure_ascii=False, indent=2)

    print('kernelrelease=%s' % KREL)
    print('stock 清单=%d 条 -> 保留 %d / 因内建删除 %d / 真缺失 %d'
          % (len(stock), len(kept), len(dropped_builtin), len(missing)))
    for n, fn, s in dropped_builtin:
        print('  [内建，已从清单删除] %-18s (%s)' % (n, fn))
    for n, fn, s in missing:
        print('  [MISSING 既非模块也非内建] %s (%s) 原 source=%s' % (n, fn, s))
    changed = sum(1 for k in kept if k['sha256_changed_from_stock'])
    print('sha256 与 stock 不同的保留项=%d（预期：本轮重编，哈希必须跟着换）' % changed)
    print('写出 %s' % OUT)
    print('MODULES_JSON_MISSING=%d' % len(missing))
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
