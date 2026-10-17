# fs / trace / security / namespace CONFIG 精简分析清单

## 背景
- 当前 zImage: 8.5 MB (arm32)
- 目标: < 4.96 MB 以装入 6MB bootimg 分区 (配 ~1MB ramdisk + DTB)
- 已有分析: clk/reg/mfd/crypto/thermal/cpufreq/leds (不重复)
- 本报告范围: filesystem / tracing / security / cgroup-perf / BPF / namespace / misc FS

---

## HIGH 优先级 (建议直接关)

### 1. CONFIG_NFS_FS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~300-500 KB (sunrpc + nfs client 代码段 + GSS crypto) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | CONFIG_SUNRPC=y (default y if NFS_FS=y)，关闭 NFS_FS 后 SUNRPC 自动变 n |
| 理由 | Z1 用 ramdisk 启动，不挂载 NFS。第一阶段(13键+USB ACM)完全不需要网络文件系统 |

### 2. CONFIG_SUNRPC (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~200-400 KB (source dir 5.1M, 大部分编译后代码段) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 仅由 NFS_FS/NFSD 的 "default y if..." 触发。关闭 NFS_FS 后自动变 n |
| 理由 | NFS 的 RPC 基础协议栈，NFS 关了就不需要 |

### 3. CONFIG_SUNRPC_GSS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~50-100 KB (GSS 认证支持) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | SUNRPC 的子选项 |
| 理由 | NFS 的 Kerberos 认证，不用 NFS 就不需要 |

### 4. CONFIG_UBIFS_FS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~150-300 KB (source dir 3.4M) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 选 CRC16、CRC32、CRYPTO(如果选了 advanced compr)、CRYPTO_LZO、CRYPTO_DEFLATE、CRYPTO_ZSTD。关掉后这些 crypto 子选项自动变 n |
| 理由 | Z1 是 eMMC SD card boot，无 NAND flash，不需要 UBIFS |

### 5. CONFIG_NTFS3_FS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~100-200 KB (source dir 2.2M) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 选 BUFFER_HEAD、NLS、LEGACY_DIRECT_IO、FS_POSIX_ACL(CONFIG_EXT4_FS_POSIX_ACL 已 n) |
| 理由 | ramdisk 和 SD card 用 FAT/ext4，不需要 NTFS |

### 6. CONFIG_SQUASHFS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~80-150 KB (source dir 1.3M, 但 squashfs 代码段相对小) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 选 SQUASHFS_DECOMP_SINGLE/MULTI/MULTI_PERCPU |
| 理由 | Z1 不挂载 squashfs 镜像，ramdisk 是 cpio 格式 |

### 7. CONFIG_TRACING (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~400-700 KB (kernel/trace source dir 5.1M, 编译后代码段占比约 10-15%) |
| 安全度 | HIGH - 无风险 (但注意依赖链) |
| 依赖/断链 | **核心依赖**：TRACING 选中 (select) 了 RING_BUFFER、STACKTRACE(if STACKTRACE_SUPPORT)、TRACEPOINTS、NOP_TRACER、BINARY_PRINTF、EVENT_TRACING、TRACE_CLOCK、NEED_TASKS_RCU。关闭 TRACING 将级联关闭所有 trace 子选项。 |
| 理由 | 上板调试不需要 ftrace/tracepoints。关闭后 kernel/trace/ 下所有文件不编译 |

### 8. CONFIG_CONTEXT_SWITCH_TRACER (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~5-10 KB (已被 TRACING->EVENT_TRACING->CONTEXT_SWITCH_TRACER 链选中，关 TRACING 后自动消失) |
| 安全度 | HIGH |
| 依赖/断链 | 被 EVENT_TRACING 选中，EVENT_TRACING 被 TRACING 选中 |
| 理由 | 上下文切换追踪，调试用，生产/验证不需要 |

### 9. CONFIG_PERF_EVENTS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~100-200 KB (含 perf core + trace_event_perf + driver/perf 代码) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | UPROBE_EVENTS 和 trace_event_perf.o 依赖它。driverrs/perf/Kconfig: depends on PERF_EVENTS。关 PERF_EVENTS 后 UPROBE_EVENTS 自然变 n |
| 理由 | perf 性能分析，上板验证阶段不需要 |

### 10. CONFIG_UPROBES (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~50-100 KB |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 选 TASKS_TRACE_RCU。关闭后 TASKS_TRACE_RCU 可能变 n (如果没有其他选择者) |
| 理由 | 用户空间探针，不需要 |

### 11. CONFIG_SECURITYFS (y) -> n
| 项目 | 详情 |
|------|------|
| 省约 | ~10-20 KB (security/inode.c 文件小) |
| 安全度 | HIGH - 无风险 |
| 依赖/断链 | 可能被 ima/safesetid/tomoyo/apparmor/efi_secret 选中，但这些当前都是 n。当前是直接设置为 y |
| 理由 | /sys/fs/security 文件系统，SELinux/APPARMOR 等 LSM 模块依赖，Z1 不需要 |

### 12. CONFIG_AUDIT / CONFIG_AUDITSYSCALL (已 n) -> 已关
| 项目 | 详情 |
|------|------|
| 省约 | 0 (已关闭) |
| 理由 | 已确认关闭 |

---

## MEDIUM 优先级 (视情况关)

### 13. CONFIG_CGROUP_PERF (n) -> 已关
| 项目 | 详情 |
|------|------|
| 省约 | 0 (已关闭) |
| 理由 | 已确认关闭 |

### 14. CONFIG_BPF (y) -> 保持 y (无法安全关闭)
| 项目 | 详情 |
|------|------|
| 省约 | ~2-10 KB (仅 select CRYPTO_LIB_SHA256，非常小) |
| 安全度 | MED - 不建议关 |
| 依赖/断链 | `net/Kconfig: select BPF` (无条件)。除非关闭整个 networking，否则 BPF 必选 |
| 理由 | BPF base 被 NET 无条件 select。BPF_SYSCALL=n 和 BPF_JIT=n 已经限制了实际代码。关闭 BPF 需要关闭 NET，不可行。 |

### 15. CONFIG_CGROUPS (y) -> 保持 y
| 项目 | 详情 |
|------|------|
| 省约 | ~2-5 KB (cgroup core 代码很小) |
| 安全度 | MED - 保留基础 |
| 理由 | cgroup 基础基础设施很小。所有子选项(CGROUP_SCHED、CGROUP_FREEZER、CGROUP_PIDS 等)均已 n。关闭 CGROUPS 可能影响 device_cgroup 等少数地方 |

### 16. CONFIG_STACKTRACE (y) -> 可选关
| 项目 | 详情 |
|------|------|
| 省约 | ~10-20 KB |
| 安全度 | LOW - 关后可能影响 oops 栈回溯 |
| 依赖/断链 | 被 TRACING 选中(TRACING->STACKTRACE if STACKTRACE_SUPPORT)。如果关了 TRACING，STACKTRACE 可能变 n |
| 理由 | oops 时的栈回溯信息，debug 有用但非必须 |

---

## LOW 优先级 (改动小/影响大，不建议)

### 17. CONFIG_NFS_V3/NFS_V4 子选项 (已在 NFS_FS 内)
| 项目 | 详情 |
|------|------|
| 省约 | 已随 NFS_FS 一起关闭 |
| 理由 | 不单独处理 |

### 18. CONFIG_TASKS_TRACE_RCU (y) -> 待 TRACING+UPROBES 关闭后评估
| 项目 | 详情 |
|------|------|
| 省约 | ~5-15 KB |
| 安全度 | LOW - 影响 trace RCU |
| 依赖/断链 | 被 TRACEPOINTS (init/Kconfig:2220, select TASKS_TRACE_RCU)、UPROBES (arch/Kconfig:185)、BPF_SYSCALL (已 n)、FORCE_TASKS_TRACE_RCU (已 n) 选择 |
| 理由 | 关闭 TRACING 后 TRACEPOINTS=n，关闭 UPROBES 后不再被选。但 RCU 层可能有其他选择者 |

### 19. CONFIG_NAMESPACES (y) 及子选项
| 项目 | 详情 |
|------|------|
| 省约 | ~2-5 KB |
| 安全度 | LOW - namespace 基础代码很小，且可能被其他代码依赖 |
| 理由 | 当前已保留。关闭影响较大但节省极小 |

---

## 编译依赖链总结

关闭顺序建议 (如果手动编辑 .config):

```
# 第一组 - 文件系统大块 (独立，无交叉依赖)
CONFIG_NFS_FS=y -> n      # 级联关闭: SUNRPC, SUNRPC_GSS, NFS_V3, NFS_V4, NFS_ACL_SUPPORT
CONFIG_UBIFS_FS=y -> n    # 级联关闭: UBIFS_FS_LZO, UBIFS_FS_ZLIB, UBIFS_FS_ZSTD, UBIFS_FS_XATTR, UBIFS_FS_SECURITY
CONFIG_NTFS3_FS=y -> n
CONFIG_SQUASHFS=y -> n

# 第二组 - Tracing 大块 (互相嵌套)
CONFIG_TRACING=y -> n     # 级联关闭: EVENT_TRACING, CONTEXT_SWITCH_TRACER, TRACEPOINTS, RING_BUFFER, NOP_TRACER, BINARY_PRINTF, TRACE_CLOCK, NEED_TASKS_RCU, STACKTRACE(if)
CONFIG_PERF_EVENTS=y -> n # 级联关闭: trace_event_perf, driver/perf/ 代码
CONFIG_UPROBES=y -> n     # 级联关闭: TASKS_TRACE_RCU(if no other selector)

# 第三组 - Security
CONFIG_SECURITYFS=y -> n  # 独立选项
```

---

## 不改动的项目 (保持现状)

| CONFIG | 理由 |
|--------|------|
| CONFIG_EXT4_FS=y | 保守保留，ramdisk 可能有 ext4 分区 |
| CONFIG_FAT_FS=y / CONFIG_VFAT_FS=y / CONFIG_MSDOS_FS=y | SD card 挂载需要 |
| CONFIG_PROC_FS / CONFIG_SYSFS | /proc 和 /sys 调试必需 |
| CONFIG_TMPFS=y | ramdisk 解压需要 (ramfs/tmpfs) |
| CONFIG_BINFMT_ELF=y | 运行 elf 二进制必需 |
| CONFIG_BINFMT_SCRIPT=y | 运行 shell script 必需 |
| CONFIG_CGROUPS=y | 基础 cgroup 保留 |
| CONFIG_BPF=y | 被 NET 无条件 select，关闭 NET 不可行 |
| CONFIG_NAMESPACES 系列 | 关闭收益极小，可能影响系统调用 |
| CONFIG_STACKTRACE | 可选关但收益小 |
| CONFIG_CRC* | 底层库，被多处引用 |

---

## 估省汇总

| 类别 | 估省 KB | 安全度 |
|------|---------|--------|
| NFS_FS + SUNRPC + SUNRPC_GSS | 500-900 | HIGH |
| UBIFS_FS | 150-300 | HIGH |
| NTFS3_FS | 100-200 | HIGH |
| SQUASHFS | 80-150 | HIGH |
| TRACING + 所有 trace 子模块 | 400-700 | HIGH |
| PERF_EVENTS | 100-200 | HIGH |
| UPROBES | 50-100 | HIGH |
| SECURITYFS | 10-20 | HIGH |
| **合计** | **~1390-2570 KB** | |

预估关闭所有 HIGH 项目后，zImage 从 8.5MB 降至 ~5.9-7.1MB。
距离目标 4.96MB 还差 ~1-2MB。

---

## 已分析但不在本范围的项目 (task9 前置)

以下项目已由 task9 前置分析覆盖，本清单不重复：
- clk/regulator 优化
- mfd 精简
- crypto 子选项
- thermal 驱动
- cpufreq
- leds
