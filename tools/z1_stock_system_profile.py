#!/usr/bin/env python3
"""Disable unsupported remote HIDL servers in a NEW stock diagnostic ext4.

Preserve inode metadata and SELinux xattrs. Only edit an existing single-block
RC payload and its size; never operate on block devices. Keep native Binder
services intact. The staging files are updated to match for closure auditing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess

from z1_verify_init_closure import services


def command(image, text, write=False):
    result = subprocess.run(['debugfs']+(['-w'] if write else [])+
                            ['-R', text, str(image)], check=True,
                            capture_output=True, timeout=30)
    return result.stdout


def unsupported(service):
    return (service['name'] in ('hwservicemanager', 'vndservicemanager', 'hidl_memory', 'media.codec')
            or service['executable'].startswith('/vendor/bin/hw/')
            or service['executable'].startswith('/system/bin/hw/'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--system', type=Path, required=True)
    ap.add_argument('--tree', type=Path, required=True)
    ap.add_argument('--backup', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    if not stat.S_ISREG(args.system.stat().st_mode):
        raise ValueError('only regular new image files may be edited')
    if args.backup.exists():
        raise ValueError('backup directory already exists')
    args.backup.mkdir(parents=True)
    superblock = subprocess.run(['dumpe2fs', '-h', str(args.system)], check=True,
                               capture_output=True, text=True, timeout=30).stdout
    bs = re.search(r'^Block size:\s+(\d+)$', superblock, re.M)
    if not bs or int(bs.group(1)) != 4096 or 'metadata_csum' in superblock:
        raise ValueError('unsupported ext4 profile for preserving RC inodes')
    block_size = int(bs.group(1))
    changes = []
    candidates = sorted((args.tree/'etc/init').glob('*.rc'))
    candidates += sorted((args.tree/'vendor/etc/init').glob('*.rc'))
    for rc in candidates:
        relative = rc.relative_to(args.tree).as_posix()
        original = rc.read_bytes()
        parsed = services(original.decode(), relative)
        names = {s['name'] for s in parsed if unsupported(s)}
        if not names:
            continue
        result = []
        for line in original.decode().splitlines(keepends=True):
            result.append(line)
            if line.startswith('service ') and line.split()[1] in names:
                result.append('    disabled\n')
        modified = ''.join(result).encode()
        if len(modified) > block_size:
            raise ValueError('RC modification exceeds existing single block')
        stored = command(args.system, 'cat /'+relative)
        if stored != original:
            raise ValueError('image/staging RC mismatch: '+relative)
        blocks = command(args.system, 'blocks /'+relative).decode().split()
        if len(blocks) != 1 or not blocks[0].isdigit():
            raise ValueError('RC must occupy exactly one existing ext4 data block: '+relative)
        dest = args.backup/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(rc, dest)
        with args.system.open('r+b') as image:
            image.seek(int(blocks[0])*block_size)
            image.write(modified+b'\0'*(block_size-len(modified)))
        command(args.system, 'set_inode_field /'+relative+' size '+str(len(modified)), write=True)
        if command(args.system, 'cat /'+relative) != modified:
            raise ValueError('image RC edit verification failed: '+relative)
        rc.write_bytes(modified)
        changes.append(dict(path=relative, disabled_services=sorted(names),
                            before_sha256=hashlib.sha256(original).hexdigest(),
                            after_sha256=hashlib.sha256(modified).hexdigest()))
    if not any('hwservicemanager' in c['disabled_services'] for c in changes):
        raise ValueError('hardware manager RC was not found')
    report = dict(result='STATIC_STOCK_SERVICE_PROFILE_PASS', device_runtime_verified=False,
                  method='preserve allocated RC data blocks/inode ownership/mode/xattrs', changes=changes)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print('Disabled unsupported HIDL servers in', len(changes), 'RC files; native Binder services preserved')


if __name__ == '__main__':
    main()
