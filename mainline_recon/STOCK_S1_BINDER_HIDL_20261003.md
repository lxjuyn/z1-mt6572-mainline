# Stock S1: Binder 7 and HIDL compatibility audit (2026-10-03)

## Finding

The unmodified original kernel can remain the kernel for a **process-local
native HIDL compatibility experiment**, but this is not a proven Android 9 boot
solution. Binder32 selection alone does not provide scatter/gather or hardware
service-manager contexts. Simply changing manifest transport to passthrough
and disabling hwbinder services would still block or fail mandatory services.
The known Binder 7 / rejected BC_TRANSACTION_SG evidence is taken as an input;
this audit does not repeat kernel disassembly.

## Existing control flow and constraints

* `android9/los16/build/make/core/soong_config.mk:10`,
  `frameworks/native/libs/binder/Android.bp:111` and
  `system/libhwbinder/Android.bp:58`: `TARGET_USES_64_BIT_BINDER` selects the
  binder32bit product variable and `BINDER_IPC_32BIT=1`. Both Binder libraries
  need the same actual target ABI. This resolves protocol layout, not features.
* `system/libhwbinder/IPCThreadState.cpp:630` always sends
  `BC_TRANSACTION_SG` for hardware Binder. Replacing this opcode alone with
  BC_TRANSACTION is invalid: HIDL parcels contain embedded buffers, pointer
  parent fixups and native-handle/FD arrays requiring SG semantics.
* `system/libhwbinder/ProcessState.cpp:362` opens `/dev/hwbinder`. Aliasing this
  path to `/dev/binder` would share the same old context manager and still lack
  SG; it does not create an independent hardware service namespace.
* Original `system/libhidl/transport/ServiceManagement.cpp:664` resolves the
  remote hardware service manager **before** checking passthrough/getStub.
  `defaultServiceManager1_1()` waits for `hwservicemanager.ready` when the device
  exists. Its `registerReference()` also contacts the remote registry after
  loading a local HAL. Thus stock cannot use the ordinary passthrough fallback
  without an explicit compatibility change.
* `PassthroughServiceManager::openLibs/get` loads package-version `-impl*.so`
  and invokes `HIDL_FETCH_<Interface>`. A standalone `-service` executable is
  not a passthrough implementation. The lookup must fail if no real factory
  exists; success cannot be supplied by a manifest declaration.

## Native compatibility gate implemented

Authorized source change is confined to
`android9/los16/system/libhidl/transport/ServiceManagement.cpp`.

When read-only `ro.z1.stock_kernel=true`:

1. `getRawServiceInternal()` directly uses the existing passthrough manager
   and real factory before remote manager lookup. Missing factory returns
   nullptr with a diagnostic. There is no retry/wait or fake interface.
2. `wrapPassthrough` behavior remains for normal lookups; raw `getStub` and
   supported TREBLE_TESTING_OVERRIDE behavior are retained. The existing Bs
   wrappers own their implementation through normal strong references and
   maintain local oneway task lifetime/instrumentation.
3. `defaultServiceManager1_1()` returns nullptr instead of waiting for an
   unavailable hardware registry. It does not pretend a local registry exists.
4. `registerReference()` does not report local clients to that nonexistent
   registry. This omits remote bookkeeping only, not object ownership.

When the property is absent/false the original paths remain. Property must be
set in the dedicated stock product before clients start; do not enable it for
the working Linux 7 product. This source change has diff-whitespace validation
only; target compilation and runtime verification are owned by the coordinating
build. Backup: `bridge_backups/stock_hidl_passthrough_pre_20261003_195323/`.

## Native targets and remaining mandatory integration

| Consumer | Process-local route | Required follow-up |
| --- | --- | --- |
| SurfaceFlinger composer | Existing composer 2.1 `-impl` exports HIDL_FETCH_IComposer; standard HWC1 adapter can remain local | Package real compatible stock gralloc/HWC, mapper/allocator `-impl`, verify EGL/rendering and factory namespace loads |
| Graphics allocator/mapper | Existing 2.0 factories available | Remove remote allocator/composer daemon dependency; verify each process imports and maps handles through local mapper |
| ConfigStore | Use a real `-impl` for supported version or existing optional defaults | 1.1 current source builds a standalone service; its executable alone cannot satisfy local factory lookup. Missing optional interface must remain honestly absent |
| Audio factory/effects | libaudiohal probes HIDL 4.0 then 2.0 with getService; existing versioned implementation factories can be packaged | Verify factory implementation/legacy HAL selection, streams and callback lifecycle in audioserver; missing factory must not be accepted as working audio |
| Keystore Keymaster | Existing keymaster3 `HIDL_FETCH_IKeymasterDevice` supports local factory loading | Verify stock product packages appropriate actual implementation and keystore requirements; no fake hardware-backed keys |
| BatteryService Health2 | **Unresolved mandatory route** | Requires native/Java adapter or suitable genuine local Health2 factory and Java notification bridge; existing service executable cannot be reused unchanged |
| Native netd | Classic Binder/socket interfaces remain usable with Binder32 | Stock must explicitly omit optional vendor-HIDL publication, preserving native netd interfaces and errors |

### BatteryService is a hard unresolved gate

`frameworks/base/services/core/java/com/android/server/BatteryService.java:289`
initializes `HealthServiceWrapper`. Lines 1406 onward request health2 instances,
throw `NoSuchElementException` if none is available, register callbacks, and
request hardware IServiceManager notifications. The current health2 implementation
is a static library plus `health_service_main()` binary; it has no passthrough
HIDL_FETCH_IHealth entry point. Its common service startup additionally
CHECKs successful registerAsService.

`frameworks/base/core/jni/android_os_HwBinder.cpp:323` calls native
getRawServiceInternal, then toBinder and starts a hardware Binder thread pool.
The native gate can remove the initial registry wait but does not supply a
Java notification registry or a valid health2 factory. Java-generated IHealth
is not automatically replaced by the native C++ interface. Do not claim this
is solved merely by changing VINTF transport.

A possible follow-up is an explicitly local native health bridge with proper
periodic BatteryMonitor event loop and Java callback delivery, or restoration
of a real legacy battery Binder backend with a matching framework consumer.
Both require actual implementation and tests. Returning fabricated battery
values, skipping initialization, or fake registration notifications is not an
acceptable completion.

### netd has an embedded HIDL publication gate

`system/netd/server/NetdHwService.cpp:59` configures hardware RPC and calls
INetd::registerAsService. `system/netd/server/main.cpp` treats failure of that
last service startup as exit(1). The native passthrough gate intentionally has
no hardware registry, so unchanged netd will fail here even if classic networking
works. A stock-specific product adaptation can explicitly omit this vendor
HAL publication while retaining NetdNativeService and command/dns/fwmark
sockets. It must not return a made-up successful registration.

## Callback limits and acceptance checks

Passthrough objects/callbacks work only within their hosting process unless an
actual IPC bridge is added. A Bp proxy, Java remote stub, native handle, FMQ or
cross-process callback reference does not become transport-independent simply
because initial service lookup was local. Independently initialized local HAL
instances may also conflict over devices previously owned by a single daemon.

Minimum checks before calling the experiment bootable:

* Real target Binder32 protocol succeeds for servicemanager, zygote and native
  Binder consumers without layout mismatches.
* Native HAL factory lookups return correct local interfaces promptly, including
  mapper/import, with no hwservicemanager.ready wait or BC_TRANSACTION_SG use.
* Stock mode retains Bs wrapper and callback lifetime; failed factories return
  unavailable, not success. Non-stock product behavior is unchanged.
* BatteryService receives real HealthInfo updates and system_server completes
  initialization; Java registration/callback path is explicitly demonstrated.
* netd starts native Binder and sockets without an unavailable vendor-HIDL gate.
* SurfaceFlinger/audio/keystore remain alive and perform their actual function;
  no requirement is inferred from build success alone.

Keeping standard cross-process HIDL instead requires kernel SG/object handling
and independent contexts, or a fully implemented substitute IPC transport.
The stock binary lacks those capabilities. Native passthrough is a narrower
compatibility technique, not evidence that the kernel has acquired them.
