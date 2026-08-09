ccflags-y += -I$(srctree)/drivers/misc/mediatek/connectivity/stub_includes
# ---------------------------------------------------
# Compile Options
# ---------------------------------------------------
# MTK_PATH_BUILD include removed (Android build infra; not used by kbuild)
ccflags-y += -DLINUX -DMT6620
# out-of-tree 外编(M=modules)时不会引入内核 autoconf.h, 源码用 #if CONFIG_NL80211_TESTMODE
# (无 CONFIG_ 前缀的整数判) 会因宏未定义走到 #error "ENABLE ... Wi-Fi Direct".
# 这里按主 .config(=y) 把它显式传成宏, 对齐 in-tree 编译行为.
ccflags-y += -DCONFIG_NL80211_TESTMODE=1
# mainline 7.0 netdevice.h 把 struct net_device->ieee80211_ptr 成员包在
# #if IS_ENABLED(CONFIG_CFG80211) 里, 外编 M= 不读根 .config autoconf.h,
# 当前 .config 未设 CONFIG_CFG80211 -> 成员不在 -> gl_init.c:2165/2938/2941 报
# "no member named ieee80211_ptr"。显式传 CONFIG_CFG80211=1 让 IS_ENABLED 按1算。
# CONFIG_WIRELESS_EXT 同理: netdevice.h 用它包 wireless_handlers 等, 顺带给上。
ccflags-y += -DCONFIG_CFG80211=1 -DCONFIG_WIRELESS_EXT=1
# mainline 7.0 把 iw_handler_def 的 num_private/num_private_args/private/private_args
# 成员包在 #ifdef CONFIG_WEXT_PRIV 里(iw_handler.h), 外编不喂 -> gl_wext.c
# wext_handler_def 初始化报 "no member named xxx"。显式喂上。
ccflags-y += -DCONFIG_WEXT_PRIV=1
# mainline 7.0 Kbuild NOSTDINC_FLAGS 只给 -nostdinc 不再喂 gcc 自带 lib include(-isystem),
# 而 vendor 源直接 #include <stddef.h>(非 <linux/stddef.h>) 会找不到 -> fatal error: stddef.h.
# 这里动态取 cross-gcc 自带 include 目录补成 -isystem, 只影响本子树, 不改顶层 Makefile.
ccflags-y += -isystem $(shell $(CROSS_COMPILE)gcc -print-file-name=include)

ifeq ($(MTK_WAPI_SUPPORT), yes)
    ccflags-y += -DCFG_SUPPORT_WAPI=1
else
    ccflags-y += -DCFG_SUPPORT_WAPI=0
endif

ifeq ($(HAVE_XLOG_FEATURE), yes)
    ccflags-y += -DCFG_SUPPORT_XLOG=1
else
    ccflags-y += -DCFG_SUPPORT_XLOG=0
endif

ifeq ($(HAVE_AEE_FEATURE), yes)
    ccflags-y += -DCFG_SUPPORT_AEE=1
else
    ccflags-y += -DCFG_SUPPORT_AEE=0
endif

#ifeq ($(CONFIG_MTK_COMBO_WIFI_HIF_SDIO1), y)
#    ccflags-y += -D_HIF_SDIO=1
#endif

MODULE_NAME := wlan_mt6620

ccflags-y += -D_HIF_SDIO=1

ccflags-y += -DDBG=0
ccflags-y += -I$(src)/os -I$(src)/os/linux/include -I$(src)/os/linux/hif/sdio/include
ccflags-y += -I$(src)/include -I$(src)/include/nic -I$(src)/include/mgmt

# 外编 M=modules 不读根 .config 的 CONFIG_MTK_COMBO_WIFI, obj-$(空) 不编模块,
# 导致 wlan.ko 不生成。直接改 obj-m 无条件编译(外编默认 =m 与 in-tree=y 等效产物)。
obj-m += $(MODULE_NAME).o
#obj-$(CONFIG_MTK_COMBO_WIFI) += $(MODULE_NAME).o

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
COMMON_DIR  := common/
OS_DIR      := os/linux/
HIF_DIR	    := os/linux/hif/sdio/
NIC_DIR     := nic/
MGMT_DIR    := mgmt/


# ---------------------------------------------------
# Objects List
# ---------------------------------------------------

COMMON_OBJS :=	$(COMMON_DIR)dump.o \
	       		$(COMMON_DIR)wlan_lib.o \
	       		$(COMMON_DIR)wlan_oid.o \
	       		$(COMMON_DIR)wlan_bow.o

NIC_OBJS := $(NIC_DIR)nic.o \
			$(NIC_DIR)nic_tx.o \
			$(NIC_DIR)nic_rx.o \
			$(NIC_DIR)nic_pwr_mgt.o \
			$(NIC_DIR)cmd_buf.o \
			$(NIC_DIR)que_mgt.o \
			$(NIC_DIR)nic_cmd_event.o

OS_OBJS :=	$(OS_DIR)gl_init.o \
			$(OS_DIR)gl_kal.o  \
			$(OS_DIR)gl_bow.o \
			$(OS_DIR)gl_wext.o \
			$(OS_DIR)gl_wext_priv.o \
			$(OS_DIR)gl_rst.o \
			$(OS_DIR)gl_cfg80211.o \
			$(OS_DIR)platform.o

MGMT_OBJS := $(MGMT_DIR)ais_fsm.o \
			 $(MGMT_DIR)aaa_fsm.o \
			 $(MGMT_DIR)assoc.o \
			 $(MGMT_DIR)auth.o \
			 $(MGMT_DIR)bss.o \
			 $(MGMT_DIR)cnm.o \
			 $(MGMT_DIR)cnm_timer.o \
			 $(MGMT_DIR)cnm_mem.o \
			 $(MGMT_DIR)hem_mbox.o \
			 $(MGMT_DIR)mib.o \
			 $(MGMT_DIR)privacy.o  \
			 $(MGMT_DIR)rate.o \
			 $(MGMT_DIR)rlm.o \
			 $(MGMT_DIR)rlm_domain.o \
			 $(MGMT_DIR)rlm_obss.o \
			 $(MGMT_DIR)rlm_protection.o \
			 $(MGMT_DIR)rsn.o \
			 $(MGMT_DIR)saa_fsm.o \
			 $(MGMT_DIR)scan.o \
			 $(MGMT_DIR)scan_fsm.o \
		 	 $(MGMT_DIR)sec_fsm.o \
             $(MGMT_DIR)swcr.o \
             $(MGMT_DIR)roaming_fsm.o
                         
# ---------------------------------------------------
# P2P Objects List
# ---------------------------------------------------      

COMMON_OBJS += $(COMMON_DIR)wlan_p2p.o

NIC_OBJS += $(NIC_DIR)p2p_nic.o

OS_OBJS += $(OS_DIR)gl_p2p.o \
           $(OS_DIR)gl_p2p_cfg80211.o \
           $(OS_DIR)gl_p2p_init.o \
           $(OS_DIR)gl_p2p_kal.o

MGMT_OBJS += $(MGMT_DIR)p2p_assoc.o \
             $(MGMT_DIR)p2p_bss.o \
             $(MGMT_DIR)p2p_fsm.o \
             $(MGMT_DIR)p2p_func.o \
             $(MGMT_DIR)p2p_rlm.o \
             $(MGMT_DIR)p2p_rlm_obss.o \
             $(MGMT_DIR)p2p_scan.o \
             $(MGMT_DIR)p2p_ie.o \
             $(MGMT_DIR)p2p_state.o        
                 

ifeq ($(MTK_WAPI_SUPPORT), yes)
MGMT_OBJS += $(MGMT_DIR)wapi.o
endif

ifeq ($(WLAN_PROC), y)
OS_OBJS += gl_proc.o
endif

HIF_OBJS :=  $(HIF_DIR)arm.o \
             $(HIF_DIR)sdio.o

$(MODULE_NAME)-objs  += $(COMMON_OBJS)
$(MODULE_NAME)-objs  += $(NIC_OBJS)
$(MODULE_NAME)-objs  += $(OS_OBJS)
$(MODULE_NAME)-objs  += $(HIF_OBJS)
$(MODULE_NAME)-objs  += $(MGMT_OBJS)


