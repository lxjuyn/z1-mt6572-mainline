/* SPDX-License-Identifier: GPL-2.0 */
/* STUB: Android wake_lock removed from mainline Linux (4.17+).
 * Provide empty type + noop API so legacy code compiles. Real PM
 * integration deferred until functional phase. */
#ifndef _LINUX_WAKELOCK_H_STUB
#define _LINUX_WAKELOCK_H_STUB

#include <linux/types.h>

enum {
	WAKE_LOCK_SUSPEND = 0,
	WAKE_LOCK_IDLE    = 1,
};

struct wake_lock {
	const char *name;
};

static inline int wake_lock_init(struct wake_lock *lock, int type, const char *name)
{
	(void)type; lock->name = name; return 0;
}

static inline void wake_lock_destroy(struct wake_lock *lock) { (void)lock; }
static inline void wake_lock(struct wake_lock *lock)        { (void)lock; }
static inline void wake_unlock(struct wake_lock *lock)      { (void)lock; }
static inline int  wake_lock_active(struct wake_lock *lock) { return 0; (void)lock; }
static inline int  has_wake_lock(int type)                  { return 0; (void)type; }

#endif /* _LINUX_WAKELOCK_H_STUB */
