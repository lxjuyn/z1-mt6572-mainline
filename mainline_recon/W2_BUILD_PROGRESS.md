## 2026-07-10 17:58:58 — stub headers batch (wakelock/earlysuspend/aee/xlog expand)

- Added stub linux/wakelock.h, linux/earlysuspend.h, expanded linux/aee.h+xlog.h
  in drivers/misc/mediatek/connectivity/stub_includes/linux/
- Backups: /home/<user>/claude/6572/bridge_backups/wifi_w2_stubs_*/*.bak
- Next: re-run make modules, expect init_timer/timer_setup + work_struct + unlocked_ioctl + procfs issues.



## 2026-07-10_20:15:57 — wmt_dev.c mainline API墙亲手全清
- 4处全清: procfs(create_proc_entry->proc_create+proc_ops+seq_file adapter), read_file(f_op->read/llseek->kernel_read+file_inode+i_size_read), patch_get(删整套set_fs/get_ds/cred fsuid覆盖), timeval(do_gettimeofday->ktime_get_real_ts64/timespec64). make 0 error过此文件.
- 已编出 common/core/*.o 7个 + mtk_stp_wmt.mod 半成.
- 下个卡文件 wmt_tm.c 撞 thermal_zone_device_ops ABI墙+xlog_printk+del_timer+send_sig_info (约20类, 大部分MTK thermal框架重写过).

