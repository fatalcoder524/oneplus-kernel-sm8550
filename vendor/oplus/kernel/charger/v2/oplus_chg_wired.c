// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2024 . Oplus All rights reserved.
 */

#define pr_fmt(fmt) "[CHG_WIRED]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#endif

#include <oplus_chg.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_strategy.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_wired.h>
#include <oplus_chg_cpa.h>
#include <oplus_chg_state_retention.h>
#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#include "oplus_cfg.h"
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <mt-plat/mtk_boot_common.h>
#endif


#define PDQC_CONFIG_WAIT_TIME_MS	15000
#define QC_CHECK_WAIT_TIME_MS		20000
#define PD_CHECK_WAIT_TIME_MS		1500
#define WIRED_COOL_DOWN_LEVEL_MAX	8
#define FACTORY_MODE_PDQC_9V_THR	4100
#define PDQC_BUCK_DEF_CURR_MA		500
#define PDQC_BUCK_VBUS_THR		7500
#define OPLUS_CHG_500_CHARGING_CURRENT	500
#define OPLUS_CHG_900_CHARGING_CURRENT	900
#define OPLUS_CHG_VBUS_5V		5000
#define OPLUS_CHG_VBUS_9V		9000
#define OPLUS_CHG_SHUTDOWN_WAIT		100
#define PDQC_SALE_MODE_CURR_LIMIT_MA	1200
#define PDQC_SALE_MODE_ALLOW_BUCK_MV	5000
#define DPQC_CONNECT_ERROR_COUNT_LEVEL	3
#define WAIT_BC1P2_GET_TYPE 600

struct oplus_wired_spec_config {
	int32_t pd_iclmax_ma;
	int32_t qc_iclmax_ma;
	int32_t non_standard_ibatmax_ma;
	int32_t input_power_mw[OPLUS_WIRED_CHG_MODE_MAX];
	int32_t led_on_fcc_max_ma[TEMP_REGION_MAX];
	int32_t fcc_ma[2][OPLUS_WIRED_CHG_MODE_MAX][TEMP_REGION_MAX];
	int32_t vbatt_pdqc_to_5v_thr;
	int32_t vbatt_pdqc_to_9v_thr;
	int32_t cool_down_pdqc_vol_mv[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_pdqc_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_vooc_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_normal_curr_ma[WIRED_COOL_DOWN_LEVEL_MAX];
	int32_t cool_down_sale_pdqc_vol_mv;
	int32_t cool_down_sale_pdqc_curr_ma;
	int32_t cool_down_pdqc_level_max;
	int32_t cool_down_vooc_level_max;
	int32_t cool_down_normal_level_max;
	int32_t vbus_uv_thr_mv[OPLUS_VBUS_MAX];
	int32_t vbus_ov_thr_mv[OPLUS_VBUS_MAX];
} __attribute__((packed));

struct oplus_wired_config {
	uint8_t *strategy_name[OPLUS_WIRED_CHG_MODE_MAX];
	uint8_t *strategy_data[OPLUS_WIRED_CHG_MODE_MAX];
	uint32_t strategy_data_size[OPLUS_WIRED_CHG_MODE_MAX];
} __attribute__((packed));

struct oplus_chg_wired {
	struct device *dev;
	struct oplus_mms *wired_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *vooc_topic;
	struct oplus_mms *cpa_topic;
	struct oplus_mms *retention_topic;
	struct mms_subscribe *retention_subs;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *comm_subs;
	struct mms_subscribe *vooc_subs;
	struct mms_subscribe *cpa_subs;

	struct oplus_wired_spec_config spec;
	struct oplus_wired_config config;

	struct oplus_chg_strategy *strategy[OPLUS_WIRED_CHG_MODE_MAX];

	struct work_struct gauge_update_work;
	struct work_struct plugin_work;
	struct work_struct chg_type_change_work;
	struct work_struct temp_region_update_work;
	struct work_struct charger_current_changed_work;
	struct work_struct led_on_changed_work;
	struct work_struct icl_changed_work;
	struct work_struct fcc_changed_work;
	struct work_struct qc_check_work;
	struct work_struct pd_check_work;
	struct work_struct sale_mode_buckboost_work;
	struct delayed_work retention_disconnect_work;
	struct delayed_work switch_end_recheck_work;
	struct delayed_work pd_config_work;
	struct delayed_work qc_config_work;
	struct delayed_work pd_boost_icl_disable_work;

	struct power_supply *usb_psy;
	struct power_supply *batt_psy;

	struct votable *fcc_votable;
	struct votable *icl_votable;
	struct votable *input_suspend_votable;
	struct votable *output_suspend_votable;
	struct votable *pd_svooc_votable;
	struct votable *vooc_disable_votable;
	struct votable *pd_boost_disable_votable;
	struct votable *vooc_chg_auto_mode_votable;

	struct completion qc_action_ack;
	struct completion pd_action_ack;
	struct completion qc_check_ack;
	struct completion pd_check_ack;

	bool unwakelock_chg;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *suspend_ws;
#endif

	bool chg_online;
	bool irq_plugin;
	bool vooc_support;
	bool retention_state;
	bool disconnect_change;
	bool retention_state_ready;
	bool adjust_pdqc_vol_thr_support;
	bool authenticate;
	bool hmac;
	bool vooc_started;
	bool pd_boost_disable;
	bool cpa_support;

	int chg_type;
	int vbus_set_mv;
	int vbus_mv;
	int vbat_mv;
	int pdqc_connect_error_count;
	int pdqc_connect_error_count_level;
	enum oplus_chg_protocol_type cpa_current_type;
	enum oplus_temp_region temp_region;
	enum oplus_wired_charge_mode chg_mode;
	enum comm_topic_item fcc_gear;
	enum oplus_wired_action qc_action;
	enum oplus_wired_action pd_action;
	int cool_down;
	int chg_ctrl_by_sale_mode;
	int pd_retry_count;
	int qc_retry_count;
	unsigned int err_code;
	struct mutex icl_lock;
	struct mutex current_lock;

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	struct oplus_cfg spec_debug_cfg;
	struct oplus_cfg normal_debug_cfg;
#endif
};

/* default parameters used when dts is not configured */
static struct oplus_wired_spec_config default_config = {
	.pd_iclmax_ma = 3000,
	.qc_iclmax_ma = 2000,
	.cool_down_sale_pdqc_vol_mv = 9000,
	.cool_down_sale_pdqc_curr_ma = 1200,
	.input_power_mw = {
		2500, 2500, 7500, 10000, 18000, 18000, 18000
	},
	.led_on_fcc_max_ma = { 0, 540, 2000, 2500, 2500, 2500, 2500, 500, 0 },
	.fcc_ma = {
		{
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_UNKNOWN */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_SDP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_CDP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_DCP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_VOOC */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_QC */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_PD */
		},
		{
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_UNKNOWN */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_SDP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_CDP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_DCP */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_VOOC */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_QC */
			{ 0, 0, 500, 500, 500, 500, 500, 500, 0},	/* OPLUS_WIRED_CHG_MODE_PD */
		}
	}
};

static const char *const oplus_wired_chg_mode_text[] = {
	[OPLUS_WIRED_CHG_MODE_UNKNOWN] = "unknown",
	[OPLUS_WIRED_CHG_MODE_SDP] = "sdp",
	[OPLUS_WIRED_CHG_MODE_CDP] = "cdp",
	[OPLUS_WIRED_CHG_MODE_DCP] = "dcp",
	[OPLUS_WIRED_CHG_MODE_VOOC] = "vooc",
	[OPLUS_WIRED_CHG_MODE_QC] = "qc",
	[OPLUS_WIRED_CHG_MODE_PD] = "pd",
	[OPLUS_WIRED_CHG_MODE_MAX] = "invalid",
};

__maybe_unused static bool is_usb_psy_available(struct oplus_chg_wired *chip)
{
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	return !!chip->usb_psy;
}

__maybe_unused static bool is_batt_psy_available(struct oplus_chg_wired *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");
	return !!chip->batt_psy;
}

__maybe_unused static bool
is_pd_svooc_votable_available(struct oplus_chg_wired *chip)
{
	if (!chip->pd_svooc_votable)
		chip->pd_svooc_votable = find_votable("PD_SVOOC");
	return !!chip->pd_svooc_votable;
}

__maybe_unused static bool
is_vooc_disable_votable_available(struct oplus_chg_wired *chip)
{
	if (!chip->vooc_disable_votable)
		chip->vooc_disable_votable = find_votable("VOOC_DISABLE");
	return !!chip->vooc_disable_votable;
}

__maybe_unused static bool
is_vooc_chg_auto_mode_votable_available(struct oplus_chg_wired *chip)
{
	if (!chip->vooc_chg_auto_mode_votable)
		chip->vooc_chg_auto_mode_votable =
			find_votable("VOOC_CHG_AUTO_MODE");
	return !!chip->vooc_chg_auto_mode_votable;
}

static const char *
oplus_wired_get_chg_mode_region_str(enum oplus_wired_charge_mode mode)
{
	return oplus_wired_chg_mode_text[mode];
}

static void oplus_wired_awake_init(struct oplus_chg_wired *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND,
		       "wired wakelock");
#else
	chip->suspend_ws = wakeup_source_register(NULL, "wired wakelock");
#endif
}

static void oplus_wired_awake_exit(struct oplus_chg_wired *chip)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_destroy(&chip->suspend_lock);
#else
	wakeup_source_unregister(chip->suspend_ws);
#endif
}

static void oplus_wired_set_awake(struct oplus_chg_wired *chip, bool awake)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (chip->unwakelock_chg && awake) {
		chg_err("unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (awake) {
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	static bool pm_flag = false;

	if (chip->unwakelock_chg && awake) {
		chg_err("unwakelock testing, can not set wakelock.\n");
		return;
	}

	if (!chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
	}
#endif
}

static int oplus_wired_set_err_code(struct oplus_chg_wired *chip,
				    unsigned int err_code)
{
	struct mms_msg *msg;
	int rc;

	if (chip->err_code == err_code)
		return 0;

	chip->err_code = err_code;
	chg_info("set err_code=%08x\n", err_code);

	if (err_code & BIT(OPLUS_ERR_CODE_OVP))
		vote(chip->output_suspend_votable, UOVP_VOTER, true, 1, false);
	else
		vote(chip->output_suspend_votable, UOVP_VOTER, false, 0, false);

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				      WIRED_ITEM_ERR_CODE, err_code);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->wired_topic, msg);
	if (rc < 0) {
		chg_err("publish error code msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

#define VBUS_CHECK_COUNT 2
#define VBUS_OV_OFFSET 500
#define VBUS_UV_OFFSET 300
static void oplus_wired_vbus_check(struct oplus_chg_wired *chip)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	static int ov_count;
	enum oplus_wired_vbus_vol vbus_type;
	int ov_mv;
	unsigned int err_code = 0;

	if (chip->vooc_started)
		goto done;

	if (chip->vbus_set_mv == OPLUS_CHG_VBUS_9V)
		vbus_type = OPLUS_VBUS_9V;
	else
		vbus_type = OPLUS_VBUS_5V;

	if (chip->err_code & BIT(OPLUS_ERR_CODE_OVP))
		ov_mv = spec->vbus_ov_thr_mv[vbus_type] - VBUS_OV_OFFSET;
	else
		ov_mv = spec->vbus_ov_thr_mv[vbus_type];

	if (chip->vbus_mv > ov_mv) {
		if (ov_count > VBUS_CHECK_COUNT)
			err_code |= BIT(OPLUS_ERR_CODE_OVP);
		else
			ov_count++;
	} else {
		ov_count = 0;
	}

done:
	oplus_wired_set_err_code(chip, err_code);
}

static int oplus_wired_current_set(struct oplus_chg_wired *chip,
				   bool vbus_changed)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	int icl_ma, icl_tmp_ma;
	int fcc_ma;
	int cool_down, cool_down_curr;
	bool led_on = false;
	bool icl_changed;
	union mms_msg_data data = { 0 };
	int rc;

	if (!chip->chg_online)
		return 0;

	/* Make sure you get the correct charger type */
	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_CHG_TYPE, &data,
				false);
	chip->chg_type = data.intval;

	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_DCP:
	case OPLUS_CHG_USB_TYPE_ACA:
	case OPLUS_CHG_USB_TYPE_C:
	case OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID:
	case OPLUS_CHG_USB_TYPE_PD_SDP:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_DCP;
		break;
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_QC;
		break;
	case OPLUS_CHG_USB_TYPE_CDP:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_CDP;
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_PD;
		break;
	case OPLUS_CHG_USB_TYPE_VOOC:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_VOOC;
		break;
	case OPLUS_CHG_USB_TYPE_SVOOC:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_DCP;
		break;
	case OPLUS_CHG_USB_TYPE_SDP:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_SDP;
		break;
	case OPLUS_CHG_USB_TYPE_UFCS:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_DCP;
		break;
	default:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_UNKNOWN;
		break;
	}

	cool_down = chip->cool_down;
	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_QC:
	case OPLUS_WIRED_CHG_MODE_PD:
		if (cool_down > spec->cool_down_pdqc_level_max)
			cool_down = spec->cool_down_pdqc_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_pdqc_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	case OPLUS_WIRED_CHG_MODE_VOOC:
		if (cool_down > spec->cool_down_vooc_level_max)
			cool_down = spec->cool_down_vooc_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_vooc_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	default:
		if (cool_down > spec->cool_down_normal_level_max)
			cool_down = spec->cool_down_normal_level_max;
		if (cool_down > 0)
			cool_down_curr =
				spec->cool_down_normal_curr_ma[cool_down - 1];
		else
			cool_down_curr = 0;
		break;
	}
	if (chip->chg_ctrl_by_sale_mode && chip->chg_type == OPLUS_CHG_USB_TYPE_DCP) {
		if (chip->chg_ctrl_by_sale_mode == SALE_MODE_COOL_DOWN)
			cool_down_curr = OPLUS_CHG_900_CHARGING_CURRENT;
		else if (chip->chg_ctrl_by_sale_mode == SALE_MODE_COOL_DOWN_TWO)
			cool_down_curr = OPLUS_CHG_500_CHARGING_CURRENT;
		chg_info("sale mode enter: %d\n", chip->chg_ctrl_by_sale_mode);
	}

	icl_ma =
		spec->input_power_mw[chip->chg_mode] * 1000 / chip->vbus_set_mv;
	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_QC:
		icl_ma = min(icl_ma, spec->qc_iclmax_ma);
		break;
	case OPLUS_WIRED_CHG_MODE_PD:
		icl_ma = min(icl_ma, spec->pd_iclmax_ma);
		break;
	default:
		break;
	}
	fcc_ma =
		spec->fcc_ma[chip->fcc_gear][chip->chg_mode][chip->temp_region];

	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_EIS_STATUS, &data, false);
	if ((rc == 0) && (data.intval != EIS_STATUS_DISABLE)) {
		icl_ma =
			spec->input_power_mw[OPLUS_WIRED_CHG_MODE_PD] * 1000 / OPLUS_CHG_VBUS_9V;
		icl_ma = min(icl_ma, spec->pd_iclmax_ma);
		chg_info("<EIS>refresh icl[%d] for EIS[%d]\n", icl_ma, data.intval);
	}

	chg_info(
		"chg_type=%s, chg_mode=%s, spec_icl=%d, spec_fcc=%d, cool_down_icl=%d, sale_mode=%d, cool_down=%d\n",
		oplus_wired_get_chg_type_str(chip->chg_type),
		oplus_wired_get_chg_mode_region_str(chip->chg_mode), icl_ma,
		fcc_ma, cool_down_curr, chip->chg_ctrl_by_sale_mode, chip->cool_down);

	mutex_lock(&chip->current_lock);
	icl_tmp_ma = get_effective_result(chip->icl_votable);
	vote(chip->fcc_votable, SPEC_VOTER, true, fcc_ma, false);
	vote(chip->icl_votable, SPEC_VOTER, true, icl_ma, true);
	if (!chip->authenticate || !chip->hmac) {
		vote(chip->fcc_votable, NON_STANDARD_VOTER, true,
		     spec->non_standard_ibatmax_ma, false);
		chg_err("!authenticate or !hmac, set nonstandard current\n");
	} else {
		vote(chip->fcc_votable, NON_STANDARD_VOTER, false, 0, false);
	}

	/* cool down */
	if (chip->comm_topic) {
		rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_LED_ON,
					     &data, false);
		if (!rc)
			led_on = !!data.intval;
	}
	if (led_on && cool_down_curr > 0) {
		if (chip->chg_ctrl_by_sale_mode &&
		    (chip->chg_mode == OPLUS_WIRED_CHG_MODE_QC ||
		    chip->chg_mode == OPLUS_WIRED_CHG_MODE_PD))
			vote(chip->icl_votable, SALE_MODE_VOTER,
			     true, spec->cool_down_sale_pdqc_curr_ma, true);
		else
			vote(chip->icl_votable, SALE_MODE_VOTER, false, 0, true);
		vote(chip->icl_votable, COOL_DOWN_VOTER, true, cool_down_curr, true);
	} else {
		vote(chip->icl_votable, COOL_DOWN_VOTER, false, 0, true);
	}

	if (led_on) {
		if (chip->chg_type == OPLUS_CHG_USB_TYPE_VOOC)
			vote(chip->fcc_votable, LED_ON_VOTER, false, 0, false);
		else
			vote(chip->fcc_votable, LED_ON_VOTER, true,
			     spec->led_on_fcc_max_ma[chip->temp_region], false);
	} else {
		vote(chip->fcc_votable, LED_ON_VOTER, false, 0, false);
	}
	icl_changed = (icl_tmp_ma != get_effective_result(chip->icl_votable));
	chg_info("vbus_changed=%s, icl_changed=%s\n",
		 true_or_false_str(vbus_changed),
		 true_or_false_str(icl_changed));
	/* If ICL has changed, no need to reset the current */
	if (vbus_changed && !icl_changed) {
		chg_info("vbus changed, need rerun icl vote\n");
		rerun_election(chip->icl_votable, true);
	}
	mutex_unlock(&chip->current_lock);

	return 0;
}

static void oplus_wired_variables_init(struct oplus_chg_wired *chip)
{
	chip->chg_online = false;

	chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	chip->vbus_set_mv = OPLUS_CHG_VBUS_5V;
	chip->temp_region = TEMP_REGION_HOT;
	chip->chg_mode = OPLUS_WIRED_CHG_MODE_UNKNOWN;
	chip->qc_action = OPLUS_ACTION_NULL;
	chip->pd_action = OPLUS_ACTION_NULL;
	chip->pd_retry_count = 0;
	chip->qc_retry_count = 0;
	chip->chg_ctrl_by_sale_mode = 0;
	mutex_init(&chip->icl_lock);
	mutex_init(&chip->current_lock);
}

static int oplus_wired_get_vbatt_pdqc_to_9v_thr(struct oplus_chg_wired *chip)
{
	struct device_node *node = oplus_get_node_by_type(chip->dev->of_node);
	int thr;
	int rc;

	if (node != NULL) {
		rc = of_property_read_u32(
			node, "oplus_spec,vbatt_pdqc_to_9v_thr", &thr);
		if (rc < 0) {
			chg_err("oplus_spec,vbatt_pdqc_to_9v_thr reading failed, rc=%d\n",
				rc);
			thr = default_config.vbatt_pdqc_to_9v_thr;
		}
	} else {
		thr = default_config.vbatt_pdqc_to_9v_thr;
	}

	return thr;
}

#define QC_RETRY_DELAY msecs_to_jiffies(3000)
#define QC_RETRY_COUNT_MAX 3
static void oplus_wired_qc_config_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, qc_config_work.work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	int cool_down, cool_down_vol;
	int vbus_set_mv = OPLUS_CHG_VBUS_5V; /* vbus default setting voltage is 5V */
	bool vbus_changed = false;
	int rc;

	if (chip->chg_mode != OPLUS_WIRED_CHG_MODE_QC)
		goto set_curr;

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		if (chip->chg_ctrl_by_sale_mode) {
			if (spec->cool_down_sale_pdqc_vol_mv == PDQC_SALE_MODE_ALLOW_BUCK_MV)
				chip->qc_action = OPLUS_ACTION_BUCK;
			cool_down_vol = spec->cool_down_sale_pdqc_vol_mv;
		} else {
			cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
		}
	} else {
		cool_down_vol = 0;
	}
	chip->vbus_mv = oplus_wired_get_vbus();
	switch (chip->qc_action) {
	case OPLUS_ACTION_BOOST:
		if (cool_down_vol > 0 && cool_down_vol < OPLUS_CHG_VBUS_9V) {
			chg_info("cool down limit, qc cannot be boosted\n");
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}

		if (chip->vbus_mv > PDQC_BUCK_VBUS_THR) {
			chg_info("vbus_mv = %d mv, not need to boost.\n", chip->vbus_mv);
			goto set_curr;
		}

		if (spec->vbatt_pdqc_to_9v_thr > 0 &&
		    chip->vbat_mv < spec->vbatt_pdqc_to_9v_thr) {
			chg_info("qc starts to boost, retry count %d.\n", chip->qc_retry_count);
			/* Set the current to 500ma before QC boost ot 9V */
			vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
			     true);
			mutex_lock(&chip->icl_lock);
			rc = oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, OPLUS_CHG_VBUS_9V);
			mutex_unlock(&chip->icl_lock);
			if (rc == -EAGAIN) {
				chg_err("vbus_mv = %d mv, try again.\n", chip->vbus_mv);
				if (chip->qc_retry_count < QC_RETRY_COUNT_MAX) {
					chip->qc_retry_count++;
					chip->qc_action = OPLUS_ACTION_BOOST;
					schedule_delayed_work(
						&chip->qc_config_work,
						QC_RETRY_DELAY);
					return;
				} else {
					chip->qc_retry_count = 0;
					chip->qc_action = OPLUS_ACTION_NULL;
					goto set_curr;
				}
			}
			if (rc < 0) {
				chip->qc_action = OPLUS_ACTION_NULL;
				goto set_curr;
			}
			chip->qc_retry_count = 0;
		} else {
			chg_info(
				"battery voltage too high, qc cannot be boosted\n");
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = OPLUS_CHG_VBUS_9V;
		break;
	case OPLUS_ACTION_BUCK:
		chg_info("qc starts to buck\n");
		if (chip->vbus_mv <= PDQC_BUCK_VBUS_THR) {
			chg_info("vbus_mv = %d mv, not need to buck.\n", chip->vbus_mv);
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}

		/* Set the current to 500ma before stepping down */
		vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
		     true);
		mutex_lock(&chip->icl_lock);
		rc = oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, OPLUS_CHG_VBUS_5V);
		mutex_unlock(&chip->icl_lock);
		if (rc < 0) {
			chip->qc_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = OPLUS_CHG_VBUS_5V;
		break;
	default:
		goto set_curr;
	}

	oplus_wired_current_set(chip, true);
	reinit_completion(&chip->qc_action_ack);
	if (!READ_ONCE(chip->chg_online)) {
		chg_info("charger offline\n");
		return;
	}
	rc = wait_for_completion_timeout(
		&chip->qc_action_ack,
		msecs_to_jiffies(PDQC_CONFIG_WAIT_TIME_MS));
	if (!rc) {
		chg_err("qc config timeout\n");
		chip->vbus_mv = oplus_wired_get_vbus();
		if (chip->vbus_mv >= 7500) {
			vbus_set_mv = OPLUS_CHG_VBUS_9V;
		} else {
			mutex_lock(&chip->icl_lock);
			oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, OPLUS_CHG_VBUS_5V);
			mutex_unlock(&chip->icl_lock);
			oplus_wired_aicl_rerun();
			vbus_set_mv = OPLUS_CHG_VBUS_5V;
		}
		chip->qc_action = OPLUS_ACTION_NULL;
	}
	if (chip->qc_action == OPLUS_ACTION_BOOST)
		vbus_set_mv = OPLUS_CHG_VBUS_9V;
	else if (chip->qc_action == OPLUS_ACTION_BUCK)
		vbus_set_mv = OPLUS_CHG_VBUS_5V;
	chip->qc_action = OPLUS_ACTION_NULL;

	if (vbus_set_mv != chip->vbus_set_mv) {
		chip->vbus_set_mv = vbus_set_mv;
		vbus_changed = true;
	}

set_curr:
	/* The configuration fails and the current needs to be reset */
	oplus_wired_current_set(chip, vbus_changed);
}

static int oplus_wired_get_afi_condition(void)
{
	int afi_condition = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_GET_AFI_CONDITION,
				     &data, true);
	if (!rc)
		afi_condition = data.intval;

	return afi_condition;
}

static void oplus_pdqc_switch_end_recheck_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wired *chip =
		container_of(dwork, struct oplus_chg_wired, switch_end_recheck_work);

	chg_info("switch end recheck\n");
	if (chip->retention_state_ready)
		return;
	chg_info("switch end\n");
	if (chip->cpa_current_type == CHG_PROTOCOL_PD)
		oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_PD);
	else if (chip->cpa_current_type == CHG_PROTOCOL_QC)
		oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_QC);
}

#define SWITCH_END_RECHECK_DELAY_MS	1000
static int oplus_pd_cpa_switch_end(struct oplus_chg_wired *chip)
{
	if (!chip->retention_state) {
		oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_PD);
	} else {
		if (!chip->retention_state_ready)
			schedule_delayed_work(&chip->switch_end_recheck_work,
				msecs_to_jiffies(SWITCH_END_RECHECK_DELAY_MS));
	}
	return 0;
}

static int oplus_qc_cpa_switch_end(struct oplus_chg_wired *chip)
{
	if (!chip->retention_state) {
		oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_QC);
	} else {
		if (!chip->retention_state_ready)
			schedule_delayed_work(&chip->switch_end_recheck_work,
				msecs_to_jiffies(SWITCH_END_RECHECK_DELAY_MS));
	}
	return 0;
}

#define PD_BOOST_DISABLE_ICL_DELAY msecs_to_jiffies(3000)
#define PD_BOOST_ICL_MA 1500
static void oplus_wired_pd_boost_icl_disable_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(work, struct oplus_chg_wired, pd_boost_icl_disable_work.work);

	vote(chip->icl_votable, BOOST_VOTER, false, 0, true);
}

#define PD_RETRY_DELAY msecs_to_jiffies(1000)
#define PD_RETRY_COUNT_MAX 3
static void oplus_wired_pd_config_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, pd_config_work.work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	int cool_down, cool_down_vol;
	int vbus_set_mv = OPLUS_CHG_VBUS_5V; /* vbus default setting voltage is 5V */
	bool vbus_changed = false;
	int rc;
	int vbus_get_mv = 0;
	union mms_msg_data data = { 0 };

#define OPLUS_PD_5V_PDO 0x31912c
#define OPLUS_PD_9V_PDO 0x32d12c

	if (chip->chg_mode != OPLUS_WIRED_CHG_MODE_PD) {
		chg_err("chg_mode(=%d) error\n", chip->chg_mode);
		goto set_curr;
	}

	if (chip->cpa_support) {
		oplus_mms_get_item_data(chip->cpa_topic, CPA_ITEM_ALLOW, &data, false);
		if (data.intval != CHG_PROTOCOL_PD) {
			chg_err("switched to other protocol, not change vbus.");
			return;
		}
	}

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		if (chip->chg_ctrl_by_sale_mode) {
			if (spec->cool_down_sale_pdqc_vol_mv == PDQC_SALE_MODE_ALLOW_BUCK_MV)
				chip->pd_action = OPLUS_ACTION_BUCK;
			cool_down_vol = spec->cool_down_sale_pdqc_vol_mv;
		} else {
			cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
		}
	} else {
		cool_down_vol = 0;
	}

	chip->vbus_mv = oplus_wired_get_vbus();
	switch (chip->pd_action) {
	case OPLUS_ACTION_BOOST:
		if (is_pd_svooc_votable_available(chip) &&
		    !!get_effective_result(chip->pd_svooc_votable) &&
		    is_vooc_disable_votable_available(chip) &&
		    !get_effective_result(chip->vooc_disable_votable)) {
			chg_info("pd_svooc check, pd cannot be boosted\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			oplus_pd_cpa_switch_end(chip);
			goto set_curr;
		}

		if (chip->pd_boost_disable) {
			chg_info("pd boost is disable\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (cool_down_vol > 0 && cool_down_vol < OPLUS_CHG_VBUS_9V) {
			chg_info("cool down limit, pd cannot be boosted\n");
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		if (spec->vbatt_pdqc_to_9v_thr > 0 &&
		    chip->vbat_mv < spec->vbatt_pdqc_to_9v_thr) {
			chg_info("pd starts to boost,retry count %d.\n", chip->pd_retry_count);
			/* Set the current to 500ma before PD before boost ot 9V */
			vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
			     true);
			cancel_delayed_work_sync(&chip->pd_boost_icl_disable_work);
			vote(chip->icl_votable, BOOST_VOTER, true, PD_BOOST_ICL_MA, true);
			schedule_delayed_work(&chip->pd_boost_icl_disable_work, PD_BOOST_DISABLE_ICL_DELAY);
			mutex_lock(&chip->icl_lock);
			rc = oplus_wired_set_pd_config(OPLUS_PD_9V_PDO);
			mutex_unlock(&chip->icl_lock);
			vbus_get_mv = oplus_wired_get_vbus();
			if (rc < 0 || vbus_get_mv < 7500) {
				if (chip->pd_retry_count < PD_RETRY_COUNT_MAX) {
					chip->pd_retry_count++;
					schedule_delayed_work(
						&chip->pd_config_work,
						PD_RETRY_DELAY);
					return;
				} else {
					chip->pd_retry_count = 0;
					chip->pd_action = OPLUS_ACTION_NULL;
					vote(chip->pd_boost_disable_votable,
					     TIMEOUT_VOTER, true, 1, false);
					chg_err("set pd boost timeout\n");
					goto set_curr;
				}
			}
			chip->pd_retry_count = 0;
		} else {
			chg_info("vbat_mv too high, vbatt_pdqc_to_9v_thr=%d, vbat_mv=%d\n",
				spec->vbatt_pdqc_to_9v_thr, chip->vbat_mv);
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = OPLUS_CHG_VBUS_9V;
		break;
	case OPLUS_ACTION_BUCK:
		chg_info("pd starts to buck\n");
		if (chip->vbus_mv <= PDQC_BUCK_VBUS_THR) {
			chg_info("vbus_mv = %d mv, not need to buck.\n", chip->vbus_mv);
			goto set_curr;
		}

		/* Set the current to 500ma before stepping down */
		vote(chip->icl_votable, SPEC_VOTER, true, PDQC_BUCK_DEF_CURR_MA,
		     true);
		vote(chip->icl_votable, BOOST_VOTER, false, 0, true);
		cancel_delayed_work_sync(&chip->pd_boost_icl_disable_work);
		mutex_lock(&chip->icl_lock);
		rc = oplus_wired_set_pd_config(OPLUS_PD_5V_PDO);
		mutex_unlock(&chip->icl_lock);
		if (rc < 0) {
			chip->pd_action = OPLUS_ACTION_NULL;
			goto set_curr;
		}
		chip->vbus_set_mv = OPLUS_CHG_VBUS_5V;
		break;
	default:
		goto set_curr;
	}

	oplus_wired_current_set(chip, true);
	reinit_completion(&chip->pd_action_ack);
	if (!READ_ONCE(chip->chg_online)) {
		chg_info("charger offline\n");
		return;
	}
	rc = wait_for_completion_timeout(
		&chip->pd_action_ack,
		msecs_to_jiffies(PDQC_CONFIG_WAIT_TIME_MS));
	if (!rc) {
		chg_err("pd config timeout\n");
		chip->vbus_mv = oplus_wired_get_vbus();
		if (chip->vbus_mv >= 7500)
			vbus_set_mv = OPLUS_CHG_VBUS_9V;
		else
			vbus_set_mv = OPLUS_CHG_VBUS_5V;
		chip->pd_action = OPLUS_ACTION_NULL;
	}
	if (chip->pd_action == OPLUS_ACTION_BOOST)
		vbus_set_mv = OPLUS_CHG_VBUS_9V;
	else if (chip->pd_action == OPLUS_ACTION_BUCK)
		vbus_set_mv = OPLUS_CHG_VBUS_5V;
	chip->pd_action = OPLUS_ACTION_NULL;

	if (vbus_set_mv != chip->vbus_set_mv) {
		chip->vbus_set_mv = vbus_set_mv;
		vbus_changed = true;
	}

set_curr:
	/* The configuration fails and the current needs to be reset */
	oplus_wired_current_set(chip, vbus_changed);
}

static void oplus_wired_strategy_update(struct oplus_chg_wired *chip)
{
	struct oplus_chg_strategy *strategy;
	int tmp;
	int rc;

	if (chip->chg_mode < OPLUS_WIRED_CHG_MODE_UNKNOWN || chip->chg_mode >= OPLUS_WIRED_CHG_MODE_MAX)
		return;

	strategy = chip->strategy[chip->chg_mode];
	if (strategy == NULL)
		return;

	rc = oplus_chg_strategy_get_data(strategy, &tmp);
	if (rc < 0) {
		vote(chip->icl_votable, STRATEGY_VOTER, false, 0, true);
		chg_err("get strategy data error, rc=%d", rc);
	} else {
		if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_PD || chip->chg_mode == OPLUS_WIRED_CHG_MODE_QC) {
			if (chip->vbus_set_mv == OPLUS_CHG_VBUS_9V)
				vote(chip->icl_votable, STRATEGY_VOTER, true, tmp, true);
			else
				vote(chip->icl_votable, STRATEGY_VOTER, false, 0, true);
		} else {
			vote(chip->icl_votable, STRATEGY_VOTER, true, tmp, true);
		}
	}
}

static void oplus_wired_gauge_update_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, gauge_update_work);
	struct oplus_wired_spec_config *spec = &chip->spec;
	union mms_msg_data data = { 0 };
	int cool_down_vol = 0;
	int cool_down;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;

	if (!chip->chg_online)
		return;

	chip->vbus_mv = oplus_wired_get_vbus();
	if (chip->vbus_mv < 0)
		chip->vbus_mv = 0;

	if ((chip->qc_action == OPLUS_ACTION_BOOST && chip->vbus_mv > 7500) ||
	    (chip->qc_action == OPLUS_ACTION_BUCK && chip->vbus_mv < 7500))
		complete(&chip->qc_action_ack);
	if ((chip->pd_action == OPLUS_ACTION_BOOST && chip->vbus_mv > 7500) ||
	    (chip->pd_action == OPLUS_ACTION_BUCK && chip->vbus_mv < 7500))
		complete(&chip->pd_action_ack);

	if (chip->cool_down > 0) {
		cool_down = chip->cool_down > spec->cool_down_pdqc_level_max ?
				    spec->cool_down_pdqc_level_max :
				    chip->cool_down;
		cool_down_vol = spec->cool_down_pdqc_vol_mv[cool_down - 1];
	}

	if (chip->vbus_mv > 7500 &&
	    ((spec->vbatt_pdqc_to_5v_thr > 0 &&
	      chip->vbat_mv >= spec->vbatt_pdqc_to_5v_thr) ||
	     (cool_down_vol > 0 && cool_down_vol < OPLUS_CHG_VBUS_9V))) {
		if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_QC) {
			chip->qc_action = OPLUS_ACTION_BUCK;
			schedule_delayed_work(&chip->qc_config_work, 0);
		} else if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_PD) {
			chip->pd_action = OPLUS_ACTION_BUCK;
			schedule_delayed_work(&chip->pd_config_work, 0);
		}
	}

	if (oplus_wired_get_afi_condition())
		oplus_gauge_protect_check();

	oplus_wired_strategy_update(chip);
	oplus_wired_vbus_check(chip);
	if (!chip->vooc_started)
		oplus_wired_kick_wdt(chip->wired_topic);
}

static void oplus_wired_gauge_subs_callback(struct mms_subscribe *subs,
					    enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_TIMER:
		schedule_work(&chip->gauge_update_work);
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case GAUGE_ITEM_AUTH:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n",
					rc);
				chip->authenticate = false;
			} else {
				chip->authenticate = !!data.intval;
			}
			break;
		case GAUGE_ITEM_HMAC:
			rc = oplus_mms_get_item_data(chip->gauge_topic, id,
						     &data, false);
			if (rc < 0) {
				chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n",
					rc);
				chip->hmac = false;
			} else {
				chip->hmac = !!data.intval;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_gauge_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->gauge_topic = topic;
	chip->gauge_subs = oplus_mms_subscribe(chip->gauge_topic, chip,
					       oplus_wired_gauge_subs_callback,
					       "chg_wired");
	if (IS_ERR_OR_NULL(chip->gauge_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->gauge_subs));
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MAX, &data,
				false);
	chip->vbat_mv = data.intval;
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_HMAC, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_HMAC data, rc=%d\n", rc);
		chip->hmac = false;
	} else {
		chip->hmac = !!data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_AUTH, &data,
				     true);
	if (rc < 0) {
		chg_err("can't get GAUGE_ITEM_AUTH data, rc=%d\n", rc);
		chip->authenticate = false;
	} else {
		chip->authenticate = !!data.intval;
	}
	chg_info("hmac=%d, authenticate=%d\n", chip->hmac, chip->authenticate);
}

static void oplus_wired_wired_subs_callback(struct mms_subscribe *subs,
					    enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			schedule_work(&chip->plugin_work);
			break;
		case WIRED_ITEM_CHG_TYPE:
			schedule_work(&chip->chg_type_change_work);
			break;
		case WIRED_ITEM_REAL_CHG_TYPE:
			if (get_client_vote(chip->pd_boost_disable_votable, SVID_VOTER) == 0)
				complete(&chip->pd_check_ack);
			break;
		case WIRED_ITEM_PRESENT:
			oplus_mms_get_item_data(chip->wired_topic, id, &data, false);
			chip->irq_plugin = !!data.intval;
			break;
		case WIRED_ITEM_CC_MODE:
		case WIRED_ITEM_CC_DETECT:
			break;
		case WIRED_ITEM_CHARGER_CURR_MAX:
			schedule_work(&chip->charger_current_changed_work);
			break;
		case WIRED_ITEM_CHARGER_VOL_MAX:
			/* TODO */
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_wired_topic(struct oplus_mms *topic,
					      void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs = oplus_mms_subscribe(chip->wired_topic, chip,
					       oplus_wired_wired_subs_callback,
					       "chg_wired");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	if (!chip->vooc_support && !chip->cpa_support)
		oplus_wired_qc_detect_enable(true);
	else
		oplus_wired_qc_detect_enable(false);
	if (oplus_is_rf_ftm_mode())
		vote(chip->input_suspend_votable, WLAN_VOTER, true, 1, false);

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->chg_online = data.intval;
	if (!chip->chg_online)
		schedule_work(&chip->plugin_work);
}

static void oplus_wired_plugin_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, plugin_work);
	union mms_msg_data data = { 0 };
	int i = 0;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int boot_mode = 0;

	boot_mode = get_boot_mode();
#endif
	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				false);
	chip->chg_online = data.intval;
	if (chip->chg_online) {
		chip->retention_state_ready = false;
		oplus_wired_set_awake(chip, true);
		if (chip->gauge_topic != NULL) {
			oplus_mms_get_item_data(chip->gauge_topic,
						GAUGE_ITEM_VOL_MAX, &data,
						true);
			chip->vbat_mv = data.intval;
			chg_info("vbat_mv %d\n", chip->vbat_mv);
		}
		vote_override(chip->output_suspend_votable, OVERRIDE_VOTER, false, 0, false);
		vote_override(chip->input_suspend_votable, OVERRIDE_VOTER, false, 0, false);

		oplus_wired_current_set(chip, false);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
			if (chip->strategy[i] != NULL)
				oplus_chg_strategy_init(chip->strategy[i]);
		}

		if (READ_ONCE(chip->disconnect_change) && chip->retention_state) {
			if (chip->cpa_current_type == CHG_PROTOCOL_PD)
				schedule_work(&chip->pd_check_work);
			if (chip->cpa_current_type == CHG_PROTOCOL_QC)
				schedule_work(&chip->qc_check_work);
			WRITE_ONCE(chip->disconnect_change, false);
		}
	} else {
		/*
		 * Set during plug out to prevent untimely settings
		 * during plug in
		 */
		chip->chg_ctrl_by_sale_mode = 0;
		vote(chip->pd_boost_disable_votable, SVID_VOTER, true, 1,
		     false);
		vote(chip->pd_boost_disable_votable, TIMEOUT_VOTER, false, 0,
		     false);

		/* USER_VOTER and HIDL_VOTER need to be invalid when the usb is unplugged */
		vote(chip->icl_votable, USER_VOTER, false, 0, true);
		vote(chip->fcc_votable, USER_VOTER, false, 0, true);
		vote(chip->fcc_votable, SPEC_VOTER, false, 0, true);
		vote(chip->icl_votable, HIDL_VOTER, false, 0, true);
		vote(chip->icl_votable, MAX_VOTER, false, 0, true);
		vote(chip->icl_votable, STRATEGY_VOTER, false, 0, true);
		vote(chip->icl_votable, PD_PDO_ICL_VOTER, false, 0, true);
		chip->pd_retry_count = 0;
		chip->qc_retry_count = 0;
		chip->qc_action = OPLUS_ACTION_NULL;
		chip->pd_action = OPLUS_ACTION_NULL;
		complete_all(&chip->qc_action_ack);
		complete_all(&chip->pd_action_ack);
		complete_all(&chip->qc_check_ack);
		complete_all(&chip->pd_check_ack);
		cancel_delayed_work_sync(&chip->qc_config_work);
		cancel_delayed_work_sync(&chip->pd_config_work);
		vote(chip->icl_votable, BOOST_VOTER, false, 0, true);
		cancel_delayed_work_sync(&chip->pd_boost_icl_disable_work);
		cancel_delayed_work_sync(&chip->switch_end_recheck_work);
		cancel_work_sync(&chip->qc_check_work);
		cancel_work_sync(&chip->pd_check_work);
		chip->vbus_set_mv = OPLUS_CHG_VBUS_5V;
		oplus_wired_set_err_code(chip, 0);

		if (is_pd_svooc_votable_available(chip))
			vote(chip->pd_svooc_votable, DEF_VOTER, false, 0,
			     false);

		/* Force open charging */
		vote_override(chip->output_suspend_votable, OVERRIDE_VOTER, true, 0, false);
		vote_override(chip->input_suspend_votable, OVERRIDE_VOTER, true, 0, false);
		vote(chip->icl_votable, SPEC_VOTER, true, 500, true);
#ifdef CONFIG_OPLUS_CHARGER_MTK
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
		if (boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT)
			oplus_wired_set_awake(chip, false);
#endif
#else
		oplus_wired_set_awake(chip, false);
#endif
	}

	if (chip->gauge_topic != NULL)
		oplus_mms_topic_update(chip->gauge_topic, true);
}

static void oplus_wired_chg_type_change_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, chg_type_change_work);

	chip->chg_type = oplus_wired_get_chg_type();
	if (chip->chg_type < 0)
		chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;

	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_QC;
		chip->qc_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->qc_config_work, 0);
		if (chip->cpa_support)
			complete(&chip->qc_check_ack);
		break;
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		if (chip->cpa_support && chip->cpa_current_type != CHG_PROTOCOL_PD)
			break;
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_PD;
		chip->pd_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->pd_config_work, 0);
		break;
	default:
		oplus_wired_current_set(chip, false);
		break;
	}
}

static void oplus_wired_charger_current_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, charger_current_changed_work);
	union mms_msg_data data = { 0 };
	int rc;

	rc = oplus_mms_get_item_data(chip->wired_topic,
				     WIRED_ITEM_CHARGER_CURR_MAX, &data, false);
	if (rc < 0) {
		chg_err("can't get charger curr max msg data\n");
		return;
	}
	vote(chip->icl_votable, MAX_VOTER, true, data.intval, true);
}

static void oplus_wired_temp_region_update_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip = container_of(
		work, struct oplus_chg_wired, temp_region_update_work);

	if (chip->temp_region == TEMP_REGION_HOT ||
	    chip->temp_region == TEMP_REGION_COLD)
		vote(chip->output_suspend_votable, BATT_TEMP_VOTER, true, 0,
		     false);
	else
		vote(chip->output_suspend_votable, BATT_TEMP_VOTER, false, 0,
		     false);

	oplus_wired_current_set(chip, false);
}

static void oplus_wired_led_on_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, led_on_changed_work);

	if (!chip->chg_online)
		return;

	oplus_wired_current_set(chip, false);
}

static void oplus_wired_icl_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, icl_changed_work);

	rerun_election(chip->icl_votable, true);
}

static void oplus_wired_fcc_changed_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, fcc_changed_work);

	rerun_election(chip->fcc_votable, false);
}

static void oplus_wired_qc_check_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, qc_check_work);
	int rc;

	chg_info("qc check work\n");
	reinit_completion(&chip->qc_check_ack);
	rc = oplus_cpa_switch_start(chip->cpa_topic, CHG_PROTOCOL_QC);
	if (rc < 0) {
		chg_info("cpa protocol not qc, return\n");
		return;
	}
	if (chip->chg_type == OPLUS_CHG_USB_TYPE_QC2 ||
		chip->chg_type == OPLUS_CHG_USB_TYPE_QC3) {
		chg_info("is qc charging, not retry\n");
		return;
	}
	oplus_wired_qc_detect_enable(true);
	rc = wait_for_completion_timeout(
		&chip->qc_check_ack,
		msecs_to_jiffies(QC_CHECK_WAIT_TIME_MS));
	if (!rc) {
		chg_err("qc check timeout\n");
		oplus_wired_qc_detect_enable(false);
		oplus_qc_cpa_switch_end(chip);
		return;
	}
}

static void oplus_wired_pd_check_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, pd_check_work);
	int rc;

	rc = oplus_cpa_switch_start(chip->cpa_topic, CHG_PROTOCOL_PD);
	if (rc < 0) {
		chg_info("cpa protocol not pd, return\n");
		return;
	}
	chip->chg_type = oplus_wired_get_chg_type();
	if (chip->chg_type < 0)
		chip->chg_type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	if (chip->chg_type == OPLUS_CHG_USB_TYPE_UNKNOWN ||
		chip->chg_type == OPLUS_CHG_USB_TYPE_DCP) {
		msleep(WAIT_BC1P2_GET_TYPE);
		chip->chg_type = oplus_wired_get_chg_type();
	}
	chg_info("wired_type=%s\n", oplus_wired_get_chg_type_str(chip->chg_type));

	if (!chip->chg_online) {
		oplus_pd_cpa_switch_end(chip);
		return;
	}

	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		reinit_completion(&chip->pd_check_ack);
		if (get_client_vote(chip->pd_boost_disable_votable, SVID_VOTER) > 0) {
			rc = wait_for_completion_timeout(
				&chip->pd_check_ack, msecs_to_jiffies(PD_CHECK_WAIT_TIME_MS));
			if (!rc) {
				chg_err("pd check timeout\n");
				oplus_pd_cpa_switch_end(chip);
				return;
			}
		}
		if (get_client_vote(chip->pd_boost_disable_votable, SVID_VOTER) > 0) {
			oplus_pd_cpa_switch_end(chip);
			return;
		}
		chip->chg_mode = OPLUS_WIRED_CHG_MODE_PD;
		chip->pd_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->pd_config_work, 0);
		break;
	default:
		oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_PD);
		break;
	}
}

static void oplus_pdqc_retention_disconnect_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_wired *chip =
		container_of(dwork, struct oplus_chg_wired, retention_disconnect_work);
	union mms_msg_data data = { 0 };

	oplus_mms_get_item_data(chip->retention_topic, RETENTION_ITEM_DISCONNECT_COUNT, &data, true);
	chip->pdqc_connect_error_count = data.intval;
	chg_debug("cpa_current_type= %d, pdqc_connect_error_count =%d\n",
		chip->cpa_current_type, chip->pdqc_connect_error_count);
	if (chip->pdqc_connect_error_count > DPQC_CONNECT_ERROR_COUNT_LEVEL) {
		if (chip->cpa_current_type == CHG_PROTOCOL_QC) {
			oplus_cpa_protocol_disable(chip->cpa_topic, CHG_PROTOCOL_QC);
			oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_QC);
			return;
		} else if (chip->cpa_current_type == CHG_PROTOCOL_PD) {
			oplus_cpa_protocol_disable(chip->cpa_topic, CHG_PROTOCOL_PD);
			oplus_cpa_switch_end(chip->cpa_topic, CHG_PROTOCOL_PD);
			return;
		}
	}

	if (READ_ONCE(chip->chg_online)) {
		flush_work(&chip->plugin_work);
		if (chip->retention_state && chip->cpa_current_type == CHG_PROTOCOL_PD)
			schedule_work(&chip->pd_check_work);
		if (chip->retention_state && chip->cpa_current_type == CHG_PROTOCOL_QC)
			schedule_work(&chip->qc_check_work);
		WRITE_ONCE(chip->disconnect_change, false);
	} else {
		WRITE_ONCE(chip->disconnect_change, true);
	}
}

static void oplus_wired_retention_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };
	int ret = 0;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case RETENTION_ITEM_CONNECT_STATUS:
			oplus_mms_get_item_data(chip->retention_topic, id, &data,
						false);
			chip->retention_state = !!data.intval;
			if (chip->retention_state) {
				if (!chip->chg_online) {
					chip->retention_state_ready = true;
					cancel_delayed_work(&chip->switch_end_recheck_work);
				}
			}
			break;
		case RETENTION_ITEM_DISCONNECT_COUNT:
			if (chip->irq_plugin) {
				ret = schedule_delayed_work(&chip->retention_disconnect_work,
					msecs_to_jiffies(WAIT_BC1P2_GET_TYPE));
				if (ret == 0) {
					cancel_delayed_work(&chip->retention_disconnect_work);
					ret = schedule_delayed_work(&chip->retention_disconnect_work,
						msecs_to_jiffies(WAIT_BC1P2_GET_TYPE));
					chg_info("ret:%d\n", ret);
				}
			} else {
				cancel_delayed_work(&chip->retention_disconnect_work);
				schedule_delayed_work(&chip->retention_disconnect_work, 0);
			}
			break;
		case RETENTION_ITEM_STATE_READY:
			if (!chip->chg_online) {
				chip->retention_state_ready = true;
				cancel_delayed_work(&chip->switch_end_recheck_work);
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_retention_topic(struct oplus_mms *topic,
					   void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->retention_topic = topic;
	chip->retention_subs = oplus_mms_subscribe(chip->retention_topic, chip,
					     oplus_wired_retention_subs_callback,
					     "chg_wired");
	if (IS_ERR_OR_NULL(chip->retention_subs)) {
		chg_err("subscribe retention topic error, rc=%ld\n",
			PTR_ERR(chip->retention_subs));
		return;
	}
	rc = oplus_mms_get_item_data(chip->retention_topic, RETENTION_ITEM_DISCONNECT_COUNT, &data, true);
	if (rc >= 0)
		chip->pdqc_connect_error_count = data.intval;
}

#define SALE_MODE_PDQC_DELAY msecs_to_jiffies(200)
static void oplus_wired_sale_mode_buckboost_work(struct work_struct *work)
{
	struct oplus_chg_wired *chip =
		container_of(work, struct oplus_chg_wired, sale_mode_buckboost_work);

	switch (chip->chg_mode) {
	case OPLUS_WIRED_CHG_MODE_QC:
		if (!chip->chg_ctrl_by_sale_mode)
			chip->qc_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->qc_config_work, SALE_MODE_PDQC_DELAY);
		break;
	case OPLUS_WIRED_CHG_MODE_PD:
		if (!chip->chg_ctrl_by_sale_mode)
			chip->pd_action = OPLUS_ACTION_BOOST;
		schedule_delayed_work(&chip->pd_config_work, SALE_MODE_PDQC_DELAY);
		break;
	default:
		break;
	}
}

static void oplus_wired_comm_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	struct oplus_wired_spec_config *spec = &chip->spec;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_TEMP_REGION:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->temp_region = data.intval;
			schedule_work(&chip->temp_region_update_work);
			break;
		case COMM_ITEM_FCC_GEAR:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->fcc_gear = data.intval;
			schedule_work(&chip->temp_region_update_work);
			break;
		case COMM_ITEM_COOL_DOWN:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->cool_down = data.intval;
			/*
			 * Need to recheck the type and check whether the
			 * charging voltage needs to be adjusted.
			 */
			schedule_work(&chip->chg_type_change_work);
			break;
		case COMM_ITEM_CHARGING_DISABLE:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				chg_err("can't get charging disable status, rc=%d", rc);
			else
				vote(chip->output_suspend_votable, USER_VOTER,
				     !!data.intval, data.intval, false);
			break;
		case COMM_ITEM_CHARGE_SUSPEND:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				chg_err("can't get charge suspend status, rc=%d", rc);
			else
				vote(chip->input_suspend_votable, USER_VOTER,
				     !!data.intval, data.intval, false);
			break;
		case COMM_ITEM_UNWAKELOCK:
			rc = oplus_mms_get_item_data(chip->comm_topic, id,
						     &data, false);
			if (rc < 0)
				break;
			chip->unwakelock_chg = data.intval;
			oplus_wired_set_awake(chip, !chip->unwakelock_chg);
			/* charger WDT enable/disable */
			oplus_wired_wdt_enable(chip->wired_topic,
					       !chip->unwakelock_chg);
			break;
		case COMM_ITEM_FACTORY_TEST:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			if (!!data.intval && spec->vbatt_pdqc_to_9v_thr > 0) {
				if (oplus_gauge_get_batt_num() == 2) {
					if (chip->adjust_pdqc_vol_thr_support)
						spec->vbatt_pdqc_to_9v_thr =
							FACTORY_MODE_PDQC_9V_THR;
					else
						spec->vbatt_pdqc_to_9v_thr =
							oplus_wired_get_vbatt_pdqc_to_9v_thr(chip);
					schedule_work(&chip->chg_type_change_work);
				}
			} else {
				spec->vbatt_pdqc_to_9v_thr =
					oplus_wired_get_vbatt_pdqc_to_9v_thr(
						chip);
			}
			break;
		case COMM_ITEM_LED_ON:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			schedule_work(&chip->led_on_changed_work);
			break;
		case COMM_ITEM_SALE_MODE:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->chg_ctrl_by_sale_mode = data.intval;
			schedule_work(&chip->sale_mode_buckboost_work);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_comm_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };
	int rc;

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(chip->comm_topic, chip,
					      oplus_wired_comm_subs_callback,
					      "chg_wired");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe gauge topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_TEMP_REGION, &data,
				true);
	chip->temp_region = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_FCC_GEAR, &data,
				true);
	chip->fcc_gear = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_COOL_DOWN, &data,
				true);
	chip->cool_down = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SALE_MODE, &data,
				true);
	chip->chg_ctrl_by_sale_mode = data.intval;
	rc = oplus_mms_get_item_data(chip->comm_topic,
				     COMM_ITEM_CHARGING_DISABLE, &data, true);
	if (rc < 0)
		chg_err("can't get charging disable status, rc=%d", rc);
	else
		vote(chip->output_suspend_votable, USER_VOTER, !!data.intval,
		     data.intval, false);
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_CHARGE_SUSPEND,
				     &data, true);
	if (rc < 0)
		chg_err("can't get charge suspend status, rc=%d", rc);
	else
		vote(chip->input_suspend_votable, USER_VOTER, !!data.intval,
		     data.intval, false);
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UNWAKELOCK,
				     &data, true);
	if (rc < 0) {
		chg_err("can't get unwakelock_chg status, rc=%d", rc);
		chip->unwakelock_chg = false;
	} else {
		chip->unwakelock_chg = data.intval;
		oplus_wired_set_awake(chip, !chip->unwakelock_chg);
	}
}

static void oplus_wired_vooc_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case VOOC_ITEM_VOOC_STARTED:
			oplus_mms_get_item_data(chip->vooc_topic, id, &data,
						false);
			chip->vooc_started = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_vooc_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->vooc_topic = topic;
	chip->vooc_subs = oplus_mms_subscribe(chip->vooc_topic, chip,
					      oplus_wired_vooc_subs_callback,
					      "chg_wired");
	if (IS_ERR_OR_NULL(chip->vooc_subs)) {
		chg_err("subscribe vooc topic error, rc=%ld\n",
			PTR_ERR(chip->vooc_subs));
		return;
	}

	oplus_mms_get_item_data(chip->vooc_topic, VOOC_ITEM_VOOC_STARTED, &data,
				true);
	chip->vooc_started = data.intval;
}

static void oplus_wired_cpa_subs_callback(struct mms_subscribe *subs,
					  enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_wired *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case CPA_ITEM_ALLOW:
			oplus_mms_get_item_data(chip->cpa_topic, id, &data,
						false);
			chip->cpa_current_type = data.intval;
			if (data.intval == CHG_PROTOCOL_QC)
				schedule_work(&chip->qc_check_work);
			else if (data.intval == CHG_PROTOCOL_PD)
				schedule_work(&chip->pd_check_work);
			else if (data.intval == CHG_PROTOCOL_BC12)
				oplus_cpa_switch_start(chip->cpa_topic, CHG_PROTOCOL_BC12);
			break;
		case CPA_ITEM_TIMEOUT:
			oplus_mms_get_item_data(chip->cpa_topic, id, &data,
						false);
			if (data.intval == CHG_PROTOCOL_QC) {
				chg_info("qc time out\n");
				complete(&chip->qc_check_ack);
				oplus_qc_cpa_switch_end(chip);
			} else if (data.intval == CHG_PROTOCOL_PD) {
				chg_info("pd time out\n");
				oplus_pd_cpa_switch_end(chip);
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_wired_subscribe_cpa_topic(struct oplus_mms *topic,
					    void *prv_data)
{
	struct oplus_chg_wired *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->cpa_topic = topic;
	chip->cpa_subs = oplus_mms_subscribe(chip->cpa_topic, chip,
					     oplus_wired_cpa_subs_callback,
					     "chg_wired");
	if (IS_ERR_OR_NULL(chip->cpa_subs)) {
		chg_err("subscribe cpa topic error, rc=%ld\n",
			PTR_ERR(chip->cpa_subs));
		return;
	}

	oplus_mms_get_item_data(chip->cpa_topic, CPA_ITEM_ALLOW, &data, true);
	chip->cpa_current_type = data.intval;
	if (data.intval == CHG_PROTOCOL_QC)
		schedule_work(&chip->qc_check_work);
	else if (data.intval == CHG_PROTOCOL_PD)
		schedule_work(&chip->pd_check_work);

	if (chip->cpa_support) {
		oplus_cpa_protocol_ready(chip->cpa_topic, CHG_PROTOCOL_BC12);
		oplus_cpa_protocol_ready(chip->cpa_topic, CHG_PROTOCOL_PD);
		oplus_cpa_protocol_ready(chip->cpa_topic, CHG_PROTOCOL_QC);
	}
}

static int oplus_wired_fcc_vote_callback(struct votable *votable, void *data,
					 int fcc_ma, const char *client,
					 bool step)
{
	int rc;

	if (fcc_ma < 0)
		return 0;

	rc = oplus_wired_set_fcc(fcc_ma);

	return rc;
}

static int oplus_wired_icl_vote_callback(struct votable *votable, void *data,
					 int icl_ma, const char *client,
					 bool step)
{
	struct oplus_chg_wired *chip = data;
	int rc;

	if (icl_ma < 0)
		return 0;

	chg_info("icl vote clent %s, icl_ma = %d\n", client, icl_ma);
	mutex_lock(&chip->icl_lock);
	if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_VOOC && chip->vooc_started)
		rc = oplus_wired_set_icl_by_vooc(chip->wired_topic, icl_ma);
	else
		rc = oplus_wired_set_icl(icl_ma, step);
	mutex_unlock(&chip->icl_lock);

	return rc;
}

static int oplus_wired_input_suspend_vote_callback(struct votable *votable,
						   void *data, int disable,
						   const char *client,
						   bool step)
{
	struct oplus_chg_wired *chip = data;
	static bool suspend = true;
	static bool suspend_check_only = false;
	int rc;

	if (strcmp(client, SHUTDOWN_VOTER) == 0)
		suspend_check_only = true;

	chg_info("charger suspend change to %s by %s suspend_check_only %s\n",
		 disable ? "true" : "false", client,
		 suspend_check_only ? "true" : "false");

	if (chip->chg_online && !disable && !suspend_check_only) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHARGE_SUSPEND_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	rc = oplus_wired_input_enable(!disable);

	if (suspend_check_only)
		return rc;

	if (chip->chg_online && disable) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHARGE_SUSPEND_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	/* Restore current setting */
	if (!disable && suspend) {
		chg_info("rerun icl vote\n");
		suspend = false;
		schedule_work(&chip->icl_changed_work);
	} else {
		suspend = disable;
	}

	return rc;
}

static int oplus_wired_output_suspend_vote_callback(struct votable *votable,
						    void *data, int disable,
						    const char *client,
						    bool step)
{
	struct oplus_chg_wired *chip = data;
	static bool suspend = true;
	static bool suspend_check_only = false;
	int rc;

	if (strcmp(client, SHUTDOWN_VOTER) == 0)
		suspend_check_only = true;

	chg_info("charging disabled change to %s by %s  suspend_check_only %s\n",
		 disable ? "true" : "false", client,
		 suspend_check_only ? "true" : "false");


	if (chip->chg_online && !disable && !suspend_check_only) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHAEGE_DISABLE_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	if (oplus_get_chg_spec_version() >= OPLUS_CHG_SPEC_VER_V3P7 && chip->chg_online && disable) {
		vote(chip->fcc_votable, CHAEGE_DISABLE_VOTER, true, chip->spec.non_standard_ibatmax_ma, false);
		usleep_range(10000, 10000);
	}

	rc = oplus_wired_output_enable(!disable);

	if (suspend_check_only)
		return rc;

	if (chip->chg_online && disable) {
		if (is_vooc_chg_auto_mode_votable_available(chip))
			vote(chip->vooc_chg_auto_mode_votable,
			     CHAEGE_DISABLE_VOTER, disable, disable, false);
		else
			chg_err("vooc_chg_auto_mode_votable not found\n");
	}

	if (oplus_get_chg_spec_version() >= OPLUS_CHG_SPEC_VER_V3P7 && !disable)
		vote(chip->fcc_votable, CHAEGE_DISABLE_VOTER, false, 0, false);

	/* Restore current setting */
	if (!disable && suspend) {
		chg_info("rerun fcc/icl vote\n");
		suspend = false;
		schedule_work(&chip->fcc_changed_work);
		schedule_work(&chip->icl_changed_work);
	} else {
		suspend = disable;
	}

	return rc;
}

static int oplus_wired_pd_boost_disable_vote_callback(struct votable *votable,
						      void *data, int disable,
						      const char *client,
						      bool step)
{
	struct oplus_chg_wired *chip = data;

	chip->pd_boost_disable = !!disable;
	if (chip->pd_boost_disable)
		chg_info("pd boost disable by %s\n", client);
	else
		chg_info("pd boost enable\n");

	return 0;
}

static int oplus_wired_vote_init(struct oplus_chg_wired *chip)
{
	int rc;

	chip->fcc_votable = create_votable("WIRED_FCC", VOTE_MIN,
					   oplus_wired_fcc_vote_callback, chip);
	if (IS_ERR(chip->fcc_votable)) {
		rc = PTR_ERR(chip->fcc_votable);
		chip->fcc_votable = NULL;
		return rc;
	}

	chip->icl_votable = create_votable("WIRED_ICL", VOTE_MIN,
					   oplus_wired_icl_vote_callback, chip);
	if (IS_ERR(chip->icl_votable)) {
		rc = PTR_ERR(chip->icl_votable);
		chip->icl_votable = NULL;
		goto create_icl_votable_err;
	}

	chip->input_suspend_votable =
		create_votable("WIRED_CHARGE_SUSPEND", VOTE_SET_ANY,
			       oplus_wired_input_suspend_vote_callback, chip);
	if (IS_ERR(chip->input_suspend_votable)) {
		rc = PTR_ERR(chip->input_suspend_votable);
		chip->input_suspend_votable = NULL;
		goto create_input_suspend_votable_err;
	}

	chip->output_suspend_votable =
		create_votable("WIRED_CHARGING_DISABLE", VOTE_SET_ANY,
			       oplus_wired_output_suspend_vote_callback, chip);
	if (IS_ERR(chip->output_suspend_votable)) {
		rc = PTR_ERR(chip->output_suspend_votable);
		chip->output_suspend_votable = NULL;
		goto create_output_suspend_votable_err;
	}

	chip->pd_boost_disable_votable =
		create_votable("PD_BOOST_DISABLE", VOTE_SET_ANY,
			       oplus_wired_pd_boost_disable_vote_callback,
			       chip);
	if (IS_ERR(chip->pd_boost_disable_votable)) {
		rc = PTR_ERR(chip->pd_boost_disable_votable);
		chip->pd_boost_disable_votable = NULL;
		goto create_pd_boost_disable_votable_err;
	}
	/* boost is disabled by default, need to wait for SVID recognition */
	vote(chip->pd_boost_disable_votable, SVID_VOTER, true, 1, false);

	return 0;

create_pd_boost_disable_votable_err:
	destroy_votable(chip->output_suspend_votable);
create_output_suspend_votable_err:
	destroy_votable(chip->input_suspend_votable);
create_input_suspend_votable_err:
	destroy_votable(chip->icl_votable);
create_icl_votable_err:
	destroy_votable(chip->fcc_votable);
	return rc;
}


/*
 * oplus,unknown_strategy_name
 * oplus,sdp_strategy_name
 * oplus,cdp_strategy_name
 * oplus,dcp_strategy_name
 * oplus,vooc_strategy_name
 * oplus,qc_strategy_name
 * oplus,pd_strategy_name
 *
 * oplus,unknown_strategy_data
 * oplus,sdp_strategy_data
 * oplus,cdp_strategy_data
 * oplus,dcp_strategy_data
 * oplus,vooc_strategy_data
 * oplus,qc_strategy_data
 * oplus,pd_strategy_data
 */
static void oplus_wired_parse_strategy_dt(struct oplus_chg_wired *chip, struct device_node *node)
{
	struct oplus_wired_config *config = &chip->config;
	int i, rc;
	char strategy_name_dt[32] = { 0 };
	char strategy_data_dt[32] = { 0 };

	for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
		snprintf(strategy_name_dt, sizeof(strategy_name_dt), "oplus,%s_strategy_name",
			 oplus_wired_get_chg_mode_region_str(i));
		snprintf(strategy_data_dt, sizeof(strategy_data_dt), "oplus,%s_strategy_data",
			 oplus_wired_get_chg_mode_region_str(i));
		rc = of_property_read_string(node, strategy_name_dt, (const char **)&config->strategy_name[i]);
		if (rc < 0)
			continue;
		chg_info("%s=%s\n", strategy_name_dt, config->strategy_name[i]);
		rc = oplus_chg_strategy_read_data(chip->dev, strategy_data_dt, &config->strategy_data[i]);
		if (rc < 0) {
			chg_err("read %s failed, rc=%d\n", strategy_data_dt, rc);
			config->strategy_data[i] = NULL;
			config->strategy_data_size[i] = 0;
		} else {
			chg_info("%s size is %d\n", strategy_data_dt, rc);
			config->strategy_data_size[i] = rc;
		}
	}
}

static int oplus_wired_parse_dt(struct oplus_chg_wired *chip)
{
	struct oplus_wired_spec_config *spec = &chip->spec;
	struct device_node *node = oplus_get_node_by_type(chip->dev->of_node);
	int i, m;
	int rc;

	chip->vooc_support = of_property_read_bool(node, "oplus,vooc-support");
	chip->adjust_pdqc_vol_thr_support = of_property_read_bool(node,
						"oplus,adjust-pdqc-vol-thr-support");

	rc = of_property_read_u32(node, "oplus_spec,pd-iclmax-ma",
				  &spec->pd_iclmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,pd-iclmax-ma reading failed, rc=%d\n", rc);
		spec->pd_iclmax_ma = default_config.pd_iclmax_ma;
	}
	rc = of_property_read_u32(node, "oplus_spec,qc-iclmax-ma",
				  &spec->qc_iclmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,qc-iclmax-ma reading failed, rc=%d\n", rc);
		spec->qc_iclmax_ma = default_config.qc_iclmax_ma;
	}
	rc = of_property_read_u32(node, "oplus_spec,non-standard-ibatmax-ma",
				  &spec->non_standard_ibatmax_ma);
	if (rc < 0) {
		chg_err("oplus_spec,non-standard-ibatmax-ma reading failed, rc=%d\n",
			rc);
		spec->non_standard_ibatmax_ma =
			default_config.non_standard_ibatmax_ma;
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,input-power-mw",
					  (u32 *)(spec->input_power_mw),
					  OPLUS_WIRED_CHG_MODE_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,input-power-mw error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++)
			spec->input_power_mw[i] =
				default_config.input_power_mw[i];
	}

	rc = read_unsigned_temp_region_data(node, "oplus_spec,led_on-fccmax-ma",
					  (u32 *)(spec->led_on_fcc_max_ma),
					  oplus_comm_get_temp_region_max(), TEMP_REGION_MAX, 1,
					  oplus_comm_temp_region_map);
	if (rc < 0) {
		chg_err("get oplus_spec,led_on-fccmax-ma error, rc=%d\n", rc);
		for (i = 0; i < TEMP_REGION_MAX; i++)
			spec->led_on_fcc_max_ma[i] =
				default_config.led_on_fcc_max_ma[i];
	}

	rc = read_unsigned_temp_region_data(
		node, "oplus_spec,fccmax-ma-lv", (u32 *)(spec->fcc_ma[0]),
		oplus_comm_get_temp_region_max(), TEMP_REGION_MAX, OPLUS_WIRED_CHG_MODE_MAX,
		oplus_comm_temp_region_map);
	if (rc < 0) {
		chg_err("get oplus_spec,fccmax-ma-lv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
			for (m = 0; m < TEMP_REGION_MAX; m++)
				spec->fcc_ma[0][i][m] =
					default_config.fcc_ma[0][i][m];
		}
	}

	rc = read_unsigned_temp_region_data(
		node, "oplus_spec,fccmax-ma-hv", (u32 *)(spec->fcc_ma[1]),
		oplus_comm_get_temp_region_max(), TEMP_REGION_MAX, OPLUS_WIRED_CHG_MODE_MAX,
		oplus_comm_temp_region_map);
	if (rc < 0) {
		chg_err("get oplus_spec,fccmax-ma-hv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
			for (m = 0; m < TEMP_REGION_MAX; m++)
				spec->fcc_ma[1][i][m] =
					default_config.fcc_ma[1][i][m];
		}
	}

	rc = of_property_read_u32(node, "oplus_spec,vbatt_pdqc_to_5v_thr",
				  &spec->vbatt_pdqc_to_5v_thr);
	if (rc < 0) {
		chg_err("oplus_spec,vbatt_pdqc_to_5v_thr reading failed, rc=%d\n",
			rc);
		spec->vbatt_pdqc_to_5v_thr =
			default_config.vbatt_pdqc_to_5v_thr;
	}
	rc = of_property_read_u32(node, "oplus_spec,vbatt_pdqc_to_9v_thr",
				  &spec->vbatt_pdqc_to_9v_thr);
	if (rc < 0) {
		chg_err("oplus_spec,vbatt_pdqc_to_9v_thr reading failed, rc=%d\n",
			rc);
		spec->vbatt_pdqc_to_9v_thr =
			default_config.vbatt_pdqc_to_9v_thr;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_pdqc_vol_mv",
					  (u32 *)(spec->cool_down_pdqc_vol_mv),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_pdqc_vol_mv error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_pdqc_vol_mv[i] =
				default_config.cool_down_pdqc_vol_mv[i];
			spec->cool_down_pdqc_level_max =
				default_config.cool_down_pdqc_level_max;
		}
	} else {
		spec->cool_down_pdqc_level_max = rc;
	}
	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_pdqc_curr_ma",
					  (u32 *)(spec->cool_down_pdqc_curr_ma),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0 || spec->cool_down_pdqc_level_max != rc) {
		chg_err("get oplus_spec,cool_down_pdqc_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_pdqc_curr_ma[i] =
				default_config.cool_down_pdqc_curr_ma[i];
			spec->cool_down_pdqc_level_max =
				default_config.cool_down_pdqc_level_max;
		}
	}

	rc = of_property_read_u32(node, "oplus_spec,cool_down_sale_pdqc_vol_mv",
					  &spec->cool_down_sale_pdqc_vol_mv);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_sale_pdqc_vol_mv error, rc=%d\n",
			rc);
		spec->cool_down_sale_pdqc_vol_mv = default_config.cool_down_sale_pdqc_vol_mv;
	}
	rc = of_property_read_u32(node, "oplus_spec,cool_down_sale_pdqc_curr_ma",
					  &spec->cool_down_sale_pdqc_curr_ma);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_sale_pdqc_curr_ma error, rc=%d\n",
			rc);
		spec->cool_down_sale_pdqc_curr_ma = default_config.cool_down_sale_pdqc_curr_ma;
	}

	rc = read_unsigned_data_from_node(node,
					  "oplus_spec,cool_down_vooc_curr_ma",
					  (u32 *)(spec->cool_down_vooc_curr_ma),
					  WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_vooc_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_vooc_curr_ma[i] =
				default_config.cool_down_vooc_curr_ma[i];
			spec->cool_down_vooc_level_max =
				default_config.cool_down_vooc_level_max;
		}
	} else {
		spec->cool_down_vooc_level_max = rc;
	}
	rc = read_unsigned_data_from_node(
		node, "oplus_spec,cool_down_normal_curr_ma",
		(u32 *)(spec->cool_down_normal_curr_ma),
		WIRED_COOL_DOWN_LEVEL_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,cool_down_normal_curr_ma error, rc=%d\n",
			rc);
		for (i = 0; i < WIRED_COOL_DOWN_LEVEL_MAX; i++) {
			spec->cool_down_normal_curr_ma[i] =
				default_config.cool_down_normal_curr_ma[i];
			spec->cool_down_normal_level_max =
				default_config.cool_down_normal_level_max;
		}
	} else {
		spec->cool_down_normal_level_max = rc;
	}

	rc = read_unsigned_data_from_node(node, "oplus_spec,vbus_ov_thr_mv",
					  (u32 *)(spec->vbus_ov_thr_mv),
					  OPLUS_VBUS_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,vbus_ov_thr_mv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_VBUS_MAX; i++)
			spec->vbus_ov_thr_mv[i] =
				default_config.vbus_ov_thr_mv[i];
	}
	rc = read_unsigned_data_from_node(node, "oplus_spec,vbus_uv_thr_mv",
					  (u32 *)(spec->vbus_uv_thr_mv),
					  OPLUS_VBUS_MAX);
	if (rc < 0) {
		chg_err("get oplus_spec,vbus_uv_thr_mv error, rc=%d\n", rc);
		for (i = 0; i < OPLUS_VBUS_MAX; i++)
			spec->vbus_uv_thr_mv[i] =
				default_config.vbus_uv_thr_mv[i];
	}

	oplus_wired_parse_strategy_dt(chip, node);
	return 0;
}

static int oplus_wired_strategy_init(struct oplus_chg_wired *chip)
{
	struct oplus_wired_config *config = &chip->config;
	int i = 0;

	for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
		chip->strategy[i] = oplus_chg_strategy_alloc(config->strategy_name[i], config->strategy_data[i], config->strategy_data_size[i]);
		if (chip->strategy[i] == NULL)
			chg_err("%s strategy alloc error\n", oplus_wired_get_chg_mode_region_str(i));
		devm_kfree(chip->dev, chip->config.strategy_data[i]);
		chip->config.strategy_data[i] = NULL;
	}

	return 0;
}

static void oplus_wired_shutdown(struct platform_device *pdev)
{
	struct oplus_chg_wired *chip = platform_get_drvdata(pdev);

	if (!chip || !chip->chg_online) {
		chg_err("chip NULL or charger not online");
		return;
	}

	chip->chg_type = oplus_wired_get_chg_type();
	chg_info("wired_type=%s, chg_mode = %d\n", oplus_wired_get_chg_type_str(chip->chg_type),
		chip->chg_mode);
	switch (chip->chg_type) {
	case OPLUS_CHG_USB_TYPE_PD:
	case OPLUS_CHG_USB_TYPE_PD_DRP:
	case OPLUS_CHG_USB_TYPE_PD_PPS:
		if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_PD) {
			oplus_wired_set_pd_config(OPLUS_PD_5V_PDO);
			msleep(OPLUS_CHG_SHUTDOWN_WAIT);
		}
		break;
	case OPLUS_CHG_USB_TYPE_QC2:
	case OPLUS_CHG_USB_TYPE_QC3:
		if (chip->chg_mode == OPLUS_WIRED_CHG_MODE_QC) {
			oplus_wired_set_qc_config(OPLUS_CHG_QC_2_0, OPLUS_CHG_VBUS_5V);
			msleep(OPLUS_CHG_SHUTDOWN_WAIT);
		}
		break;
	default:
		break;
	}
	return;
}

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#include "config/dynamic_cfg/oplus_wired_cfg.c"
#endif

static int oplus_wired_probe(struct platform_device *pdev)
{
	struct oplus_chg_wired *chip;
	int i, rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_wired),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);

	rc = oplus_wired_parse_dt(chip);
	if (rc < 0)
		goto parse_dt_err;

	/*
	 * We need to initialize the resources that may be used in the
	 * subsequent initialization process in advance
	 */
	init_completion(&chip->qc_action_ack);
	init_completion(&chip->pd_action_ack);
	init_completion(&chip->qc_check_ack);
	init_completion(&chip->pd_check_ack);
	INIT_WORK(&chip->plugin_work, oplus_wired_plugin_work);
	INIT_WORK(&chip->chg_type_change_work,
		  oplus_wired_chg_type_change_work);
	INIT_WORK(&chip->temp_region_update_work,
		  oplus_wired_temp_region_update_work);
	INIT_WORK(&chip->gauge_update_work, oplus_wired_gauge_update_work);
	INIT_DELAYED_WORK(&chip->switch_end_recheck_work, oplus_pdqc_switch_end_recheck_work);
	INIT_DELAYED_WORK(&chip->pd_boost_icl_disable_work, oplus_wired_pd_boost_icl_disable_work);
	INIT_DELAYED_WORK(&chip->qc_config_work, oplus_wired_qc_config_work);
	INIT_DELAYED_WORK(&chip->pd_config_work, oplus_wired_pd_config_work);
	INIT_DELAYED_WORK(&chip->retention_disconnect_work,
		  oplus_pdqc_retention_disconnect_work);
	INIT_WORK(&chip->charger_current_changed_work,
		  oplus_wired_charger_current_changed_work);
	INIT_WORK(&chip->led_on_changed_work, oplus_wired_led_on_changed_work);
	INIT_WORK(&chip->icl_changed_work, oplus_wired_icl_changed_work);
	INIT_WORK(&chip->fcc_changed_work, oplus_wired_fcc_changed_work);
	INIT_WORK(&chip->qc_check_work, oplus_wired_qc_check_work);
	INIT_WORK(&chip->pd_check_work, oplus_wired_pd_check_work);
	INIT_WORK(&chip->sale_mode_buckboost_work, oplus_wired_sale_mode_buckboost_work);

	chip->cpa_support = oplus_cpa_support();

	rc = oplus_wired_vote_init(chip);
	if (rc < 0)
		goto vote_init_err;

	rc = oplus_wired_strategy_init(chip);
	if (rc < 0)
		goto strategy_init_err;

	oplus_wired_variables_init(chip);

	oplus_wired_awake_init(chip);

	oplus_mms_wait_topic("gauge", oplus_wired_subscribe_gauge_topic, chip);
	oplus_mms_wait_topic("wired", oplus_wired_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("common", oplus_wired_subscribe_comm_topic, chip);
	oplus_mms_wait_topic("vooc", oplus_wired_subscribe_vooc_topic, chip);
	oplus_mms_wait_topic("cpa", oplus_wired_subscribe_cpa_topic, chip);
	oplus_mms_wait_topic("retention", oplus_wired_subscribe_retention_topic, chip);

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	(void)oplus_wired_reg_debug_config(chip);
#endif

	chg_info("probe success\n");
	return 0;

strategy_init_err:
	destroy_votable(chip->pd_boost_disable_votable);
	destroy_votable(chip->output_suspend_votable);
	destroy_votable(chip->input_suspend_votable);
	destroy_votable(chip->icl_votable);
	destroy_votable(chip->fcc_votable);
vote_init_err:
	for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
		if (chip->config.strategy_data[i])
			devm_kfree(&pdev->dev, chip->config.strategy_data[i]);
	}
parse_dt_err:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);
	chg_err("probe error, rc=%d\n", rc);
	return rc;
}

static int oplus_wired_remove(struct platform_device *pdev)
{
	struct oplus_chg_wired *chip = platform_get_drvdata(pdev);
	int i = 0;

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	oplus_wired_unreg_debug_config(chip);
#endif
	if (!IS_ERR_OR_NULL(chip->comm_subs))
		oplus_mms_unsubscribe(chip->comm_subs);
	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);
	if (!IS_ERR_OR_NULL(chip->gauge_subs))
		oplus_mms_unsubscribe(chip->gauge_subs);
	if (!IS_ERR_OR_NULL(chip->cpa_subs))
		oplus_mms_unsubscribe(chip->cpa_subs);
	if (!IS_ERR_OR_NULL(chip->retention_subs))
		oplus_mms_unsubscribe(chip->retention_subs);
	oplus_wired_awake_exit(chip);
	for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
		if (chip->strategy[i])
			oplus_chg_strategy_release(chip->strategy[i]);
	}
	destroy_votable(chip->pd_boost_disable_votable);
	destroy_votable(chip->output_suspend_votable);
	destroy_votable(chip->input_suspend_votable);
	destroy_votable(chip->icl_votable);
	destroy_votable(chip->fcc_votable);
	for (i = 0; i < OPLUS_WIRED_CHG_MODE_MAX; i++) {
		if (chip->config.strategy_data[i])
			devm_kfree(&pdev->dev, chip->config.strategy_data[i]);
	}
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id oplus_wired_match[] = {
	{ .compatible = "oplus,wired" },
	{},
};

static struct platform_driver oplus_wired_driver = {
	.driver		= {
		.name = "oplus-wired",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_wired_match),
	},
	.probe		= oplus_wired_probe,
	.remove		= oplus_wired_remove,
	.shutdown   = oplus_wired_shutdown,
};

static __init int oplus_wired_init(void)
{
	return platform_driver_register(&oplus_wired_driver);
}

static __exit void oplus_wired_exit(void)
{
	platform_driver_unregister(&oplus_wired_driver);
}

oplus_chg_module_register(oplus_wired);
