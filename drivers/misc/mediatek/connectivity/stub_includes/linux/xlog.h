/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: Android xlog printk wrapper redirecting to standard pr_<level>. */
#ifndef _LINUX_XLOG_H_STUB
#define _LINUX_XLOG_H_STUB

#include <linux/printk.h>

#define XLOG_TAG_STA "sta"
#define XLOG_TAG_P2P "p2p"

/* Map the (name, ...) signature used in legacy code. */
#define XLOG_INFO(tag, ...)    pr_info("[%s] " pr_fmt(__VA_ARGS__), tag)
#define XLOG_WARN(tag, ...)    pr_warn("[%s] " pr_fmt(__VA_ARGS__), tag)
#define XLOG_ERR(tag, ...)     pr_err("[%s] " pr_fmt(__VA_ARGS__), tag)
#define XLOG_DBG(tag, ...)     pr_debug("[%s] " pr_fmt(__VA_ARGS__), tag)
#define XLOG_TRACE(tag, ...)   pr_devel("[%s] " pr_fmt(__VA_ARGS__), tag)

/* Some legacy call sites use bare printk-style. */
#define LOG_I(tag, ...)       pr_info("[%s] ", tag)
#define LOG_W(tag, ...)       pr_warn("[%s] ", tag)
#define LOG_E(tag, ...)       pr_err("[%s] ", tag)
#define LOG_D(tag, ...)       pr_debug("[%s] ", tag)

#endif /* _LINUX_XLOG_H_STUB */
