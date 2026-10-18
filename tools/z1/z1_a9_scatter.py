#!/usr/bin/env python3
"""z1_a9_scatter.py —— 从原厂 scatter 生成"只放行指定分区"的 A9 刷机 scatter。

为什么需要：原厂 1/MT6572_Android_scatter.txt 里 preloader/mbr/ebr1/uboot(LK)/recovery/
sec_ro/logo/**usrdata** 的 is_download 都是 true，且 preloader 的 file_name 是**空值**——
直接点 Download 会拿空文件去刷引导链；Z1 唯一的原厂 LK 没了就没法回滚。
所以默认只放行 bootimg(+可选 android)，其余一律 is_download:false / file_name:NONE。

两个必须遵守的实现约束（都是我第一版踩出来的）：
 1) 原厂是 **CRLF** 行尾，读写都要 newline=''，否则整份行尾被换掉；
 2) scatter 的头部 `general: MTK_PLATFORM_CFG`(platform/project/storage/block_size) 是 SP Flash
    Tool 认文件的必需段，**不能只输出分区块**——第一版就是这么把 341 行写成 323 行，产物直接不可用。
    所以这里改成"整文件逐行原地改"，并断言行数、每块键序列都与原件一致。
"""
import argparse, os, re, sys

FNAME = {'bootimg': 'boot.img', 'android': 'system.img'}
PART_KEY = re.compile(r'^\s*(partition_name|is_download|file_name):')


def transform(raw, allow):
    lines = raw.splitlines(keepends=True)
    cur_part, flips, passed = None, [], []
    out = []
    in_general = True          # 第一个 "- partition_index:" 之前是头部 general 段
    for l in lines:
        m = re.match(r'^\s*partition_name:\s*(\S+)', l)
        if m:
            cur_part = m.group(1)
            in_general = False
            out.append(l)          # 这行必须原样输出；漏掉它会让 19 个分区块各少一行
            continue
        s = l.lstrip()
        if cur_part is not None and s.startswith('is_download:'):
            want = 'true' if cur_part in allow else 'false'
            if s.split(':', 1)[1].strip() == 'true' and want == 'false':
                flips.append(cur_part)
            eol = '\r\n' if l.endswith('\r\n') else '\n'
            out.append('  is_download: ' + want + eol)
            continue
        if cur_part is not None and s.startswith('file_name:'):
            eol = '\r\n' if l.endswith('\r\n') else '\n'
            if cur_part in allow:
                out.append('  file_name: ' + FNAME[cur_part] + eol)
                if cur_part not in passed:
                    passed.append(cur_part)
            else:
                out.append('  file_name: NONE' + eol)
            continue
        out.append(l)
    return ''.join(out), flips, passed, in_general


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--allow', default='bootimg', help='逗号分隔，如 bootimg,android')
    ap.add_argument('--system', action='store_true', help='等价于 allow 里追加 android')
    a = ap.parse_args()
    allow = {x for x in a.allow.split(',') if x}
    if a.system:
        allow.add('android')
    for n in allow:
        if n not in FNAME:
            print('不支持放行的分区名: %s（本工具只知道 %s）' % (n, sorted(FNAME))); return 2

    raw = open(a.src, encoding='utf-8', errors='replace', newline='').read()
    new, flips, passed, still_general = transform(raw, allow)

    # --- 结构自检：行数 / CRLF 数 / 每块键序列，三者都不许变 ---
    if new.count('\n') != raw.count('\n'):
        print('SCATTER-STRUCT-FAIL 行数 %d -> %d' % (raw.count('\n'), new.count('\n'))); return 1
    if new.count('\r\n') != raw.count('\r\n'):
        print('SCATTER-STRUCT-FAIL CRLF %d -> %d' % (raw.count('\r\n'), new.count('\r\n'))); return 1
    def keyseq(t):
        return re.findall(r'^\s{2}([A-Za-z_]+):', t, re.M)
    if keyseq(raw) != keyseq(new):
        diff = [(x, y) for x, y in zip(keyseq(raw), keyseq(new)) if x != y][:5]
        print('SCATTER-STRUCT-FAIL 键序列变化(前 5 处): %s' % diff); return 1
    if still_general:
        print('SCATTER-STRUCT-FAIL 没进入任何分区块（general 段之后没找到 - partition_index）'); return 1
    if sorted(passed) != sorted(allow):
        print('SCATTER-ALLOW-FAIL 期望放行 %s，实际 %s' % (sorted(allow), sorted(passed))); return 1

    open(a.dst, 'w', encoding='utf-8', newline='').write(new)
    print('分区块行数=%d(与原件一致)  CRLF 保真=%d/%d' % (new.count('\n'), new.count('\r\n'), raw.count('\r\n')))
    print('放行下载的分区: %s' % ', '.join(passed))
    print('已强制关闭下载的分区(%d 个): %s' % (len(flips), ' '.join(sorted(set(flips))) or '无'))
    for n in passed:
        p = os.path.join(os.path.dirname(os.path.abspath(a.dst)), FNAME[n])
        ok = os.path.exists(p)
        print('  %s -> %s : %s (%d B)' % (n, FNAME[n], '存在' if ok else '缺失!',
                                          os.path.getsize(p) if ok else -1))
        if not ok:
            print('  >>> 该文件不在 scatter 同目录，SP Flash Tool 会报找不到文件'); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
