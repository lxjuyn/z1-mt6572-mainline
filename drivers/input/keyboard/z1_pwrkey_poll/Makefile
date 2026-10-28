# SPDX-License-Identifier: GPL-2.0-only
#
# Fallback polled PMIC power-key driver for the MT6572 Z1 board.
#
# This directory is deliberately NOT referenced from drivers/input/keyboard/Kconfig
# or drivers/input/keyboard/Makefile, so it can never end up in a kernel image by
# accident.  Build it on demand:
#
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
#        M=drivers/input/keyboard/z1_pwrkey_poll KBUILD_MODPOST_WARN=1 modules
#
# product: drivers/input/keyboard/z1_pwrkey_poll/z1_pmic_pwrkey_poll.ko
#
# insmod on the board (only if the normal PMIC-IRQ path failed):
#   insmod z1_pmic_pwrkey_poll.ko

obj-m += z1_pmic_pwrkey_poll.o
