#!/usr/bin/env python3
"""Stage rebuilt Linux7 network modules and their dependency-ordered init action.

This operates on build files only. It never connects to or writes a device.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
KERNEL = ROOT / 'fork_linux'
DEVICE = ROOT / 'android9/device/jiabo/z1'


def info(path, field):
    return subprocess.check_output(['modinfo', '-F', field, str(path)], text=True).strip()


def main():
    release = (KERNEL / 'include/config/kernel.release').read_text().strip()
    if release != '7.0.0-rc7-z1+':
        raise ValueError(f'unexpected kernel release: {release}')
    paths = [(KERNEL / p).with_suffix('.ko') for p in
             (KERNEL / 'modules.order').read_text().splitlines()]
    by_name = {}
    for path in paths:
        name = path.stem.replace('-', '_')
        if name in by_name:
            raise ValueError(f'duplicate module name {name}')
        by_name[name] = path
    seeds = sorted(path.stem.replace('-', '_') for path in paths if
                   path.relative_to(KERNEL).as_posix().startswith((
                       'net/netfilter/', 'net/ipv4/netfilter/', 'net/ipv6/netfilter/',
                       'net/xfrm/', 'fs/fuse/')) or
                   path.relative_to(KERNEL).as_posix() == 'net/ipv6/ipv6.ko')
    required = {'ipv6', 'nfnetlink', 'nfnetlink_log', 'x_tables', 'ip_tables',
                'ip6_tables', 'xt_NFLOG', 'xt_quota2', 'xt_IDLETIMER',
                'fuse', 'xfrm_user', 'xt_policy'}
    if not required.issubset(by_name):
        raise ValueError(f'missing required modules: {required - by_name.keys()}')
    ordered, visiting, visited = [], set(), set()
    dependencies = {}

    def visit(name):
        if name in visited:
            return
        if name in visiting:
            raise ValueError(f'cyclic module dependency: {name}')
        if name not in by_name:
            raise ValueError(f'missing dependency: {name}')
        path = by_name[name]
        if not path.is_file() or info(path, 'vermagic').split()[0] != release:
            raise ValueError(f'module does not match rebuilt kernel: {path}')
        visiting.add(name)
        dependencies[name] = sorted(d.replace('-', '_') for d in
                                    info(path, 'depends').split(',') if d)
        for dep in dependencies[name]:
            visit(dep)
        visiting.remove(name)
        visited.add(name)
        ordered.append(name)

    for name in seeds:
        visit(name)
    if not required.issubset(visited):
        raise ValueError('required modules were not selected for loading')
    destination = DEVICE / 'network/modules'
    destination.mkdir(parents=True, exist_ok=True)
    expected = {by_name[name].name for name in ordered}
    if any(p.name not in expected for p in destination.glob('*.ko')):
        raise ValueError('staging contains stale modules; archive them before rerunning')
    records = []
    for name in ordered:
        path = by_name[name]
        shutil.copyfile(path, destination / path.name)
        records.append({'name': name, 'file': path.name,
                        'source': path.relative_to(KERNEL).as_posix(),
                        'depends': dependencies[name],
                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    rc = ('# Generated from rebuilt module dependencies; load after /system mounts.\n'
          '# netd starts later in class main. Keep this paired with its kernel.\n'
          'on post-fs\n' + ''.join(
              f'    insmod /system/lib/modules/z1-network/{r["file"]}\n' for r in records))
    (DEVICE / 'rootdir/init.z1.network.rc').write_text(rc)
    (DEVICE / 'network/modules.json').write_text(json.dumps(
        {'kernel_release': release, 'modules': records,
         'known_missing': ['Android qtaguid UID/tag accounting and inbound owner semantics']},
        indent=2) + '\n')
    print(f'Staged {len(records)} modules for {release}, in dependency order')


if __name__ == '__main__':
    main()
