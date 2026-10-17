# cfg80211_ops 签名不匹配分析:MT6620 vendor → mainline 7.0-rc7

移植 MTK 3.4.67 vendor WiFi driver (MT6620) 到 mainline 7.0-rc7 时,`gl_init.c:840-862` 一组 `cfg80211_ops` 初始化报 15+ 处 `incompatible pointer type`。根因:vendor 回调签名(老 cfg80211 API)与 mainline 7.0 `struct cfg80211_ops` 成员签名不匹配。

- mainline 定义源:`fork_linux/include/net/cfg80211.h:4901`
- vendor 初始化:`fork_linux/drivers/misc/mediatek/connectivity/drv_wlan/mt6620/wlan/os/linux/gl_init.c:838-862`
- vendor 回调声明:`.../mt6620/wlan/os/linux/include/gl_cfg80211.h:74-256`

## 不一致回调对照表

| .成员名 | vendor 签名(简) | mainline 7.0-rc7 签名(简) | 适配方式 |
|---|---|---|---|
| `.change_virtual_intf` | `(wiphy, net_device*, nl80211_iftype, u32 \*flags, vif_params*)` | `(wiphy, net_device*, nl80211_iftype, vif_params*)` | 参数减:删 vendor 的 `u32 *flags`,直接改签名 |
| `.add_key` | `(wiphy, net_device*, u8 idx, bool pairwise, const u8\* mac, key_params*)` | `(wiphy, net_device*, int link_id, u8 idx, bool pairwise, const u8\* mac, key_params*)` | 参数增:vendor 在 `ndev` 后插入 `int link_id`(可传 0),改签名 |
| `.get_key` | `(wiphy, net_device*, u8 idx, bool pairwise, const u8\* mac, void\* cookie, callback)` | `(wiphy, net_device*, int link_id, u8 idx, bool pairwise, const u8\* mac, void\* cookie, callback)` | 同上,插 `int link_id`=0 |
| `.del_key` | `(wiphy, net_device*, u8 idx, bool pairwise, const u8\* mac)` | `(wiphy, net_device*, int link_id, u8 idx, bool pairwise, const u8\* mac)` | 同上,插 `int link_id`=0 |
| `.set_default_key` | `(wiphy, net_device*, u8 idx, bool unicast, bool multicast)` | `(wiphy, net_device*, int link_id, u8 idx, bool unicast, bool multicast)` | 同上,插 `int link_id`=0 |
| `.get_station` | `(wiphy, net_device*, u8\* mac, station_info*)` | `(wiphy, net_device*, const u8\* mac, station_info*)` | const 化:`mac` 加 `const`,改签名 |
| `.scan` | `(wiphy, net_device*, cfg80211_scan_request*)` | `(wiphy, cfg80211_scan_request*)` | 参数减:删 `net_device*`(mainline scan 无 ndev),改签名;体内用 `request->wdev->netdev` 取 ndev |
| `.connect` | `(wiphy, net_device*, cfg80211_connect_params*)` | `(wiphy, net_device*, cfg80211_connect_params*)` | 一致,无需改(报错来自其它项) |
| `.disconnect` | `(wiphy, net_device*, u16 reason)` | `(wiphy, net_device*, u16 reason)` | 一致,无需改 |
| `.join_ibss` / `.leave_ibss` | `(wiphy, net_device*, ...)` | 同 vendor | 一致 |
| `.set_power_mgmt` | `(wiphy, net_device*, bool, int)` | 同 vendor | 一致 |
| `.set_pmksa` / `.del_pmksa` / `.flush_pmksa` | `(wiphy, net_device*, pmksa*)` | 同 vendor | 一致 |
| `.remain_on_channel` | `(wiphy, net_device*, ieee80211_channel\*, nl80211_channel_type, uint, u64\*)` | `(wiphy, wireless_dev\*, ieee80211_channel\*, uint, u64\*)` | `net_device*`→`wireless_dev*`,删 `channel_type` 枚举;用 wrapper 桥接:wrapper 收 `wdev`,从 `wdev->netdev` 取 ndev 再调原函数 |
| `.cancel_remain_on_channel` | `(wiphy, net_device*, u64 cookie)` | `(wiphy, wireless_dev*, u64 cookie)` | `net_device*`→`wireless_dev*`;wrapper 取 `wdev->netdev` |
| `.mgmt_tx` | `(wiphy, net_device*, ieee80211_channel\*, bool offscan, nl80211_channel_type, bool ctype_valid, uint wait, const u8\* buf, size_t len, bool no_cck, bool no_wait_ack, u64\*)` | `(wiphy, wireless_dev*, cfg80211_mgmt_tx_params*, u64\*)` | 参数剧变:ndev→wdev + 11 个零散参压成 `cfg80211_mgmt_tx_params*`;用 wrapper 桥接,从 params 拆字段再调原函数 |
| `.mgmt_tx_cancel_wait` | `(wiphy, net_device*, u64 cookie)` | `(wiphy, wireless_dev*, u64 cookie)` | `net_device*`→`wireless_dev*`;wrapper 取 `wdev->netdev` |
| `.testmode_cmd` | `(wiphy, void\* data, int len)` | `(wiphy, wireless_dev*, void\* data, int len)` | 参数增:vendor 无 wdev,mainline 中间插 `wireless_dev*`;wrapper 收 wdev 忽略之再调原函数(或直接改签名加 `wdev` 形参不用) |

## 主体结论

共 13 个回调需改(其中 `.connect/.disconnect/.join_ibss/.leave_ibss/.set_power_mgmt/.set_pmksa/.del_pmksa/.flush_pmksa` 8 项签名已一致,不在改动列)。

主体矛盾是两类:
1. **参数增减(占大头,8 项)**:`add_key/get_key/del_key/set_default_key` 多了 `int link_id`;`change_virtual_intf` 少了 `u32 *flags`;`scan` 少了 `net_device*`;`get_station` 仅 const 化;`testmode_cmd` 多了 `wireless_dev*`。这些直接改 vendor 回调签名即可,`link_id` 一律传 0(const 老 driver 无 MLO 概念)。
2. **`net_device*`→`wireless_dev*`(4 项)**:`remain_on_channel`/`cancel_remain_on_channel`/`mgmt_tx`/`mgmt_tx_cancel_wait`。优先用 wrapper 函数桥接:wrapper 按 mainline 签名收 wdev,从 `wdev->netdev` 取回 ndev 再调 vendor 原函数(原函数体几乎不动),这样 vendor 业务逻辑零侵入,分离 API 适配层。`mgmt_tx` 参数结构差异最大,wrapper 内需从 `cfg80211_mgmt_tx_params` 把 `chan/offscan/ctype/wait/buf/no_cck/dont_wait_for_ack` 逐字段拆出传给原 11 参函数。
