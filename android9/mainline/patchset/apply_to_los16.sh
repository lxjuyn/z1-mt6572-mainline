#!/usr/bin/env bash
# apply_to_los16.sh —— 在持有 LineageOS-16(Pie) 源树的机器上，按 decisions.json
# 把 mainline 线需要的补丁状态对齐：撤销必须撤销的 stock 代偿、确认必须保留的补丁仍在。
#
# 为什么靠 sha256 而不是靠 grep：decisions.json 里的 base/result 两个值直接取自
# android9/stock_patches/manifest.json（当初逐份补丁记录下来的指纹），所以"这个文件到底是
# 打了补丁还是没打"是可以**判定**的，不需要人看 diff。三种状态：
#   == base    -> 补丁未施加（对 revert 是正确态；对 keep 是需要施加）
#   == result  -> 补丁已施加（对 keep 是正确态；对 revert 是需要撤销）
#   都不等     -> 未知态：文件被别的改动碰过，只能人工判，脚本绝不猜测
#
# 默认 dry-run（只报告，不写任何东西）；要真改加 --apply。
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${LOS16_TREE:-}
DEC=${DECISIONS:-$HERE/decisions.json}
MODE=dryrun
[ "${1:-}" = "--apply" ] && MODE=apply
[ -f "$DEC" ] || { echo "ERR: 找不到 $DEC"; exit 2; }
[ -n "$TREE" ] && [ -d "$TREE" ] || { echo "ERR: 需要 LOS16_TREE=<源树根>（本机没有该树，这是给 AOSP 构建机用的）"; exit 2; }

echo "模式=$MODE  源树=$TREE  判定表=$DEC"
python3 - "$TREE" "$DEC" "$MODE" <<'PY'
import hashlib, json, os, subprocess, sys
tree, dec, mode = sys.argv[1], sys.argv[2], sys.argv[3]
entries = json.load(open(dec, encoding='utf-8'))
def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()
stat = {'ok': 0, 'todo': 0, 'unknown': 0, 'missing': 0, 'applied': 0, 'fail': 0}
for e in entries:
    f, v = e['file'], e['verdict']
    path = os.path.join(tree, f)
    line = '%-7s %-52s' % (v, f)
    if not os.path.exists(path):
        print('%s MISSING  源树里没有该文件(分支/版本不对?)' % line); stat['missing'] += 1; continue
    cur = sha(path)
    if cur == e['base_sha256']:   state = 'UNPATCHED'
    elif cur == e['result_sha256']: state = 'PATCHED'
    else: state = 'UNKNOWN'
    patch = os.path.join(os.path.dirname(dec), e['patch'])
    if v == 'revert':
        if state == 'UNPATCHED':
            print('%s OK      未施加(正确)' % line); stat['ok'] += 1
        elif state == 'PATCHED':
            if mode == 'apply':
                r = subprocess.run(['patch', '-R', '-p1', '-i', patch, '-d', tree],
                                   capture_output=True, text=True)
                if r.returncode == 0 and sha(path) == e['base_sha256']:
                    print('%s REVERTED 已撤销并复核 sha==base' % line); stat['applied'] += 1
                else:
                    print('%s FAIL     撤销未成功 rc=%s 复核=%s | %s' % (line, r.returncode,
                          'sha!=base' if r.returncode == 0 else '-',
                          ((r.stdout or '') + (r.stderr or '')).strip().replace('\n', ' | ')[:160]))
                    stat['fail'] += 1
            else:
                print('%s TODO     需撤销(dry-run；跑 --apply 执行)' % line); stat['todo'] += 1
        else:
            print('%s UNKNOWN  文件被其它改动碰过，人工判' % line); stat['unknown'] += 1
    elif v == 'keep':
        if state == 'PATCHED':
            print('%s OK      已施加(正确)' % line); stat['ok'] += 1
        elif state == 'UNPATCHED':
            if mode == 'apply':
                r = subprocess.run(['patch', '-p1', '-i', patch, '-d', tree],
                                   capture_output=True, text=True)
                if r.returncode == 0 and sha(path) == e['result_sha256']:
                    print('%s APPLIED 已施加并复核 sha==result' % line); stat['applied'] += 1
                else:
                    print('%s FAIL     施加未成功 rc=%s 复核=%s | %s' % (line, r.returncode,
                          'sha!=result' if r.returncode == 0 else '-',
                          ((r.stdout or '') + (r.stderr or '')).strip().replace('\n', ' | ')[:160]))
                    stat['fail'] += 1
            else:
                print('%s TODO     需施加(dry-run)' % line); stat['todo'] += 1
        else:
            print('%s UNKNOWN  人工判' % line); stat['unknown'] += 1
    else:  # rework
        print('%s MANUAL   %s' % (line, e['why'])); stat['todo'] += 1
print('\n汇总: ' + '  '.join('%s=%d' % (k, n) for k, n in stat.items()))
bad = stat['unknown'] + stat['missing'] + stat['fail']
print('APPLY_FAIL=%d  (=unknown+missing+fail；dry-run 下 todo 不算失败)' % bad)
sys.exit(1 if bad else 0)
PY
rc=$?
[ "$MODE" = dryrun ] && echo "(dry-run：未改动源树。确认无误后加 --apply)"
exit $rc
