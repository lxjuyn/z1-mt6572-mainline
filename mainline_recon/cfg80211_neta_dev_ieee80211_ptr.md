# net_device.ieee80211_ptr 适配(mainline 7.0)

## 事实(grep 结果)

`include/linux/netdevice.h`:
- L1924 `@ieee80211_ptr: IEEE 802.11 specific data, assign before registering`
- L2325 `struct wireless_dev *ieee80211_ptr;` — **成员仍在**

`include/net/cfg80211.h`:引用 `ieee80211_ptr` 多处(注释 + `cfg80211_unregister_wdev(dev->ieee80211_ptr)`),**无** `netdev_to_wdev`、无 `netdev_assign_ieee80211`,mainline 此树直接走成员访问。

vendor `drivers/.../gl_init.c`:无 `ieee80211_ptr` 命中(MTK 该路径未直接用 prDev->ieee80211_ptr,但其它 vendor 源常用)。

## 回答

mainline 7.0 的 `struct net_device` **仍保留** `ieee80211_ptr` 成员。关联到 `wireless_dev` 直接用 `dev->ieee80211_ptr`,无需替代 API(本树无 `netdev_to_wdev`)。

## 适配宏

```c
/* 适配:vendor 旧 prDev->ieee80211_ptr,mainline 成员仍在,直接透传 */
#define ieee80211_ptr(prDev)  ((prDev)->ieee80211_ptr)
```
