# Android 9 Stock S1：真实原厂 / A7 基线

## 身份与启动布局（已直接核验）

输入 out/fuquan_z1_review_20261001/boot.img（LOS14.1/A7 作者包）。
其 MTK KERNEL 段与 1/boot.img **逐字节完全相同**：

- 原厂内核 body 3336776 字节，SHA256 f9afaa2a38d64e5a6d6877ccb73ede2250b7efe6389fa60e66ee500cb885cc07。
- XZ 流位于 body 偏移15156；解压10191300字节。
- 解压 banner：Linux version 3.4.67，gcc4.7，2022-08-23 12:05:53 CST。
- Android boot v0，页2048，kernel_addr=0x10008000，ramdisk_addr=0x11000000，dt_size=0。**不用 Linux7 appended DTB/initrd/chosen 参数**。
- A7 boot SHA256 47566a84ea47cd1eab99702f6a285bff36913d30760417175d0e22a37903dfaa。

提取目录 out/android9_stock_s1_a7/，manifest.json 记录ramdisk每个条目mode/uid/gid。保留 init、RC、fstab、ueventd，不覆盖源码。

## fstab / 挂载

A7 fstab.mt6572 用 /emmc@android→/system、/emmc@usrdata→/data、/emmc@cache→/cache，并另挂 protect_f/protect_s；/data 无加密flag。当前 Pie fs_mgr 只发现 LABEL= 翻译，未发现 /emmc@ 翻译实现。因此**不可原样复制 /emmc@ 路径**，必须使用核实过的真实块设备或主动创建且核验链接。不要加format/encrypt/check来掩盖分区问题。A7 SD卡用 /devices/platform/mtk-msdc.1/mmc_host*；USB盘 /devices/platform/mt_usb*。原厂实际分区编号仍需与现有取证记录对照，A7标签本身不证明编号。

## USB / 日志

实际板级 init.mt6572.usb.rc 使用 /sys/class/android_usb/android0。on init设VID17EF；acm配置先disable，再PID7439、f_acm/instances=1、functions=acm、bDeviceClass02、enable1。adb,acm用PID749E。原厂二进制有 ttyGS%d / ACM 通知字符串，可沿用 /dev/ttyGS0 传输，但日志helper必须改为legacy配置；不能复用只会configfs的 --acm 初始化。

A7 generic init虽带init.usb.configfs.rc及configfs mount，它的板USB实际走legacy；不要因文件存在就宣称原厂支持configfs。Pie需显式 sys.usb.configfs=0 并避免legacy/generic action抢同一功能。A7默认 persist.sys.usb.config=none，无自动启动日志的保证。

## 图形 / ueventd / ABI

A7 ueventd.mt6572.rc：/dev/graphics/* 0660 root graphics，/dev/mali0 0666 system graphics，/dev/kick_powerkey 0600 system system；板init另chmod /dev/ion0666、/dev/mali0666并chown system graphics。graphics gralloc直接引用 /dev/graphics/fb0 和/dev/ion；hwcomposer还引用/dev/hdmitx及/dev/mtkfb_vsync。不存在Linux7 lima/DRM要求。注意mali与mali0两种路径不能凭RC判定实际节点，须保留对应权限并实测。

已提取实际ARM32 EABI5 HAL及依赖到blobs/，完整列表graphics-dependencies.json。主要依赖：

- gralloc.mt6572：libMali/libGLESv1_CM/libion/libhardware/libutils/libcutils/liblog。
- hwcomposer.mt6572：libui/libutils/libEGL/libGLESv1_CM/libsync/libm4u/libion/libdpframework/libhardware。
- libMali：libui/libgui/libbinder/libutils/libcutils/libsync/libcorkscrew/libdpframework及C运行库。

**不可把 A7 HAL 直接复制进 Pie 然后声称兼容**。按A7厂商库+Pie现有core依赖混合解析，已具体发现：

- gralloc：缺 android::CallStack(char const*,int,int)、ion_alloc_mm、ion_custom_ioctl。
- hwcomposer：缺ion_alloc_mm、ion_custom_ioctl。
- libMali：缺旧BufferQueue/GraphicBuffer/GraphicBufferAlloc/IGraphicBufferConsumer::BufferItem/Fence析构/CallStack接口，共7个强符号。
- EGL Mali：缺旧CallStack构造。

graphics-pie-mixed-abi.json列完整mangled符号和实际provider路径。这是动态符号静态检查，不是加载/硬件验证；即便补符号也仍须核对旧native_handle/ION ioctl以及HWC结构ABI。用现有源码gralloc+软件EGL适配原厂fbdev往往更可控，不得把“链接成功”当显示通过。

## Watchdog / 电源

A7 RC没有watchdogd服务，原厂内核解压字符串有wdtk-%d、[WDK]硬件喂狗和mtk_wdt_init，呈现内核MTK watchdog kicker；不能沿用Linux7 /sbin/watchdogd 5 10并假设同接口。A7 thermal_manager是class main root oneshot；charger /sbin/healthd -c class charger critical，另generic healthd服务。power.default.so依赖liblog/libc/libstdc++/libm，不直接引用可见sysfs字符串。原厂battery power_supply注册AC/USB/Battery；Pie health应读取真实原厂power_supply，而不是主线BAT_TEMP ADC绑定。旧thermal_manager有依赖/行为未验证，不建议盲启。

## Android9 板RC必须剥离的主线专属部分

1. init.z1.network.rc内41个 Linux7 .ko：不要加载到3.4.67。
2. /config/usb_gadget/g1全套、configfs UDC查找、z1_usb_setup.sh和现诊断helper的configfs ACM创建。
3. Linux7专属zram配置/模块：原厂是否有zram应读节点核验，不能把配置存在当保证。
4. watchdogd假设、mainline DT chosen/adc-battery，以及lima/DRM/显存布局假设。

保持Pie核心init/fstab语法与服务框架，不复制旧A7整份init.rc/sepolicy或任意旧系统核心库。该审计只提取基线，不生成或刷写S1镜像。
