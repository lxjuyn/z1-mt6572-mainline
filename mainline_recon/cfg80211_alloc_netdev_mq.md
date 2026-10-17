# alloc_netdev_mq 4→5 参修复 (gl_init.c:2146)

## 结论

mainline 7.0 里 `alloc_netdev_mq` **仍在**(是宏),但参数从 vendor 3.4.67 的 4 个变成 5 个:mainline 在 `name` 和 `setup` 之间插入了 `name_assign_type`(内核 4.x 引入 NET_NAME 枚举)。底层真函数是 `alloc_netdev_mqs`,多一个 `rxqs` 参数。

```
// mainline fork_linux/include/linux/netdevice.h:4945-4954
struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name,
                unsigned char name_assign_type,
                void (*setup)(struct net_device *),
                unsigned int txqs, unsigned int rxqs);
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup)
        alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, 1, 1)
#define alloc_netdev_mq(sizeof_priv, name, name_assign_type, setup, count)
        alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, count, count)
```

`alloc_netdev_mq` 的第 5 参数是 `count`,同时用作 TX 和 RX 队列数(`txqs=rxqs=count`),不是单独的 TX queue 数。

## 现状调用 (gl_init.c:2146, 4 参)

```c
prGlueInfo->prDevHandler = alloc_netdev_mq(sizeof(P_GLUE_INFO_T), NIC_INF_NAME, ether_setup, CFG_MAX_TXQ_NUM);
```

mainline 编译报:
- `alloc_netdev_mq undeclared`(宏需要 5 参,只给 4 参,宏没展开)
- `macro "alloc_netdev_mq" requires 5 arguments, but only 4 given`

## 改写 (5 参)

在 `name` 和 `setup` 之间插入 `NET_NAME_UNKNOWN`(默认值,表示由 setup 回调或后续指定),保留 `CFG_MAX_TXQ_NUM` 作第 5 参(队列数):

```c
prGlueInfo->prDevHandler = alloc_netdev_mq(sizeof(P_GLUE_INFO_T), NIC_INF_NAME, NET_NAME_UNKNOWN, ether_setup, CFG_MAX_TXQ_NUM);
```

需 `#include <linux/netdevice.h>` 已有(`<net/cfg80211.h>` 间接拉入,显式 include 更稳)。`NET_NAME_UNKNOWN` 定义在 `include/uapi/linux/if.h`,通常随 `netdevice.h` 进入。

## 备选:直接用 alloc_netdev_mqs (txqs != rxqs 时)

若将来要 TX/RX 队列数不同(此处用不到,vendor 一致):

```c
prGlueInfo->prDevHandler = alloc_netdev_mqs(sizeof(P_GLUE_INFO_T), NIC_INF_NAME, NET_NAME_UNKNOWN, ether_setup, CFG_MAX_TXQ_NUM, CFG_MAX_TXQ_NUM);
```

## 备注

- vendor 3.4.67 的 `alloc_netdev_mq(priv, name, setup, count)` 没有 `name_assign_type`,3.4 内核里宏就是这个 4 参形态。
- mainline 其它 MTK 移植代码需同理补给 `NET_NAME_UNKNOWN`,全局搜 `alloc_netdev(` / `alloc_netdev_mq(` 一次性补齐。
- 若 `NIC_INF_NAME` 是 `"wlan%d"` 这类带 `%d` 的格式串,可考虑 `NET_NAME_PREDICTABLE` 或由 driver 自己 `dev_alloc_name`,但 vendor 风格用 `NET_NAME_UNKNOWN` 即可保持原行为。
```

(内容约 110 行,正文 ~200 字)
