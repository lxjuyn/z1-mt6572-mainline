#!/usr/bin/env python3
"""Offline validator for legacy MTK Android boot images."""
from __future__ import annotations
import argparse, gzip, hashlib, io, os, stat, struct, sys
ANDROID_MAGIC=b"ANDROID!"; MTK_MAGIC=0x58881688; MTK_HDR=512; DEFAULT_MAX=6291456; HDR=1632

def al(n,p): return (n+p-1)//p*p

def newc(blob):
    out=[]; pos=0
    while pos+110<=len(blob):
        h=blob[pos:pos+110]
        if h[:6]!=b"070701": raise ValueError(f"cpio bad magic at 0x{pos:x}")
        x=[int(h[i:i+8],16) for i in range(6,110,8)]
        mode,uid,gid,filesz,namelen=x[1],x[2],x[3],x[6],x[11]
        ns=pos+110; ne=ns+namelen
        if ne>len(blob): raise ValueError("cpio truncated name")
        name=blob[ns:ne].rstrip(b"\0").decode("utf-8","surrogateescape")
        ds=al(ne,4); de=ds+filesz
        if de>len(blob): raise ValueError(f"cpio truncated data for {name}")
        out.append((name,mode,uid,gid,filesz)); pos=al(de,4)
        if name=="TRAILER!!!": return out
    raise ValueError("cpio has no TRAILER!!!")

def check_cpio(raw, errors, notes, require_init_root):
    try: entries=newc(raw)
    except ValueError as e: errors.append(str(e)); return
    by={x[0]:x for x in entries}; init=by.get("init")
    if init is not None:
        _,mode,uid,gid,_=init
        if require_init_root and stat.S_IFMT(mode)!=stat.S_IFREG: errors.append("cpio /init is not a regular file")
        if require_init_root and stat.S_IMODE(mode)!=0o755: errors.append(f"cpio /init mode is {stat.S_IMODE(mode):04o}, expected 0755")
        if require_init_root and (uid!=0 or gid!=0): errors.append(f"cpio /init owner is {uid}:{gid}, expected 0:0")
    else: notes.append("cpio has no /init; ownership/mode check not applicable")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image")
    ap.add_argument("--max-size",type=int,default=DEFAULT_MAX)
    ap.add_argument("--dtb",help="optional DTB file; compare with separate or appended DTB")
    ap.add_argument("--require-ramdisk", action="store_true",
                    help="ramdisk 缺失/为 0/不是 gzip-cpio 时判 INVALID（超预算时 mkbootimg 会静默丢掉整个 ramdisk）")
    ap.add_argument("--require-init-root", action="store_true",
                    help="fail unless a cpio /init is root:root and mode 0755")
    args=ap.parse_args(); errors=[]; notes=[]
    try:
        with open(args.image,"rb") as f: d=f.read()
    except OSError as e: ap.error(str(e))
    n=len(d); print(f"image: {args.image}\nsize: {n} bytes (max {args.max_size})")
    if n>args.max_size: errors.append(f"image exceeds size limit by {n-args.max_size} bytes")
    if n< HDR or d[:8]!=ANDROID_MAGIC: errors.append("missing Android ANDROID! header")
    if errors and n<HDR: return report(errors,notes)
    vals=struct.unpack_from("<10I",d,8); ks,ka,rs,ra,ss,sa,ta,page,dt,unused=vals
    if page==0 or page&(page-1): errors.append(f"invalid non-power-of-two page size {page}")
    if page and HDR>page: errors.append(f"header size {HDR} exceeds page size {page}")
    if unused: errors.append(f"header unused field is nonzero: 0x{unused:x}")
    if page:
        starts=[page, page+al(ks,page), page+al(ks,page)+al(rs,page), page+al(ks,page)+al(rs,page)+al(ss,page)]
        end=starts[3]+al(dt,page)
        if end!=n: errors.append(f"layout ends at 0x{end:x}, file ends at 0x{n:x}")
        segs=[("kernel",starts[0],ks),("ramdisk",starts[1],rs),("second",starts[2],ss),("dtb",starts[3],dt)]
        for name,off,size in segs:
            if off+size>n: errors.append(f"{name} exceeds file")
            elif any(d[off+size:off+al(size,page)]): errors.append(f"{name} padding is not zero")
        print(f"page: {page}\nkernel: 0x{starts[0]:x}+{ks}\nramdisk: 0x{starts[1]:x}+{rs}\nsecond: 0x{starts[2]:x}+{ss}\ndtb: 0x{starts[3]:x}+{dt}")
        if starts[0]!=page: errors.append("kernel does not start after header page")
        for name,off,size in segs:
            if size and d[off:off+4]==struct.pack("<I",MTK_MAGIC):
                if size<MTK_HDR: errors.append(f"{name} MTK wrapper truncated")
                else:
                    wsize=struct.unpack_from("<I",d,off+4)[0]; wname=d[off+8:off+40].split(b"\0",1)[0]
                    expected=b"KERNEL" if name=="kernel" else b"ROOTFS" if name=="ramdisk" else None
                    if expected and wname!=expected: errors.append(f"{name} wrapper name {wname!r}, expected {expected!r}")
                    if wsize!=size-MTK_HDR: errors.append(f"{name} wrapper size {wsize}, expected {size-MTK_HDR}")
                    print(f"{name} MTK wrapper: {wname.decode('ascii','replace')} payload={wsize}")
        k=d[starts[0]:starts[0]+ks]; r=d[starts[1]:starts[1]+rs]
        if args.require_ramdisk:
            # 实测教训: 内容超 6MB 分区时 mkbootimg 会静默把整个 ramdisk 丢掉(rs=0)，
            # 产出的镜像仍能解出 kernel/dtb、以前会被判 VALID —— 上板表现是 VFS panic，
            # 很容易被误诊成内核回归。这里把它变成硬失败。
            if rs == 0: errors.append("ramdisk_size=0: 镜像里没有 initrd(多半是 kernel+dtb+ramdisk 超过分区上限被静默丢弃)")
            elif len(r) < MTK_HDR+64: errors.append(f"ramdisk 区域只有 {len(r)} B，不足以构成 MTK ROOTFS 头+载荷")
        payload=r[MTK_HDR:] if len(r)>=MTK_HDR and r[:4]==struct.pack("<I",MTK_MAGIC) else r
        if args.require_ramdisk and payload[:3]!=b"\x1f\x8b\x08" and payload[:6] not in (b"070701",b"070702"):
            errors.append("ROOTFS 载荷既不是 gzip 也不是 newc cpio —— initrd 不可用")
        if payload[:3]==b"\x1f\x8b\x08":
            try: raw=gzip.GzipFile(fileobj=io.BytesIO(payload)).read(); print(f"gzip initrd: {len(raw)} bytes"); check_cpio(raw,errors,notes,args.require_init_root)
            except (OSError,EOFError) as e: errors.append(f"gzip initrd invalid: {e}")
        elif payload[:6] in (b"070701",b"070702"): check_cpio(payload,errors,notes,args.require_init_root)
        else: notes.append("ROOTFS payload is neither gzip nor newc cpio; initrd checks skipped")
        app=[]
        for p in range(len(k) - 8, -1, -1):
            if k[p:p+4] != b"\xd0\x0d\xfe\xed":
                continue
            total=struct.unpack_from(">I", k, p+4)[0]
            if total >= 40 and p+total == len(k):
                app.append((p,total))
                break
        sep=d[starts[3]:starts[3]+dt] if dt else b""
        if app and dt:
            appended=k[app[0][0]:app[0][0]+app[0][1]]
            if app[0][1]!=dt or appended!=sep: errors.append("appended and separate DTBs differ")
            else: print(f"DTB: appended offset 0x{app[0][0]:x} matches separate DTB")
        elif app: notes.append(f"appended DTB found in kernel at offset 0x{app[0][0]:x}; no separate DTB")
        elif dt: notes.append("separate DTB present; no appended DTB magic found")
        if args.dtb:
            try:
                ext=open(args.dtb,"rb").read()
                if dt and ext!=sep: errors.append("external DTB differs from separate DTB")
                elif app and ext!=k[app[0][0]:app[0][0]+app[0][1]]: errors.append("external DTB differs from appended DTB")
            except OSError as e: errors.append(f"cannot read external DTB: {e}")
        h=hashlib.sha1(); h.update(k); h.update(struct.pack("<I",ks)); h.update(r); h.update(struct.pack("<I",rs)); h.update(b"" if not ss else d[starts[2]:starts[2]+ss]); h.update(struct.pack("<I",ss));
        if dt: h.update(sep); h.update(struct.pack("<I",dt))
        stored=d[0x240:0x240+20]; print(f"header id SHA1: stored={stored.hex()} computed={h.hexdigest()}")
        if stored!=h.digest(): errors.append("Android header id SHA1 mismatch")
    return report(errors,notes)

def report(errors,notes):
    for x in notes: print(f"NOTE: {x}")
    if errors:
        for x in errors: print(f"FAIL: {x}",file=sys.stderr)
        print("RESULT: INVALID",file=sys.stderr); return 1
    print("RESULT: VALID"); return 0
if __name__=="__main__": sys.exit(main())
