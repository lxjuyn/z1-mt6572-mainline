# ready-to-paste: scripts/config --disable 命令清单 (仅 HIGH 安全度)

# 先跑这些，然后用 make olddefconfig 清理级联依赖

# === 文件系统大块 ===
scripts/config --disable CONFIG_NFS_FS
scripts/config --disable CONFIG_SUNRPC
scripts/config --disable CONFIG_SUNRPC_GSS
scripts/config --disable CONFIG_UBIFS_FS
scripts/config --disable CONFIG_NTFS3_FS
scripts/config --disable CONFIG_SQUASHFS

# === Tracing / Perf ===
scripts/config --disable CONFIG_TRACING
scripts/config --disable CONFIG_PERF_EVENTS
scripts/config --disable CONFIG_UPROBES

# === Security ===
scripts/config --disable CONFIG_SECURITYFS

# === 清理级联依赖 ===
# make olddefconfig  # 在运行完以上后执行，自动清理不再需要的选项
