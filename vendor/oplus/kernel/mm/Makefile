# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2018-2020 Oplus. All rights reserved.

#ifdef OPLUS_FEATURE_ZRAM_OPT
obj-$(CONFIG_OPLUS_FEATURE_ZRAM_OPT) 	+= zram_opt/
#endif /* CONFIG_OPLUS_FEATURE_ZRAM_OPT */

#ifdef OPLUS_FEATURE_PROCESS_RECLAIM
obj-$(CONFIG_PROCESS_RECLAIM_ENHANCE)	+= process_reclaim/
#endif /* OPLUS_FEATURE_PROCESS_RECLAIM */

#ifdef OPLUS_FEATURE_PROACTIVE_COMPACT
obj-$(CONFIG_PROACTIVE_COMPACT) += proactive_compact/
#endif /* OPLUS_FEATURE_PROACTIVE_COMPACT */

#ifdef OPLUS_FEATURE_MEMLEAK_DETECT
obj-$(CONFIG_DUMP_TASKS_MEM)	+= dump_tasks_mem/
#endif /* OPLUS_FEATURE_MEMLEAK_DETECT */

obj-$(CONFIG_THP_ZSMALLOC) += thp_zsmalloc/

#ifdef OPLUS_FEATURE_HYBRIDSWAP_ZRAM
obj-$(CONFIG_HYBRIDSWAP_ZRAM) += hybridswap_zram/
#endif

obj-y += mm_osvelte/
obj-$(CONFIG_KSHRINK_SLABD) += async_reclaim_opt/kshrink_slabd/
obj-$(CONFIG_PCPPAGES_OPT) += async_reclaim_opt/pcppages_opt/
obj-$(CONFIG_ABORT_MM_OPT) += async_reclaim_opt/abort_mm_opt/
obj-$(CONFIG_KSHRINK_LRUVECD) += async_reclaim_opt/kshrink_lruvecd/
obj-$(CONFIG_MAPPED_PROTECT) += memload_opt/mapped_protect/

obj-$(CONFIG_OPLUS_FEATURE_SIGKILL_DIAGNOSIS) += sigkill_diagnosis/

obj-y += uxmem_opt/
obj-y += dynamic_readahead/
obj-y += kswapd_opt/
obj-$(CONFIG_LOOK_AROUND) += memload_opt/look_around/
