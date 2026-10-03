# Stock S1: Pie SELinux policy26 diagnostic candidate

## Concrete result

`out/android9_stock_s1_policy/sepolicy` is a newly compiled **394,417-byte**
SELinux policy, version **26**, MLS enabled, allow_unknown enabled (config=5).
SHA256: `c3ffbe20a780f61713b79cb00269a1b5dd8c6cb7ec8ceae6b142e913ba5613ca`.

Status: **HOST_COMPILE_AND_PARSE_PASS_NOT_KERNEL_LOADED**.
No kernel policy load, device boot or flash was performed in this subtask.
This addresses policy binary format compatibility; it does not fix Binder/HIDL,
other kernel interfaces, or prove Android9 startup.

## Input and A7 comparison

The author A7/LOS14.1 boot ramdisk policy at
`out/stock_reuse_baseline_20261001/fuquan_ramdisk_files/sepolicy` is version26,
174,381 bytes. Host `sepolicy-analyze permissive` reports su and sudaemon.
Factory policy is also version26 (235,805 bytes). Their format therefore agrees
with original 3.4.67's statically established maximum version26. The A7 policy
is not reused wholesale: it does not provide the complete Pie type set.

Input is the existing complete Pie policy.conf:
`android9/los16/out/target/product/z1/obj/ETC/sepolicy_neverallows_intermediates/policy.conf`.
SHA256 `5c1a0f8ac017ba54d6695c907915963c7c63238f8f8ac291ff53f2e12258c397`.
An immutable copy is in output/input.policy.conf. Tools and input hashes are
recorded in MANIFEST.json. Shared Android sources were not edited.

## Precise adaptation

`tools/z1_stock_policy.py`:

1. Removes 24 allowxperm and 11 neverallowxperm statements. No auditallowxperm or
   dontauditxperm statements occurred. Removed text is saved separately.
2. Removes extended_socket_class policycap, whose new socket-class mapping is
   absent from the old kernel; keeps network_peer_controls and open_perms.
3. Compiles the retained rules/types/contexts using checkpolicy -M -c26 -U allow.
4. Enumerates the actual compiled domain attribute (176 concrete types), adds
   permissive declarations within the TE/RBAC section, then recompiles.
5. Verifies the header, host binary parsing and the 176 permissive domains.

The normal/classic ioctl rules and all existing type/context declarations are
retained. This is not a binary version-number patch, nor the previously failed
partial pie_policy_v26.bin. A first attempt appended permissive declarations
after context statements and failed grammar checking; its complete output is
preserved under bridge_backups/stock_s1_policy_retry_*/failed_attempt.

## Validation

- checkpolicy base and final compilation: exit0.
- checkpolicy -b reads final binary and serializes a version26 binary: exit0.
  Serialization changes binary ordering/size, so byte equality is not claimed.
- sepolicy-analyze enumerates 176 domains and 176 permissive domains.
- checkfc validates all nine current root file/property/service/hwservice/vendor
  context inputs: exit0, including platform and vendor contexts.
- checkseapp -p validates both platform and vendor seapp contexts: exit0.
- The final header is policy26/config5/sym_num8/ocon_num7.

These are host format/type/context checks using modern libsepol. Only a load
into the actual unchanged 3.4.67 kernel establishes full parser compatibility.

Reproduce to a NEW output directory:

```sh
python3 tools/z1_stock_policy.py \
  --input android9/los16/out/target/product/z1/obj/ETC/sepolicy_neverallows_intermediates/policy.conf \
  --output-dir out/android9_stock_s1_policy_repeat \
  --checkpolicy android9/los16/out/host/linux-x86/bin/checkpolicy \
  --analyze android9/los16/out/host/linux-x86/bin/sepolicy-analyze
```

## Init integration

For the separate stock S1 ramdisk, replace **/sepolicy** with this candidate and
retain the current matching Pie platform/vendor context files. Keep
androidboot.selinux=permissive (using the actual stock ATAG/cmdline mechanism).
The intended path is Pie's normal monolithic LoadPolicy. Do not remove its
failure checks: init/selinux.cpp:388-389 must still fail if loading fails.

Check that `/system/etc/selinux/plat_sepolicy.cil` is not readable when policy
loads: IsSplitPolicyDevice at init/selinux.cpp:252 selects split loading solely
by that file's existence. A non-Treble product name alone does not select
monolithic policy. The present Z1 product does not install that CIL file.
Do not install a conflicting split/precompiled policy on the stock branch.

A7's older policy/context set should not be mixed into Pie as a shortcut: this
candidate's type set has been checked against the actual Pie context inputs.

## Explicit diagnostic limitations

**All domains are permissive.** **Unknown class/permission checks are allowed.**
**Extended ioctl rules are removed.** The candidate intentionally lacks normal
SELinux access enforcement and fine-grained ioctl restrictions. It is for a
bounded stock-kernel startup experiment and is not a secure shipping policy.
Even this permissive policy must really load; missing context types, parser
errors and executable startup failures must remain visible.

The stock kernel may lack Binder LSM hooks regardless of policy declarations.
This policy does not add such hooks, hwbinder contexts or HIDL SG support.
Future enforcing support requires a separate compatibility/security design;
changing back to enforcing on this diagnostic policy is not a complete fix.
