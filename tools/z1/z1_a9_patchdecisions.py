#!/usr/bin/env python3
"""z1_a9_patchdecisions.py —— 把 stock(3.4.67) 线那 25 份补丁的"撤/留"判定做成机器可读清单，
并对 stock_patches/manifest.json 做双向完整性校验。

为什么需要它：mainline 线上，stock 线为"没有 hwbinder / 没有 dm-verity / timerfd 不认 BOOTTIME"
而做的一整套代偿补丁不但多余而且**有害**（例如 ServiceManagement.cpp 的 ro.z1.stock_kernel 门控
会让 vendor HAL 永远拿不到 binderized 服务）。这类东西一旦被静默带进新线，表现是"服务起不来但
说不清为什么"，代价是一整轮刷机。所以：每一份补丁都必须有明确判定，漏一项就报错。

判定依据逐条写进 JSON（含"内核侧事实"字段），出处 mainline_recon/A9_SYSTEM_MIGRATION_CHECKLIST.md。
"""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))            # .../a9_wt/tools/z1
WT = os.path.normpath(os.path.join(ROOT, '..', '..'))         # .../a9_wt
REPO = os.path.normpath(os.path.join(WT, '..'))               # 工作区根
BRANCH = 'origin/wip/android9-stock-s1'
MANIFEST = BRANCH + ':android9/stock_patches/manifest.json'
OUT = os.path.join(WT, 'android9', 'mainline', 'patchset', 'decisions.json')

# verdict: revert(必须撤销，mainline 上有害) / keep(保留) / rework(保留但需按 mainline 重写)
DECISIONS = {
  'system__libhidl__transport__ServiceManagement.cpp.patch': ('revert',
     'ro.z1.stock_kernel 门控：getRawServiceInternal 先走本地 factory、defaultServiceManager1_1() 返 nullptr',
     'CONFIG_ANDROID_BINDER_IPC=y 且 ANDROID_BINDER_DEVICES 含 hwbinder，/dev/hwbinder 由驱动静态注册'),
  'system__libhidl__transport__HidlTransportSupport.cpp.patch': ('revert',
     '不等 hwservicemanager.ready', '/dev/hwbinder 存在，hwservicemanager 应正常运行'),
  'hardware__interfaces__configstore__1.1__default__stock_factory.cpp.patch': ('revert',
     '造 configstore 1.0/1.1 本地 passthrough 工厂', '为"无 hwbinder"造的代偿；mainline 用 AOSP 默认实现'),
  'hardware__interfaces__configstore__1.1__default__stock_factory_v1_0.cpp.patch': ('revert',
     '同上(v1_0)', '同上'),
  'hardware__interfaces__configstore__1.1__default__Android.mk.patch': ('revert',
     '把上述工厂编进 vendor', '同上'),
  'frameworks__native__services__surfaceflinger__main_surfaceflinger.cpp.patch': ('revert',
     'stock 不启 allocator/display HIDL 发布', '需要真 allocator@2.0/mapper@2.0/composer'),
  'frameworks__native__services__surfaceflinger__RenderEngine__RenderEngine.cpp.patch': ('revert',
     'configstore 缺失时回退 EGL 默认', '撤后应真读 configstore'),
  'frameworks__base__core__jni__android_os_HwBinder.cpp.patch': ('revert',
     'Java HwBinder 直接报不可用', '/dev/hwbinder 可用'),
  'frameworks__base__services__core__jni__com_android_server_SystemServer.cpp.patch': ('revert',
     '不发布 sensor/scheduling HIDL', '撤销后重新评估'),
  'system__netd__server__main.cpp.patch': ('revert',
     '不启 NetdHwService', 'mainline 有 hwbinder；tether offload HAL 可用性另议'),
  'system__connectivity__wificond__main.cpp.patch': ('revert',
     '不请求 /dev/hwbinder', '撤销；WiFi 按 mt6620 线另评'),
  'system__connectivity__wificond__scanning__offload__offload_scan_manager.cpp.patch': ('revert',
     'offload 报不可用', '同上'),
  'frameworks__av__services__mediacodec__main_codecservice.cpp.patch': ('revert',
     'stock 下直接退出(无 OMX HIDL)', '撤销或做明确禁用决策，不能靠"退出"糊过去'),
  'frameworks__base__services__core__jni__com_android_server_AlarmManagerService.cpp.patch': ('revert',
     'timerfd CLOCK_BOOTTIME EINVAL 回落 CLOCK_BOOTTIME_ALARM',
     'mainline 原生支持 BOOTTIME；回落会改非唤醒闹钟功耗语义。板上由探针 RESULT_STEP_timerfd_boottime 给证据'),
  'system__core__init__security.cpp.patch': ('revert',
     '/proc/sys/vm/mmap_rnd_bits 在 ENOENT 时放行', 'mainline 有该 sysctl，应真设熵值'),
  'external__minijail__syscall_filter.c.patch': ('revert',
     'stock+permissive 放宽 seccomp', 'BPF_SYSCALL+SECCOMP_FILTER 已开，应恢复标准策略'),
  'system__core__init__selinux.cpp.patch': ('revert',
     '/z1_stock_kernel marker 跳过 streaming policy', '与内核强相关，须按 mainline 重定 policy 加载方式'),
  'frameworks__base__services__core__java__com__android__server__Z1StockBatteryMonitor.java.patch': ('revert',
     '直读 power_supply sysfs 替代 HIDL health', '回到标准 android.hardware.health@2.0（本仓已有 z1 health 服务）'),
  'system__security__keystore__keystore_main.cpp.patch': ('keep',
     '软件后端、不虚报 TEE', '硬件事实，与内核版本无关'),
  'system__security__keystore__KeyStore.cpp.patch': ('keep', '同上', '同上'),
  'hardware__interfaces__keymaster__4.0__support__Keymaster.cpp.patch': ('keep',
     '软件 keymaster 后端', '无 TEE 是硬件事实；内核侧 CONFIG_KEYS=y 已就位'),
  'frameworks__base__services__core__java__com__android__server__BatteryService.java.patch': ('keep',
     '把读 Settings.Global 延到 PHASE_SYSTEM_SERVICES_READY', '规避的是 LOS16 自身时序，与 3.4/mainline 无关'),
  'system__core__init__log.cpp.patch': ('keep', 'InitAborter 前有界 fatal 捕获', '纯诊断，保留'),
  'system__core__init__init.cpp.patch': ('rework',
     'ro.boot.hardware 兜底填 z1', '改为在 DTS chosen 给 androidboot.hardware=z1（已做），兜底可留作保险'),
  'development__samples__Home__src__com__example__android__home__Home.java.patch': ('keep',
     '壁纸 SecurityException 容错', '与内核无关'),
}

def sh(cmd):
    return subprocess.run(cmd, shell=True, cwd=WT, capture_output=True, text=True)

def main():
    r = sh(f'git show {MANIFEST}')
    if r.returncode != 0:
        print('取不到 %s: %s' % (MANIFEST, r.stderr.strip()), file=sys.stderr); return 2
    entries = json.loads(r.stdout)
    by_patch = {e['patch']: e for e in entries}

    r2 = sh(f'git ls-tree -r --name-only {BRANCH} -- android9/stock_patches')
    on_disk = {os.path.basename(p) for p in r2.stdout.split() if p.endswith('.patch')}
    in_manifest = set(by_patch)

    undecided = sorted(on_disk - set(DECISIONS))
    phantom     = sorted(set(DECISIONS) - on_disk)
    untracked   = sorted(on_disk - in_manifest)

    out = []
    for name in sorted(on_disk):
        d = DECISIONS.get(name)
        if not d:
            continue
        verdict, why, fact = d
        e = by_patch.get(name, {})
        out.append({'patch': name, 'file': e.get('file', ''),
                    'verdict': verdict, 'why': why, 'kernel_fact': fact,
                    'base_sha256': e.get('base_sha256', ''),
                    'result_sha256': e.get('result_sha256', '')})

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(out, open(OUT, 'w'), ensure_ascii=False, indent=2)

    n_rev = sum(1 for x in out if x['verdict'] == 'revert')
    n_keep = sum(1 for x in out if x['verdict'] == 'keep')
    n_rework = sum(1 for x in out if x['verdict'] == 'rework')
    print('补丁总数=%d  已判定=%d  (revert=%d keep=%d rework=%d)'
          % (len(on_disk), len(out), n_rev, n_keep, n_rework))
    print('未判定的补丁      : %s' % (undecided or '无'))
    print('判定指向不存在的补丁: %s' % (phantom or '无'))
    print('有 .patch 文件但不在 manifest.json: %s' % (untracked or '无'))
    print('写出 %s' % OUT)
    bad = undecided or phantom
    print('PATCH_DECISION_FAIL=%d' % (len(undecided) + len(phantom)))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
