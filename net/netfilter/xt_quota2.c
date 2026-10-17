// SPDX-License-Identifier: GPL-2.0-only
/* Android quota2 revision 3 ABI, derived from Jan Engelhardt's xt_quota2
 * (2008) and Android's named quota extension. Modern procfs/per-net lifetime
 * handling and asynchronous quota-limit uevents for Pie NetlinkHandler.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_quota2.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct quota2_net {
 struct mutex mutex;
 struct list_head counters;
 struct proc_dir_entry *dir;
};
#define QUOTA2_MAX_PENDING_EVENTS 64
struct quota2_event {
 struct list_head list;
 char iface[IFNAMSIZ];
};
struct xt_quota_counter {
 struct list_head list;
 spinlock_t lock;
 u64 quota;
 unsigned int refs;
 bool notify_host;
 bool alerted;
 char name[15];
 struct list_head events;
 unsigned int event_count;
 unsigned long dropped_events;
 struct proc_dir_entry *proc;
 struct work_struct notify;
};
static unsigned int quota2_net_id;
static struct class *quota2_class;
static struct device *quota2_device;

static void quota2_notify(struct work_struct *work)
{
 struct xt_quota_counter *e = container_of(work, struct xt_quota_counter, notify);
 struct quota2_event *event, *tmp;
 LIST_HEAD(events);
 unsigned long flags;
 spin_lock_irqsave(&e->lock, flags);
 list_splice_init(&e->events, &events);
 e->event_count = 0;
 spin_unlock_irqrestore(&e->lock, flags);
 list_for_each_entry_safe(event, tmp, &events, list) {
  char alert[sizeof("ALERT_NAME=") + sizeof(e->name)];
  char iface[sizeof("INTERFACE=") + IFNAMSIZ];
  char *env[] = { alert, iface, NULL };
  snprintf(alert, sizeof(alert), "ALERT_NAME=%s", e->name);
  snprintf(iface, sizeof(iface), "INTERFACE=%s", event->iface);
  kobject_uevent_env(&quota2_device->kobj, KOBJ_CHANGE, env);
  list_del(&event->list);
  kfree(event);
 }
}
/* Counter lock held. Each rearm gets an immutable event even when work
 * scheduling coalesces. A failed enqueue leaves the alert armed for retry. */
static bool quota2_queue_alert(struct xt_quota_counter *e,
                              const struct xt_action_param *par)
{
 struct quota2_event *event;
 const struct net_device *dev = xt_out(par) ?: xt_in(par);
 if (e->event_count >= QUOTA2_MAX_PENDING_EVENTS) {
  e->dropped_events++;
  pr_warn_ratelimited("quota2 %s: queue full, dropped=%lu\n",
                      e->name, e->dropped_events);
  return false;
 }
 event = kmalloc(sizeof(*event), GFP_ATOMIC);
 if (!event) {
  e->dropped_events++;
  pr_warn_ratelimited("quota2 %s: event allocation failed, dropped=%lu\n",
                      e->name, e->dropped_events);
  return false;
 }
 strscpy(event->iface, dev ? dev->name : "", sizeof(event->iface));
 list_add_tail(&event->list, &e->events);
 e->event_count++;
 schedule_work(&e->notify);
 return true;
}
static int quota2_show(struct seq_file *m, void *v)
{
 struct xt_quota_counter *e = m->private;
 u64 quota;
 spin_lock_bh(&e->lock);
 quota = e->quota;
 spin_unlock_bh(&e->lock);
 seq_printf(m, "%llu\n", quota);
 return 0;
}
static int quota2_open(struct inode *inode, struct file *file)
{
 return single_open(file, quota2_show, pde_data(inode));
}
static ssize_t quota2_write(struct file *file, const char __user *buf,
                           size_t len, loff_t *pos)
{
 struct xt_quota_counter *e = pde_data(file_inode(file));
 u64 quota;
 int ret = kstrtoull_from_user(buf, len, 10, &quota);
 if (ret)
  return ret;
 spin_lock_bh(&e->lock);
 e->quota = quota;
 e->alerted = false;
 spin_unlock_bh(&e->lock);
 return len;
}
static const struct proc_ops quota2_ops = {
 .proc_open = quota2_open,
 .proc_read = seq_read,
 .proc_lseek = seq_lseek,
 .proc_release = single_release,
 .proc_write = quota2_write,
};
static int quota2_check(const struct xt_mtchk_param *par)
{
 struct xt_quota_mtinfo2 *q = par->matchinfo;
 struct quota2_net *qn = net_generic(par->net, quota2_net_id);
 struct xt_quota_counter *e;
 int ret = 0;
 if (q->flags & ~XT_QUOTA_MASK)
  return -EINVAL;
 if (strnlen(q->name, sizeof(q->name)) == sizeof(q->name) ||
     q->name[0] == '.' || strchr(q->name, '/'))
  return -EINVAL;
 mutex_lock(&qn->mutex);
 if (q->name[0]) {
  list_for_each_entry(e, &qn->counters, list) {
   if (!strcmp(e->name, q->name)) {
    e->refs++;
    q->master = e;
    goto out;
   }
  }
 }
 e = kzalloc(sizeof(*e), GFP_KERNEL);
 if (!e) {
  ret = -ENOMEM;
  goto out;
 }
 spin_lock_init(&e->lock);
 INIT_WORK(&e->notify, quota2_notify);
 INIT_LIST_HEAD(&e->events);
 e->quota = q->quota;
 e->refs = 1;
 e->notify_host = net_eq(par->net, &init_net);
 strscpy(e->name, q->name, sizeof(e->name));
 if (q->name[0]) {
  e->proc = proc_create_data(e->name, 0644, qn->dir, &quota2_ops, e);
  if (!e->proc) {
   kfree(e);
   ret = -ENOMEM;
   goto out;
  }
  /* Pie's network_stack/netd writes quota updates as root. */
  list_add_tail(&e->list, &qn->counters);
 }
 q->master = e;
out:
 mutex_unlock(&qn->mutex);
 return ret;
}
static void quota2_destroy(const struct xt_mtdtor_param *par)
{
 const struct xt_quota_mtinfo2 *q = par->matchinfo;
 struct quota2_net *qn = net_generic(par->net, quota2_net_id);
 struct xt_quota_counter *e = q->master;
 mutex_lock(&qn->mutex);
 if (--e->refs) {
  mutex_unlock(&qn->mutex);
  return;
 }
 if (e->name[0]) {
  list_del(&e->list);
  /* proc_remove drains active proc operations before freeing counter data. */
  proc_remove(e->proc);
 }
 cancel_work_sync(&e->notify);
 while (!list_empty(&e->events)) {
  struct quota2_event *event = list_first_entry(&e->events, struct quota2_event, list);
  list_del(&event->list);
  kfree(event);
 }
 kfree(e);
 mutex_unlock(&qn->mutex);
}
static bool quota2_match(const struct sk_buff *skb, struct xt_action_param *par)
{
 const struct xt_quota_mtinfo2 *q = par->matchinfo;
 struct xt_quota_counter *e = q->master;
 u64 cost = (q->flags & XT_QUOTA_PACKET) ? 1 : skb->len;
 bool ret = !!(q->flags & XT_QUOTA_INVERT);
 spin_lock_bh(&e->lock);
 if (q->flags & XT_QUOTA_GROW) {
  if (!(q->flags & XT_QUOTA_NO_CHANGE)) {
   e->quota += cost;
   if (e->quota)
    e->alerted = false;
  }
  ret = true;
 } else if (e->quota >= cost) {
  if (!(q->flags & XT_QUOTA_NO_CHANGE))
   e->quota -= cost;
  ret = !ret;
 } else {
  /* Alert once on the first over-limit packet, including an initially
   * zero quota or the packet after an exact debit to zero. Proc writes
   * rearm this latch. Pure NO_CHANGE comparisons neither notify nor
   * consume the latch intended for the actual accounting rule. */
  if (!(q->flags & XT_QUOTA_NO_CHANGE) && !e->alerted &&
      e->name[0] && e->notify_host) {
   e->alerted = quota2_queue_alert(e, par);
  }
  if (!(q->flags & XT_QUOTA_NO_CHANGE))
   e->quota = 0;
 }
 spin_unlock_bh(&e->lock);
 return ret;
}
static struct xt_match quota2_matches[] __read_mostly = {
 { .name = "quota2", .revision = 3, .family = NFPROTO_IPV4,
   .checkentry = quota2_check, .match = quota2_match, .destroy = quota2_destroy,
   .matchsize = sizeof(struct xt_quota_mtinfo2),
   .usersize = offsetof(struct xt_quota_mtinfo2, master), .me = THIS_MODULE },
 { .name = "quota2", .revision = 3, .family = NFPROTO_IPV6,
   .checkentry = quota2_check, .match = quota2_match, .destroy = quota2_destroy,
   .matchsize = sizeof(struct xt_quota_mtinfo2),
   .usersize = offsetof(struct xt_quota_mtinfo2, master), .me = THIS_MODULE },
};
static int __net_init quota2_net_init(struct net *net)
{
 struct quota2_net *qn = net_generic(net, quota2_net_id);
 mutex_init(&qn->mutex);
 INIT_LIST_HEAD(&qn->counters);
 qn->dir = proc_net_mkdir(net, "xt_quota", net->proc_net);
 return qn->dir ? 0 : -ENOMEM;
}
static void __net_exit quota2_net_exit(struct net *net)
{
 struct quota2_net *qn = net_generic(net, quota2_net_id);
 struct xt_quota_counter *e;
 /* Namespace exit precedes table rule destruction. Drain proc readers now,
  * retain counters until their last rule reference is destroyed. */
 mutex_lock(&qn->mutex);
 list_for_each_entry(e, &qn->counters, list) {
  proc_remove(e->proc);
  e->proc = NULL;
 }
 proc_remove(qn->dir);
 qn->dir = NULL;
 mutex_unlock(&qn->mutex);
}
static struct pernet_operations quota2_net_ops = {
 .init = quota2_net_init, .exit = quota2_net_exit,
 .id = &quota2_net_id, .size = sizeof(struct quota2_net),
};
static int __init quota2_init(void)
{
 int ret;
 quota2_class = class_create("xt_quota2");
 if (IS_ERR(quota2_class))
  return PTR_ERR(quota2_class);
 quota2_device = device_create(quota2_class, NULL, MKDEV(0, 0), NULL, "quotas");
 if (IS_ERR(quota2_device)) {
  ret = PTR_ERR(quota2_device);
  class_destroy(quota2_class);
  return ret;
 }
 ret = register_pernet_subsys(&quota2_net_ops);
 if (ret)
  goto put;
 ret = xt_register_matches(quota2_matches, ARRAY_SIZE(quota2_matches));
 if (!ret)
  return 0;
 unregister_pernet_subsys(&quota2_net_ops);
put:
 device_unregister(quota2_device);
 class_destroy(quota2_class);
 return ret;
}
static void __exit quota2_exit(void)
{
 xt_unregister_matches(quota2_matches, ARRAY_SIZE(quota2_matches));
 unregister_pernet_subsys(&quota2_net_ops);
 device_unregister(quota2_device);
 class_destroy(quota2_class);
}
module_init(quota2_init);
module_exit(quota2_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Android named quota2 counters and quota-limit uevents");
MODULE_ALIAS("ipt_quota2");
MODULE_ALIAS("ip6t_quota2");
