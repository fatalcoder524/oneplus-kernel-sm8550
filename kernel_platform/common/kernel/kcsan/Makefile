# SPDX-License-Identifier: GPL-2.0
KCSAN_SANITIZE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n
PGO_PROFILE	:= n

CFLAGS_REMOVE_core.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_debugfs.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_report.o = $(CC_FLAGS_FTRACE)

CFLAGS_core.o := $(call cc-option,-fno-conserve-stack) \
	-fno-stack-protector -DDISABLE_BRANCH_PROFILING

obj-y := core.o debugfs.o report.o
obj-$(CONFIG_KCSAN_SELFTEST) += selftest.o

CFLAGS_kcsan_test.o := $(CFLAGS_KCSAN) -fno-omit-frame-pointer
CFLAGS_kcsan_test.o += $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_KCSAN_KUNIT_TEST) += kcsan_test.o
