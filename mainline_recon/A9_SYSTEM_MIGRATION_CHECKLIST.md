# A9 从 stock 线迁到 mainline 线：system/userspace 侧待办清单

依据 `origin/wip/android9-stock-s1` 的 `android9/stock_patches/`(25 份) 逐个判定，
并对齐本机 `wip/a9-mainline` 的 `A9_CONFIG_GATE.md` / `A9_DMA_HEAP_CONTRACT.md`。
用途：交给持有 `android9/los16/` 源树的机器执行；本机不产 system.img。

## 1. 必须撤销的补丁（stock 3.4.67 专属代偿，在 mainline 上有害）

| 补丁文件 | 它做什么 | 为什么在 mainline 上必须撤 |
|---|---|---|
| system__libhidl__transport__ServiceManagement.cpp | `ro.z1.stock_kernel` 门控：getRawServiceInternal 先走本地 factory、`defaultServiceManager1_1()` 返 nullptr、不注册远端 | mainline 有 /dev/hwbinder，不撤则 vendor HAL 永远拿不到 binderized 服务，manifest/VINTF 也对不上 |
| system__libhidl__transport__HidlTransportSupport.cpp | 不等 `hwservicemanager.ready` | 同上；hwservicemanager 应正常运行 |
| hardware__interfaces__configstore__1.1__default__{stock_factory,stock_factory_v1_0,Android.mk} | 造 configstore 1.0/1.1 本地 passthrough 工厂 | 是为"无 hwbinder"造的代偿，mainline 用 AOSP 默认实现 |
| frameworks__native__services__surfaceflinger__main_surfaceflinger.cpp | stock 不启 allocator/display HIDL 发布 | 需要真 allocator@2.0/mapper@2.0/composer |
| frameworks__native__services__surfaceflinger__RenderEngine__RenderEngine.cpp | configstore 缺失时回退 EGL 默认 | 撤后应真读 configstore |
| frameworks__base__core__jni__android_os_HwBinder.cpp | Java HwBinder 直接报不可用 | mainline 可用 |
| frameworks__base__services__core__jni__com_android_server_SystemServer.cpp | 不发布 sensor/scheduling HIDL | 撤销后重新评估 |
| system__netd__server__main.cpp | 不启 NetdHwService | mainline 有 hwbinder；tether offload HAL 可用性另议 |
| system__connectivity__wificond__{main,scanning__offload__offload_scan_manager}.cpp | 不请求 /dev/hwbinder、offload 报不可用 | 撤销；WiFi 能力按 mt6620 线另评 |
| frameworks__av__services__mediacodec__main_codecservice.cpp | stock 下直接退出（无 OMX HIDL） | 撤销或明确禁用决策，不能靠"退出"糊过去 |
| frameworks__base__services__core__jni__com_android_server_AlarmManagerService.cpp | `timerfd CLOCK_BOOTTIME` EINVAL -> 回落 `CLOCK_BOOTTIME_ALARM` | mainline 原生支持 BOOTTIME；回落会改非唤醒闹钟功耗语义，必须撤（探针 RESULT_STEP_timerfd_boottime 提供板上证据） |
| system__core__init__security.cpp | `/proc/sys/vm/mmap_rnd_bits` ENOENT 时放行 | mainline 有该 sysctl，应真设熵值而不是放行 |
| external__minijail__syscall_filter.c | stock+permissive 放宽 seccomp | 本机已开 `BPF_SYSCALL`+`SECCOMP_FILTER`，应恢复标准策略 |
| system__core__init__selinux.cpp | `/z1_stock_kernel` marker 跳过 streaming policy | 与内核强相关，必须按 mainline 重新决定 policy 加载方式 |
| frameworks__base__services__core__java__com__android__server__Z1StockBatteryMonitor.java | 直读 power_supply sysfs 替代 HIDL health | mainline 下回到标准 `android.hardware.health@2.0`（本仓已有 z1 health 服务骨架） |

## 2. 可保留（与内核版本无关）

- `system__security__keystore__{keystore_main,KeyStore}.cpp`、`hardware__interfaces__keymaster__4.0__support__Keymaster.cpp`：软件后端、不虚报 TEE —— 硬件事实，保留。
- `frameworks__base__services__core__java__com__android__server__BatteryService.java`：`PHASE_SYSTEM_SERVICES_READY` 延迟规避的是 LOS16 自身时序，与 3.4/mainline 无关，保留但重新审视。
- `system__core__init__log.cpp`：InitAborter 前有界 fatal 捕获，纯诊断，保留。
- `system__core__init__init.cpp`：`ro.boot.hardware` 兜底填 z1 —— 改为在 DTS chosen 里给 `androidboot.hardware=z1`（见第 4 节），兜底可留作保险。
- `development__samples__Home__src__...Home.java`：壁纸 SecurityException 容错，保留。

## 3. 必须新增（真正的工程量所在）

1. **gralloc 缓冲区来源**：主线无 `/dev/ion`。二选一（详见 `A9_DMA_HEAP_CONTRACT.md`）：
   A. ION 兼容层：`ION_IOC_ALLOC` -> `open("/dev/dma_heap/system") + ioctl(DMA_HEAP_IOCTL_ALLOC)`，`ION_IOC_FREE` -> `close(fd)`；
   B. 直接 GBM/libdrm prime 导出。本轮显示走 simple-framebuffer/fbdev，先 CPU 映射出图，lima+GBM 后置。
2. **sepolicy 面向 mainline 重做**：`ttyS0`(非 ttyMT0)、`/dev/binder|hwbinder|vndbinder`、`/dev/dma_heap/*`、configfs USB gadget 路径、cgroup/tmpfs/fuse 的 genfscon；首轮 permissive 收集 denial 再收紧。
3. **ueventd.rc / fstab.z1**：新增节点权限；分区节点名与 p4/p5/p6 几何需实测（禁止格式化 p6）。
4. **eMMC 带宽**：`&mmc0 bus-width` 从 <1> 提升（本机已出 B 镜像验证该单项，板上结论未回）。
5. **hwservicemanager/vndservicemanager 恢复运行**：stock 线是镜像级禁用的（`z1_stock_system_profile.py` 改 ext4 里的 RC），mainline 不要沿用。

## 4. boot 侧还需要第三次改动（本机可承担）

- `androidboot.hardware=z1`（+ 首轮 `androidboot.selinux=permissive`）**必须写进 DTS `chosen/bootargs`**：本启动链的 LK 不转发 boot.img 头 cmdline（已实测），且当前 main/a9_wt 的 dts 里 `androidboot` 出现 **0** 次。
- ramdisk 换成 A9 generic ramdisk（first-stage init）后：`linux,initrd-end = 0x84100000 + body字节数` 必须重算，且 ramdisk 512B MTK 头 0x04 小端字段要同步。
- 预算：当前 kernel+dtb = 4,792,108 B，boot 上限 6,291,456 B，**只剩约 217 KB**（A9 ramdisk 通常远大于此）
  -> 大概率要把非首启内建驱动改 =m 随 system 提供，或压 ramdisk；这条在做 A9 boot.img 前必须先在台账里算过。

## 5. 交付顺序建议

1. 本机：出 C 变体（A + `androidboot.hardware=z1 androidboot.selinux=permissive`，仍带探针 ramdisk）——先证明带 androidboot 参数时启动链不劣化。
2. AOSP 机：撤第 1 节补丁 + 恢复 hwservicemanager + 加 ION 兼容层 -> 产 system.img + A9 generic ramdisk。
3. 本机：用 `tools/z1_repack_a9_ramdisk.sh` 对 A9 ramdisk 重算 initrd-end、重编 dtb、打 A9 boot.img（含预算核对）。
4. 板上：读 `RESULT_STEP_*` 与 avc denial，决定 sepolicy 收紧范围。
