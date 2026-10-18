#!/usr/bin/env python3
"""z1_a9_gen_network_rc.py —— 用本仓 network/modules.json 重生成 init.z1.network.rc。

为什么必须重生成：stock 分支那份对 41 个 .ko 逐个 `insmod`，那是按 3.4.67 的配置写的。
mainline 线上从 P3/P7/P8 起，其中 39 个已是**内建**（netd 一开机就要用，不能赌运行期装载），
只有 xt_quota2/xt_IDLETIMER 还是模块。照原样发出去 = 39 行 `No such file or directory`，
还会诱使后人"干脆把所有 insmod 删掉"——那会把真正需要的两个也删掉，netd 下发
`-m quota2` / `-j IDLETIMER` 时直接失败。

顺序：按 modules.json 里保留的 depends 拓扑序输出；全部放在 `on post-fs`，
早于 class main（netd 在 main 里起）。
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(HERE, '..', '..'))
MJ = os.path.join(WT, 'android9', 'mainline', 'network', 'modules.json')
OUT = os.path.join(WT, 'android9', 'mainline', 'rootdir', 'init.z1.network.rc')
DEST = '/system/lib/modules/z1-network'


def order(mods):
    by = {m['name']: m for m in mods}
    out, seen, temp = [], set(), set()

    def visit(n):
        if n in seen:
            return
        if n in temp:
            print('注意: 依赖环 %s，按出现顺序打破' % n)
            return
        temp.add(n)
        for d in by.get(n, {}).get('depends', []):
            if d in by:
                visit(d)
        temp.discard(n)
        seen.add(n)
        out.append(n)
    for m in mods:
        visit(m['name'])
    return [by[n] for n in out]


def main():
    if not os.path.isfile(MJ):
        print('先跑 tools/z1/z1_a9_stage_modules.py 生成 %s' % MJ); return 2
    d = json.load(open(MJ))
    mods = order(d['modules'])
    lines = [
        '# Z1 mainline(7.0-rc7) 网络模块装载 —— 由 tools/z1/z1_a9_gen_network_rc.py 从',
        '# android9/mainline/network/modules.json 生成，**不要手工加回 stock 那 41 条**：',
        '# 其中绝大多数在 mainline 上已是内建(netd 开机即用，不能赌运行期装载顺序)，',
        '# 只有这里列出的仍是模块。',
        '# 本轮构建: kernelrelease=%s；条目 %d 个。' % (d.get('kernelrelease', '?'), len(mods)),
        '',
        'on post-fs',
    ]
    for m in mods:
        lines.append('    insmod %s/%s' % (DEST, m['file']))
    lines += [
        '',
        '# 装载结果自查：netd 起来前把已加载模块数写进 kmsg，失败时一眼能看出是',
        '# "模块没装"还是"装了但规则下发失败"（后者是另一类问题，见 A9_NETD_NETFILTER.md）。',
        '    exec - root root -- /system/bin/sh -c \'echo "[z1net] loaded=$(ls /sys/module | grep -cE \\"xt_quota2|xt_IDLETIMER\\")" > /dev/kmsg\'',
        '',
    ]
    open(OUT, 'w').write('\n'.join(lines))
    print('写出 %s：insmod %d 条（stock 原来 41 条）' % (OUT, len(mods)))
    for m in mods:
        print('  %-16s %s  depends=[%s]' % (m['name'], m['file'], ', '.join(m.get('depends', []))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
