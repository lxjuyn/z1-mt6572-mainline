# cfg80211 band / wiphy flags 适配笔记

移植 MTK 3.4.67 vendor WiFi driver (MT6620) 到 mainline 7.0-rc7。`gl_init.c` 七处报错。

## IEEE80211_BAND_* → NL80211_BAND_*

vendor 用法（gl_init.c:711/737/810/820/2128/2131/2711）：
```c
.band = IEEE80211_BAND_2GHZ,          /* struct ieee80211_channel 字段赋值 */
prWdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &mtk_band_2ghz;  /* 数组索引 */
prWdev->wiphy->bands[IEEE80211_BAND_5GHZ] = NULL;
```
mainline `include/net/cfg80211.h:5852`（doc）与 `struct ieee80211_channel.band`（`cfg80211.h:196`）已是 `enum nl80211_band`；`wiphy->bands` 为 `struct ieee80211_supported_band *bands[NUM_NL80211_BANDS]`（`cfg80211.h:6159`）。3.4.67 的 `enum ieee80211_band` 在 mainline 已并入 `enum nl80211_band`（定义在 `include/uapi/linux/nl80211.h`），旧名整型值顺序保持一致（2GHZ=0, 5GHZ=1）。最低改动——加两行别名即可，`wiphy->bands[别名]` 索引值不变，`.band = 别名` 赋值类型 `enum nl80211_band` 直接兼容：
```c
#define IEEE80211_BAND_2GHZ NL80211_BAND_2GHZ
#define IEEE80211_BAND_5GHZ NL80211_BAND_5GHZ
```

## WIPHY_FLAG_CUSTOM_REGULATORY

vendor（gl_init.c:2135）：
```c
prWdev->wiphy->flags = WIPHY_FLAG_CUSTOM_REGULATORY | WIPHY_FLAG_SUPPORTS_FW_ROAM;
```
mainline `enum wiphy_flags`（`cfg80211.h:5333`）已**删除** `WIPHY_FLAG_CUSTOM_REGULATORY`——该语义搬到 `wiphy->regulatory_flags` 的 `enum ieee80211_regulatory_flags`，值 `REGULATORY_CUSTOM_REG = BIT(0)`（见 `include/net/regulatory.h:161`）。`regulatory.h:94-103` 注释明确：driver 调 `wiphy_apply_custom_regulatory(wiphy, regd)` 时 core 会自动给 `wiphy->regulatory_flags` 置上 `REGULATORY_CUSTOM_REG`，driver 无需手设。mainline 另保留 `WIPHY_FLAG_SUPPORTS_FW_ROAM`（在 `cfg80211.h:5299`/5333 区域），故 `|` 右半原样保留。

最低改动（不改 vendor 注册流程）：
```c
#define WIPHY_FLAG_CUSTOM_REGULATORY 0  /* 语义已移到 regulatory_flags，由 wiphy_apply_custom_regulatory() 自动置位 */
```
把 wiphy flags 写入那行降级为 `WIPHY_FLAG_SUPPORTS_FW_ROAM`（别名占 0 不影响位或）。若 vendor 后续确有自定义 regd 注入，改调 `wiphy_apply_custom_regulatory(prWdev->wiphy, &mtk_regdomain)`（注册前），core 自动置 `REGULATORY_CUSTOM_REG`；临时不接 regd 也可只留 `WIPHY_FLAG_SUPPORTS_FW_ROAM` 而不加别名，让默认 regulatory 接管。
