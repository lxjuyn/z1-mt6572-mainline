# Stock S1 BatteryService transport

## Root cause / scope

Pie BatteryService startup unconditionally constructs HealthServiceWrapper,
registers HIDL service notifications and waits indefinitely for HealthInfo.
The unchanged stock3.4 kernel does not provide the modern HIDL transport
expected by this startup path. This is a source-level incompatibility; S1 has
not been booted and no live S1 failure is claimed.

## Change

Only ro.z1.stock_kernel=true enables Z1StockBatteryMonitor. The monitor supplies
plain HealthInfo Java values to the existing BatteryService.update path from
real /sys/class/power_supply attributes every5seconds. It does not instantiate
HealthServiceWrapper or subscribe HIDL notifications. Non-stock path is unchanged.

Missing/malformed/out-of-range capacity remains unavailable and sets status
UNKNOWN; getProperty returns NOT_SUPPORTED. No fixed50/100% value is supplied.
Existing BatteryService isPoweredLocked treats UNKNOWN as powered, preventing
missing-capacity default0 from triggering an empty-battery shutdown. Real
measured0% plus discharging is preserved. Temperature is copied in tenthsC,
with standard temp and stock batt_temp fallback; no clamp or forcednormal
temperature is added. shutdownIfOverTempLocked and its configured threshold
are unchanged. Missing temp remains unavailable, explicitly shown by dump;
HealthInfo's default0 is not a claimed measurement.

Standard voltage_now uV is converted to mV; stock batt_vol is alreadymV. The
MTK source sets batt_temp=temperature*10 and batt_vol=bat_vol; stock binary
also contains /sys/class/power_supply/battery/batt_temp and batt_vol paths.
Real stock data still needs runtime confirmation.

Both BatteryPropertiesRegistrar wrapper accesses have stock getProperty /
scheduleUpdate branches, so null HIDL wrapper is never dereferenced there.
Dump includes measured capacity/temp/voltage availability flags.

## Validation

- New Java class compiled against actual generated Pie framework and health
  jars with Java8: PASS, out/stock_battery_java_s1/compile.log.
- Real Java parsing fixture:24assertions PASS, test.log. Tests cover missing,
  malformed and101%capacity; real0%discharging; hot720 and1335000tenthsC
  unchanged; -50tenthsC; standard uV/stockmV; USB/AC; current/average/charge;
  energy uWh→nWh and overflow rejection.
- BatteryService integration inspected for every mHealthServiceWrapper access.
- Full services build is coordinated by root; not run by this subtask.

Sources: frameworks/base/services/core/java/com/android/server/BatteryService.java
and Z1StockBatteryMonitor.java. Build entry ./android9/build_z1.sh services
(or the product's services.core target).

This is an experimental Z1 stock-kernel compatibility transport. It still
requires real sysfs permissions, physical temperatures/capacity, polling update
timing, dumpsys/getProperty and hardware boot validation.
