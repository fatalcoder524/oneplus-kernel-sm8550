// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[ADSP]([%s][%d]): " fmt, __func__, __LINE__

#ifdef OPLUS_FEATURE_CHG_BASIC
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/of_platform.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_mms_wired.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_voter.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms.h>
#include <../voocphy/oplus_adsp_voocphy.h>
#include "oplus_hal_sm8450.h"
#include <oplus_chg_ufcs.h>
#include <oplus_impedance_check.h>
#include <plat_ufcs/plat_ufcs_notify.h>
#include <oplus_chg_cpa.h>

#define BCC_TYPE_IS_SVOOC 1
#define BCC_TYPE_IS_VOOC 0
#define LCM_CHECK_COUNT 5
#define LCM_CHARGER_VOL_THR_MV 2500
#define LCM_FREQUENCY_INTERVAL 5000
#define CPU_CLOCK_TIME_MS	1000000
#define OPLUS_HVDCP_DISABLE_INTERVAL round_jiffies_relative(msecs_to_jiffies(15000))
#define OPLUS_HVDCP_DETECT_TO_DETACH_TIME 3600
#define OEM_MISC_CTL_DATA_PAIR(cmd, enable) ((enable ? 0x3 : 0x1) << cmd)
#define OPLUS_PD_ONLY_CHECK_INTERVAL round_jiffies_relative(msecs_to_jiffies(500))
#define OPLUS_RERUN_AICL_THRESHOLD_MS	100
#define ICL_IBUS_ABS_THRESHOLD_MA	600

struct battery_chg_dev *g_bcdev = NULL;
static int oplus_get_vchg_trig_status(void);
static bool oplus_vchg_trig_is_support(void);
extern void oplus_usb_set_none_role(void);
extern void oplus_dwc3_config_usbphy_pfunc(bool (*pfunc)(void));
static int oplus_get_voocphy_enable(struct battery_chg_dev *bcdev);
static int oplus_voocphy_enable(struct battery_chg_dev *bcdev, bool enable);
static int fg_sm8350_get_battery_soc(void);
static int oplus_chg_8350_output_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend);
static int oplus_chg_8350_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type);
#endif /*OPLUS_FEATURE_CHG_BASIC*/
static int oplus_chg_8350_get_icl(struct oplus_chg_ic_dev *ic_dev, int *icl_ma);
static int oplus_chg_set_input_current_with_no_aicl(struct battery_chg_dev *bcdev, int current_ma);
static int oplus_chg_8350_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma);
static int oplus_chg_8350_aicl_rerun(struct oplus_chg_ic_dev *ic_dev);

#ifdef OPLUS_FEATURE_CHG_BASIC
/*for p922x compile*/
void __attribute__((weak)) oplus_set_wrx_otg_value(void)
{
	return;
}
int __attribute__((weak)) oplus_get_idt_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_en_val(void)
{
	return -1;
}
int __attribute__((weak)) oplus_get_wrx_otg_val(void)
{
	return 0;
}
void __attribute__((weak)) oplus_wireless_set_otg_en_val(void)
{
	return;
}
void __attribute__((weak)) oplus_dcin_irq_enable(void)
{
	return;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int oplus_chg_disable_charger(bool disable, const char *client_str)
{
	struct votable *disable_votable;
	int rc;

	disable_votable = find_votable("WIRED_CHARGING_DISABLE");
	if (!disable_votable) {
		chg_err("WIRED_CHARGING_DISABLE votable not found\n");
		return -EINVAL;
	}

	rc = vote(disable_votable, client_str, disable, 1, false);
	if (rc < 0)
		chg_err("%s charger error, rc = %d\n",
			     disable ? "disable" : "enable", rc);
	else
		chg_info("%s charger\n", disable ? "disable" : "enable");

	return rc;
}

static int oplus_chg_suspend_charger(bool suspend, const char *client_str)
{
	struct votable *suspend_votable;
	int rc;

	suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	if (!suspend_votable) {
		chg_err("WIRED_CHARGE_SUSPEND votable not found\n");
		return -EINVAL;
	}

	rc = vote(suspend_votable, client_str, suspend, 1, false);
	if (rc < 0)
		chg_err("%s charger error, rc = %d\n",
			     suspend ? "suspend" : "unsuspend", rc);
	else
		chg_info("%s charger\n", suspend ? "suspend" : "unsuspend");

	return rc;
}

__maybe_unused static bool is_usb_psy_available(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (!pst->psy)
		pst->psy = power_supply_get_by_name("usb");
	return !!pst->psy;
}

__maybe_unused static bool is_batt_psy_available(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	if (!pst->psy)
		pst->psy = power_supply_get_by_name("battery");
	return !!pst->psy;
}

__maybe_unused static bool is_wls_psy_available(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	if (!pst->psy)
		pst->psy = power_supply_get_by_name("wireless");
	return !!pst->psy;
}

static const int battery_prop_map[BATT_PROP_MAX] = {
	[BATT_STATUS]		= POWER_SUPPLY_PROP_STATUS,
	[BATT_HEALTH]		= POWER_SUPPLY_PROP_HEALTH,
	[BATT_PRESENT]		= POWER_SUPPLY_PROP_PRESENT,
	[BATT_CHG_TYPE]		= POWER_SUPPLY_PROP_CHARGE_TYPE,
	[BATT_CAPACITY]		= POWER_SUPPLY_PROP_CAPACITY,
	[BATT_VOLT_OCV]		= POWER_SUPPLY_PROP_VOLTAGE_OCV,
	[BATT_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[BATT_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[BATT_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[BATT_CHG_CTRL_LIM]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	[BATT_CHG_CTRL_LIM_MAX]	= POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	[BATT_TEMP]		= POWER_SUPPLY_PROP_TEMP,
	[BATT_TECHNOLOGY]	= POWER_SUPPLY_PROP_TECHNOLOGY,
	[BATT_CHG_COUNTER]	= POWER_SUPPLY_PROP_CHARGE_COUNTER,
	[BATT_CYCLE_COUNT]	= POWER_SUPPLY_PROP_CYCLE_COUNT,
	[BATT_CHG_FULL_DESIGN]	= POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	[BATT_CHG_FULL]		= POWER_SUPPLY_PROP_CHARGE_FULL,
	[BATT_MODEL_NAME]	= POWER_SUPPLY_PROP_MODEL_NAME,
	[BATT_TTF_AVG]		= POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	[BATT_TTE_AVG]		= POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	[BATT_POWER_NOW]	= POWER_SUPPLY_PROP_POWER_NOW,
	[BATT_POWER_AVG]	= POWER_SUPPLY_PROP_POWER_AVG,
};

static const int usb_prop_map[USB_PROP_MAX] = {
	[USB_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[USB_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[USB_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[USB_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[USB_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[USB_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[USB_ADAP_TYPE]		= POWER_SUPPLY_PROP_USB_TYPE,
	[USB_TEMP]		= POWER_SUPPLY_PROP_TEMP,
};

static const int wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
};

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "DCP", "CDP", "ACA", "C",
	"PD", "PD_DRP", "PD_PPS", "BrickID"
};

/* Custom usb_type definitions */
static const char * const qc_power_supply_usb_type_text[] = {
	"HVDCP", "HVDCP_3", "HVDCP_3P5"
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oem_battery_chg_write(struct battery_chg_dev *bcdev, void *data,
	int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		chg_err("glink state is down\n");
		return -ENOTCONN;
	}

	mutex_lock(&bcdev->read_buffer_lock);
	reinit_completion(&bcdev->oem_read_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->oem_read_ack,
			msecs_to_jiffies(OEM_READ_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->read_buffer_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	mutex_unlock(&bcdev->read_buffer_lock);

	return rc;
}

static int oem_read_buffer(struct battery_chg_dev *bcdev)
{
	struct oem_read_buffer_req_msg req_msg = { { 0 } };

	req_msg.data_size = sizeof(bcdev->read_buffer_dump.data_buffer);
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_READ_BUFFER;

	return oem_battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

void oplus_get_props_from_adsp_by_buffer(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is null, oplus_get_batt_argv_buffer\n");
		return;
	}
	oem_read_buffer(bcdev);
}

static void handle_oem_read_buffer(struct battery_chg_dev *bcdev,
	struct oem_read_buffer_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;

	chg_debug("correct length received: %zu expected: %lu\n", len,
		  sizeof(bcdev->read_buffer_dump));

	if (len > sizeof(bcdev->read_buffer_dump)) {
		chg_err("Incorrect length received: %zu expected: %lu\n", len,
		sizeof(bcdev->read_buffer_dump));
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->read_buffer_dump.data_buffer)) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	if (buf_len == 0) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);

	complete(&bcdev->oem_read_ack);
}

static bool oplus_vooc_get_fastchg_ing(struct battery_chg_dev *bcdev);
static int oplus_vooc_get_fast_chg_type(struct battery_chg_dev *bcdev);
static int bcc_battery_chg_write(struct battery_chg_dev *bcdev, void *data,
	int len)
{
	int rc;

	if ((NULL == bcdev) || (NULL == data)) {
		chg_err("bcdev is NULL");
		return -ENODEV;
	}

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		chg_err("glink state is down\n");
		return -ENOTCONN;
	}

	mutex_lock(&bcdev->bcc_read_buffer_lock);
	reinit_completion(&bcdev->bcc_read_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->bcc_read_ack,
			msecs_to_jiffies(OEM_READ_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->bcc_read_buffer_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->bcc_read_buffer_lock);

	return rc;
}

static int bcc_read_buffer(struct battery_chg_dev *bcdev)
{
	struct oem_read_buffer_req_msg req_msg = { { 0 } };

	if (NULL == bcdev) {
		chg_err("bcdev is NULL");
		return -ENODEV;
	}

	req_msg.data_size = sizeof(bcdev->bcc_read_buffer_dump.data_buffer);
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BCC_OPCODE_READ_BUFFER;

	return bcc_battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static void handle_bcc_read_buffer(struct battery_chg_dev *bcdev,
	struct oem_read_buffer_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;
	struct oplus_mms *wired_topic;

	if ((NULL == bcdev) || (NULL == resp_msg)) {
		chg_err("bcdev is NULL");
		return;
	}

	if (len > sizeof(bcdev->bcc_read_buffer_dump)) {
		chg_err("Incorrect length received: %zu expected: %u\n", len,
		(unsigned int)sizeof(bcdev->bcc_read_buffer_dump));
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->bcc_read_buffer_dump.data_buffer)) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	if (buf_len == 0) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->bcc_read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);

	if (oplus_vooc_get_fastchg_ing(bcdev)
		&& oplus_vooc_get_fast_chg_type(bcdev) != CHARGER_SUBTYPE_FASTCHG_VOOC) {
		bcdev->bcc_read_buffer_dump.data_buffer[15] = 1;
	} else {
		bcdev->bcc_read_buffer_dump.data_buffer[15] = 0;
	}

	if (bcdev->bcc_read_buffer_dump.data_buffer[9] == 0) {
		bcdev->bcc_read_buffer_dump.data_buffer[15] = 0;
	}

	bcdev->bcc_read_buffer_dump.data_buffer[8] = DIV_ROUND_CLOSEST((int)bcdev->bcc_read_buffer_dump.data_buffer[8], 1000);

	wired_topic = oplus_mms_get_by_name("wired");
	bcdev->bcc_read_buffer_dump.data_buffer[16] = oplus_wired_get_bcc_curr_done_status(wired_topic);

	bcdev->bcc_read_buffer_dump.data_buffer[18] = 0; /* DOUBLE_SERIES_WOUND_CELLS; */

	chg_info("----dod0_1[%d], dod0_2[%d], dod0_passed_q[%d], qmax_1[%d], qmax_2[%d], qmax_passed_q[%d], "
		"voltage_cell1[%d], temperature[%d], batt_current[%d], max_current[%d], min_current[%d], voltage_cell2[%d], "
		"soc_ext_1[%d], soc_ext_2[%d], atl_last_geat_current[%d], charging_flag[%d], bcc_curr_done[%d], guage[%d], batt_type[%d]",
		bcdev->bcc_read_buffer_dump.data_buffer[0], bcdev->bcc_read_buffer_dump.data_buffer[1], bcdev->bcc_read_buffer_dump.data_buffer[2],
		bcdev->bcc_read_buffer_dump.data_buffer[3], bcdev->bcc_read_buffer_dump.data_buffer[4], bcdev->bcc_read_buffer_dump.data_buffer[5],
		bcdev->bcc_read_buffer_dump.data_buffer[6], bcdev->bcc_read_buffer_dump.data_buffer[7], bcdev->bcc_read_buffer_dump.data_buffer[8],
		bcdev->bcc_read_buffer_dump.data_buffer[9], bcdev->bcc_read_buffer_dump.data_buffer[10], bcdev->bcc_read_buffer_dump.data_buffer[11],
		bcdev->bcc_read_buffer_dump.data_buffer[12], bcdev->bcc_read_buffer_dump.data_buffer[13], bcdev->bcc_read_buffer_dump.data_buffer[14],
		bcdev->bcc_read_buffer_dump.data_buffer[15], bcdev->bcc_read_buffer_dump.data_buffer[16], bcdev->bcc_read_buffer_dump.data_buffer[17],
		bcdev->bcc_read_buffer_dump.data_buffer[18]);
	complete(&bcdev->bcc_read_ack);
}

#define BCC_SET_DEBUG_PARMS 1
#define BCC_PARMS_COUNT 19
#define BCC_PAGE_SIZE 256
#define BCC_N_DEBUG 0
#define BCC_Y_DEBUG 1
static int bcc_debug_mode  = BCC_N_DEBUG;
static char bcc_debug_buf[BCC_PAGE_SIZE] = {0};
static int oplus_get_bcc_parameters_from_adsp(struct oplus_chg_ic_dev *ic_dev, char *buf)
{
	int ret = 0;
	struct battery_chg_dev *bcdev;
	u8 tmpbuf[PAGE_SIZE] = {0};
	int len = 0;
	int i = 0;
	int idx = 0;

	if ((ic_dev == NULL) || (buf == NULL)) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (!bcdev) {
		chg_err("!!!bcdev null, oplus_get_batt_argv_buffer\n");
		return -1;
	}

	ret = bcc_read_buffer(bcdev);

	for (i = 0; i < BCC_PARMS_COUNT - 1; i++) {
		len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx,
			"%d,", bcdev->bcc_read_buffer_dump.data_buffer[i]);
		memcpy(&buf[idx], tmpbuf, len);
		idx += len;
	}
	len = snprintf(tmpbuf, BCC_PAGE_SIZE - idx,
		"%d", bcdev->bcc_read_buffer_dump.data_buffer[i]);
	memcpy(&buf[idx], tmpbuf, len);
#ifdef BCC_SET_DEBUG_PARMS
	if (bcc_debug_mode & BCC_Y_DEBUG) {
		memcpy(&buf[0], bcc_debug_buf, BCC_PAGE_SIZE);
		chg_err("bcc_debug_buf:%s\n", bcc_debug_buf);
		return ret;
	}
#endif
	chg_info("buf:%s\n", buf);
	return ret;
}

#define BCC_DEBUG_PARAM_SIZE 8
static int oplus_set_bcc_debug_parameters(struct oplus_chg_ic_dev *ic_dev, const char *buf)
{
	int ret = 0;
#ifdef BCC_SET_DEBUG_PARMS
	char temp_buf[10] = {0};
#endif

	if ((ic_dev == NULL) || (buf == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

#ifdef BCC_SET_DEBUG_PARMS
	if (strlen(buf) <= BCC_PAGE_SIZE) {
		if (strncpy(temp_buf, buf, 7)) {
			chg_info("temp_buf:%s\n", temp_buf);
		}
		if (!strncmp(temp_buf, "Y_DEBUG", 7)) {
			bcc_debug_mode = BCC_Y_DEBUG;
			chg_info("BCC_Y_DEBUG:%d\n", bcc_debug_mode);
		} else {
			bcc_debug_mode = BCC_N_DEBUG;
			chg_info("BCC_N_DEBUG:%d\n", bcc_debug_mode);
		}
		strncpy(bcc_debug_buf, buf + BCC_DEBUG_PARAM_SIZE, BCC_PAGE_SIZE);
		chg_info("bcc_debug_buf:%s, temp_buf:%s\n", bcc_debug_buf, temp_buf);
		return ret;
	}
#endif

	chg_info("buf:%s\n", buf);
	return ret;
}
#endif

static int ufcs_battery_chg_write(struct battery_chg_dev *bcdev, void *data,
	int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_err("glink state is down\n");
		return -ENOTCONN;
	}

	mutex_lock(&bcdev->ufcs_read_buffer_lock);
	reinit_completion(&bcdev->ufcs_read_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ufcs_read_ack,
			msecs_to_jiffies(AP_UFCS_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->ufcs_read_buffer_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	mutex_unlock(&bcdev->ufcs_read_buffer_lock);

	return rc;
}

static int ufcs_read_buffer(struct battery_chg_dev *bcdev)
{
	struct oplus_ap_read_ufcs_req_msg req_msg = { { 0 } };

	if (!bcdev) {
		return false;
	}

	req_msg.data_size = sizeof(bcdev->ufcs_read_buffer_dump.data_buffer);
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = AP_OPCODE_UFCS_BUFFER;

	return ufcs_battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static void handle_ufcs_read_buffer(struct battery_chg_dev *bcdev,
	struct oplus_ap_read_ufcs_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;

	if (len > sizeof(bcdev->ufcs_read_buffer_dump)) {
		chg_err("Incorrect length received: %zu expected: %zd\n", len,
			sizeof(bcdev->ufcs_read_buffer_dump));
		complete(&bcdev->ufcs_read_ack);
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->ufcs_read_buffer_dump.data_buffer)) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		complete(&bcdev->ufcs_read_ack);
		return;
	}

	if (buf_len == 0) {
		chg_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->ufcs_read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);
	complete(&bcdev->ufcs_read_ack);
}

static int battery_chg_fw_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return -ENOTCONN;
	}

	reinit_completion(&bcdev->fw_buf_ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->fw_buf_ack,
					msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			return -ETIMEDOUT;
		}

		rc = 0;
	}

	return rc;
}

static int battery_chg_write(struct battery_chg_dev *bcdev, void *data,
				int len)
{
	int rc;

	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&bcdev->state) == PMIC_GLINK_STATE_DOWN) {
		pr_debug("glink state is down\n");
		return 0;
	}

	if (bcdev->debug_battery_detected && bcdev->block_tx)
		return 0;

	mutex_lock(&bcdev->rw_lock);
	reinit_completion(&bcdev->ack);
	rc = pmic_glink_write(bcdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&bcdev->ack,
					msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			chg_err("Error, timed out sending message\n");
			mutex_unlock(&bcdev->rw_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&bcdev->rw_lock);

	return rc;
}

static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = val;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_set;

	pr_debug("psy: %s prop_id: %u val: %u\n", pst->psy->desc->name,
		req_msg.property_id, val);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct battery_charger_req_msg req_msg = { { 0 } };

	req_msg.property_id = prop_id;
	req_msg.battery_id = 0;
	req_msg.value = 0;
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = pst->opcode_get;

	pr_debug("psy: %s prop_id: %u\n", pst->psy->desc->name,
		req_msg.property_id);

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

static int get_property_id(struct psy_state *pst,
			enum power_supply_property prop)
{
	u32 i;

	for (i = 0; i < pst->prop_count; i++)
		if (pst->map[i] == prop)
			return i;

	chg_err("No property id for property %d in psy %s\n", prop,
		pst->psy->desc->name);

	return -ENOENT;
}

static void battery_chg_notify_enable(struct battery_chg_dev *bcdev)
{
	struct battery_charger_set_notify_msg req_msg = { { 0 } };
	int rc;

	/* Send request to enable notification */
	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_NOTIFY;
	req_msg.hdr.opcode = BC_SET_NOTIFY_REQ;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0)
		chg_err("Failed to enable notification rc=%d\n", rc);
}

static void battery_chg_subsys_up_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, subsys_up_work);

	battery_chg_notify_enable(bcdev);
}

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_typec_disable(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return;
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];

	/* set disable typec mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, QCOM_TYPEC_PORT_ROLE_DRP);
	if (rc < 0) {
		chg_info("Couldn't write 0x2b44[3] rc=%d\n", rc);
	}
}

static bool is_common_topic_available(struct battery_chg_dev *bcdev)
{
	if (!bcdev->common_topic)
		bcdev->common_topic = oplus_mms_get_by_name("common");

	return !!bcdev->common_topic;
}

void oplus_chg_set_curr_level_to_voocphy(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	union mms_msg_data data = {0};
	int cool_down = 0;

	if (is_common_topic_available(bcdev)) {
		oplus_mms_get_item_data(bcdev->common_topic,
					COMM_ITEM_COOL_DOWN, &data, false);
	} else {
		chg_err("common topic not found\n");
	}
	cool_down = data.intval;

	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, cool_down);
	if (rc) {
		chg_err("set curr level fail, rc=%d\n", rc);
		return;
	}

	chg_info("ap set curr level[%d] to voocphy\n", cool_down);
}

static void oplus_adsp_voocphy_cancle_err_check(struct battery_chg_dev *bcdev)
{
	if (bcdev->voocphy_err_check == true) {
		cancel_delayed_work_sync(&bcdev->voocphy_err_work);
	}
	bcdev->voocphy_err_check = false;
}

static bool is_vooc_topic_available(struct battery_chg_dev *bcdev)
{
	if (!bcdev->vooc_topic)
		bcdev->vooc_topic = oplus_mms_get_by_name("vooc");

	return !!bcdev->vooc_topic;
}

static bool is_cpa_topic_available(struct battery_chg_dev *bcdev)
{
	if (!bcdev->cpa_topic)
		bcdev->cpa_topic = oplus_mms_get_by_name("cpa");

	return !!bcdev->cpa_topic;
}

static int oplus_chg_get_voocphy_support(struct battery_chg_dev *bcdev)
{
	int voocphy_support = 0;

	if (is_vooc_topic_available(bcdev))
		voocphy_support = oplus_vooc_get_voocphy_support(bcdev->vooc_topic);
	else
		chg_err("vooc topic not found\n");

	return voocphy_support;
}

static bool oplus_vooc_get_fastchg_ing(struct battery_chg_dev *bcdev)
{
	bool fastchg_status;
	union mms_msg_data data = { 0 };

	if (!is_vooc_topic_available(bcdev)) {
		chg_info("vooc_topic is null\n");
		return 0;
	}

	oplus_mms_get_item_data(bcdev->vooc_topic, VOOC_ITEM_VOOC_CHARGING,
				&data, true);
	fastchg_status = !!data.intval;
	chg_debug("get fastchg status = %d\n", fastchg_status);

	return fastchg_status;
}

static int oplus_vooc_get_fast_chg_type(struct battery_chg_dev *bcdev)
{
	int svooc_type = 0;
	union mms_msg_data data = { 0 };

	if (!is_vooc_topic_available(bcdev)) {
		chg_info("vooc_topic is null\n");
		return 0;
	}

	oplus_mms_get_item_data(bcdev->vooc_topic,
				VOOC_ITEM_GET_BCC_SVOOC_TYPE, &data, true);
	svooc_type = data.intval;
	chg_debug("get svooc type = %d\n", svooc_type);

	return svooc_type;
}

static int oplus_cpa_get_protocol_allow(struct battery_chg_dev *bcdev)
{
	union mms_msg_data data = { 0 };

	if (!is_cpa_topic_available(bcdev)) {
		chg_info("cpa_topic is null\n");
		return CHG_PROTOCOL_INVALID;
	}

	oplus_mms_get_item_data(bcdev->cpa_topic, CPA_ITEM_ALLOW, &data, true);
	chg_debug("get protocol allow = %d\n", data.intval);

	return data.intval;
}

static int oplus_chg_8350_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type);
static void oplus_qc_type_check_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, qc_type_check_work.work);
	int type;
	int rc;

	rc = oplus_chg_8350_get_charger_type(bcdev->buck_ic, &type);
	if (rc < 0)
		type = OPLUS_CHG_USB_TYPE_UNKNOWN;
	if (type == OPLUS_CHG_USB_TYPE_QC2 || type == OPLUS_CHG_USB_TYPE_QC3) {
		oplus_chg_ic_virq_trigger(bcdev->buck_ic,
					  OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
		return;
	}

	schedule_delayed_work(&bcdev->qc_type_check_work,
			      msecs_to_jiffies(QC_TYPE_CHECK_INTERVAL));
}

#define  VOLTAGE_2000MV  2000
#define  COUNT_SIX      6
#define  COUNT_THR      3
#define  COUNT_TEN      10
#define  CHECK_CURRENT_LOW       300
#define  CHECK_CURRENT_HIGH      900
#define  VBUS_VOLT_LOW      6000
static void oplus_recheck_input_current_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, recheck_input_current_work.work);
	bool fastchg_ing = oplus_vooc_get_fastchg_ing(bcdev);
	int fast_chg_type = oplus_vooc_get_fast_chg_type(bcdev);
	int chg_vol = 0;
	int ibus_curr = 0;
	static int count = 0;
	static int err_cnt = 0;

	chg_err("reset input current count:%d\n", count);
	chg_vol = oplus_wired_get_vbus();
	if (!fastchg_ing)
		ibus_curr = oplus_wired_get_ibus();

	if (chg_vol > VOLTAGE_2000MV) {
		count++;

		if ((count > COUNT_THR) && (ibus_curr > CHECK_CURRENT_LOW) &&
		    (ibus_curr < CHECK_CURRENT_HIGH))
			err_cnt++;
		else
			err_cnt = 0;

		if (count > COUNT_TEN) {
			chg_err("reset input current err_cnt: %d,chg_vol:%d,"
				"fastchg_ing:%d,ibus_curr:%d,fast_chg_type:%d\n",
				err_cnt, chg_vol, fastchg_ing, ibus_curr,
				fast_chg_type);
			if (bcdev->charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
				chg_err("reset input current charger_type: %d\n",
					bcdev->charger_type);
				count = 0;
				return;
			}
			if (err_cnt > COUNT_THR) {
				chg_err("reset icl setting!\n");
				oplus_chg_ic_virq_trigger(bcdev->buck_ic,
							  OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
			}
			if (fastchg_ing && (fast_chg_type != BCC_TYPE_IS_VOOC)) {
				chg_err("reset voocphy setting, chg_vol:%d\n", chg_vol);
				if (chg_vol < VBUS_VOLT_LOW)
					oplus_adsp_voocphy_reset_status();
			}
			count = 0;
		} else {
			schedule_delayed_work(&bcdev->recheck_input_current_work, msecs_to_jiffies(2000));
		}
	} else {
		count = 0;
	}
}

static void oplus_unsuspend_usb_work(struct work_struct *work)
{
	oplus_chg_suspend_charger(false, DEF_VOTER);
}

static void oplus_adsp_voocphy_status_func(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, adsp_voocphy_status_work.work);
	struct psy_state *pst = NULL;
	struct psy_state *pst_batt = NULL;
	int rc;
	int intval = 0;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	pst_batt = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = read_property_id(bcdev, pst, USB_VOOCPHY_STATUS);
	if (rc < 0) {
		chg_err("read adsp voocphy status fail\n");
		return;
	}
	intval = pst->prop[USB_VOOCPHY_STATUS];

	if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_ERR_COMMU
		|| (intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_COMMU_TIME_OUT
		|| (intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_COMMU_CLK_ERR) {
		/* unplug svooc but usb_in_status (in oplus_plugin_irq_work) was 1 sometimes */
		schedule_delayed_work(&bcdev->voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
		schedule_delayed_work(&bcdev->plugin_irq_work, 0);
		schedule_delayed_work(&bcdev->recheck_input_current_work, msecs_to_jiffies(3000));
	}
	if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_BATT_TEMP_OVER) {
		/* fast charge warm switch to normal charge,input current limmit to 500mA,rerun ICL setting */
		schedule_delayed_work(&bcdev->recheck_input_current_work, msecs_to_jiffies(3000));
	}

	oplus_adsp_voocphy_fastchg_event_handle(intval);
	if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_PRESENT
		|| (intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_ONGOING) {
		oplus_chg_set_curr_level_to_voocphy(bcdev);
	}

	if ((intval & 0xFF) != ADSP_VPHY_FAST_NOTIFY_PRESENT)
		oplus_adsp_voocphy_cancle_err_check(bcdev);
}

#define DISCONNECT			0
#define STANDARD_TYPEC_DEV_CONNECT	BIT(0)
#define OTG_DEV_CONNECT			BIT(1)
int oplus_get_otg_online_status_with_cid_scheme(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	int cid_status = 0;
	int online = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		chg_err("!!!read cid_status fail\n");
		return 0;
	}
	cid_status = pst->prop[USB_CID_STATUS];
	bcdev->cid_status = cid_status;
	online = (cid_status == 1) ? STANDARD_TYPEC_DEV_CONNECT : DISCONNECT;
	chg_info("cid_status = %d, online = %d\n", cid_status, online);

	return online;
}

static void oplus_ccdetect_enable(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		chg_err("Couldn't read USB_TYPEC_MODE, rc=%d\n", rc);
		return;
	}

	/* set DRP mode */
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, QCOM_TYPEC_PORT_ROLE_DRP);
	if (rc < 0) {
		chg_err("Couldn't clear 0x2b44[0] rc=%d\n", rc);
	}

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		chg_err("Couldn't read USB_TYPEC_MODE, rc=%d\n", rc);
		return;
	} else {
		chg_err("reg0x2b44[0x%x], bit[2:0]=0(DRP)\n", pst->prop[USB_TYPEC_MODE]);
	}
}

static int oplus_otg_ap_enable(struct battery_chg_dev *bcdev, bool enable)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_AP_ENABLE, enable);
	if (rc) {
		chg_err("oplus_otg_ap_enable fail, rc=%d\n", rc);
	} else {
		chg_err("oplus_otg_ap_enable, rc=%d\n", rc);
	}
	oplus_get_otg_online_status_with_cid_scheme(bcdev);
	if (bcdev->cid_status != 0) {
		chg_err("Oplus_otg_ap_enable,flag bcdev->cid_status != 0\n");
		oplus_ccdetect_enable(bcdev);
	}

	return rc;
}

static void oplus_otg_init_status_func(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, otg_init_work.work);

	oplus_otg_ap_enable(bcdev, true);
}

static void oplus_cid_status_change_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, cid_status_change_work.work);
	struct psy_state *pst = NULL;
	int cid_status = 0;
	int rc = 0;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		chg_err("!!!%s, read cid_status fail\n", __func__);
		return;
	}

	cid_status = pst->prop[USB_CID_STATUS];
	bcdev->cid_status = cid_status;
	chg_info("cid_status[%d]\n", cid_status);
}

static int oplus_oem_misc_ctl(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OEM_MISC_CTL, bcdev->oem_misc_ctl_data);
	if (rc)
		chg_err("oplus_oem_misc_ctl fail, rc=%d\n", rc);
	else
		chg_err("oem_misc_ctl_data: 0x%x\n", bcdev->oem_misc_ctl_data);

	return rc;
}

static void oplus_oem_lcm_en_check_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = g_bcdev;
	int enable, vph_track_high;
	static int last_enable = -1, last_vph_track_high = -1;

	if (!bcdev) {
		chg_err("bcdev is NULL\n");
		return;
	}

	enable = (bcdev->oem_usb_online ? 0 : 1);
	/* vph_track_high = (chip->batt_full ? 1 : 0); */
	vph_track_high = 0; /* TODO */

	if (bcdev->oem_usb_online && (enable == last_enable) && (last_vph_track_high == vph_track_high)) {
		schedule_delayed_work(&bcdev->oem_lcm_en_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
		return;
	}

	bcdev->oem_misc_ctl_data = 0;
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_LCM_EN, enable);
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_NCM_AUTO_MODE, enable);
	bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_VPH_TRACK_HIGH, vph_track_high);
	oplus_oem_misc_ctl();
	last_enable = enable;
	last_vph_track_high = vph_track_high;

	if (bcdev->oem_usb_online) {
		schedule_delayed_work(&bcdev->oem_lcm_en_check_work, round_jiffies_relative(msecs_to_jiffies(5000)));
	}
}

static int oplus_otg_boost_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.otg_boost_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl)) {
		chg_err("get otg_boost_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_boost_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl, "otg_booster_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_active)) {
		chg_err("get otg_boost_en_active\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_boost_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl, "otg_booster_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_sleep)) {
		chg_err("get otg_booster_en_sleep\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
		bcdev->oplus_custom_gpio.otg_boost_en_sleep);

	return 0;
}

static int oplus_otg_ovp_en_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl)) {
		chg_err("get otg_ovp_en_pinctrl fail\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_ovp_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl, "otg_ovp_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_active)) {
		chg_err("get otg_ovp_en_active\n");
		return -EINVAL;
	}
	bcdev->oplus_custom_gpio.otg_ovp_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl, "otg_ovp_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_sleep)) {
		chg_err("get otg_ovp_en_sleep\n");
		return -EINVAL;
	}

	pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
		bcdev->oplus_custom_gpio.otg_ovp_en_sleep);

	return 0;
}

static void oplus_set_otg_boost_en_val(struct battery_chg_dev *bcdev, int value)
{
	if (bcdev->oplus_custom_gpio.otg_boost_en_gpio <= 0) {
		chg_err("otg_boost_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_sleep)) {
		chg_err("otg_boost_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_sleep);
	}

	chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.otg_boost_en_gpio));
}

static void oplus_set_otg_ovp_en_val(struct battery_chg_dev *bcdev, int value)
{
	if (bcdev->oplus_custom_gpio.otg_ovp_en_gpio <= 0) {
		chg_err("otg_ovp_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_sleep)) {
		chg_err("otg_ovp_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_active);
	} else {
		gpio_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_sleep);
	}

	chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
		gpio_get_value(bcdev->oplus_custom_gpio.otg_ovp_en_gpio));
}

int oplus_adsp_batt_curve_current(void)
{
	int rc = 0;
	static int batt_current = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -ENODEV;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_GET_BATT_CURR);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return batt_current;
	}
	batt_current = DIV_ROUND_CLOSEST((int)pst->prop[USB_GET_BATT_CURR], 1000);
	chg_err("get batt_curr = %d \n", batt_current);
	return batt_current*100;
}

int oplus_adsp_voocphy_get_fast_chg_type(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	int fast_chg_type = 0;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -ENODEV;
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_VOOC_FAST_CHG_TYPE);
	if (rc < 0) {
		chg_err("read vooc_fast_chg_type fail, rc=%d\n", rc);
		return 0;
	}
	fast_chg_type = (pst->prop[USB_VOOC_FAST_CHG_TYPE]) & 0x7F;

	return fast_chg_type;
}

int oplus_adsp_voocphy_enable(bool enable)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -ENODEV;
	}

	if (oplus_chg_get_voocphy_support(bcdev) != ADSP_VOOCPHY)
		return rc;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, enable);
	if (rc) {
		chg_err("set enable adsp voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("set enable adsp voocphy success, rc=%d\n", rc);
	}

	return rc;
}

void oplus_turn_off_power_when_adsp_crash(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is null\n");
		return;
	}

	bcdev->is_chargepd_ready = false;
	bcdev->pd_svooc = false;
	bcdev->otg_online = false;
	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_SVID);

	if (bcdev->otg_online == true) {
		bcdev->otg_online = false;
		if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_EXTERNAL) {
#ifdef OPLUS_CHG_UNDEF /* TODO */
			oplus_wpc_set_booster_en_val(0);
#endif
			oplus_set_otg_ovp_en_val(bcdev, 0);
		} else if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_PMIC) {
			oplus_chg_ic_virq_trigger(bcdev->buck_ic,
						  OPLUS_IC_VIRQ_OTG_ENABLE);
		}
	}
}
EXPORT_SYMBOL(oplus_turn_off_power_when_adsp_crash);

bool oplus_is_pd_svooc(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is null\n");
		return false;
	}

	chg_info("pd_svooc = %d\n", bcdev->pd_svooc);

	return bcdev->pd_svooc;
}
EXPORT_SYMBOL(oplus_is_pd_svooc);

void oplus_adsp_crash_recover_work(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is null\n");
		return;
	}

	schedule_delayed_work(&bcdev->adsp_crash_recover_work,
			      round_jiffies_relative(msecs_to_jiffies(1500)));
}
EXPORT_SYMBOL(oplus_adsp_crash_recover_work);

static int oplus_ap_init_adsp_gague(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_ADSP_GAUGE_INIT, 1);
	if (rc)
		chg_err("init adsp gague fail, rc=%d\n", rc);
	else
		chg_err("init adsp gague sucess.");

	return rc;
}

static void oplus_adsp_crash_recover_func(struct work_struct *work)
{
	struct battery_chg_dev *bcdev =
		container_of(work, struct battery_chg_dev, adsp_crash_recover_work.work);

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		oplus_ap_init_adsp_gague(bcdev);
		oplus_adsp_voocphy_reset_status();
	}

	oplus_voocphy_enable(bcdev, true);
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	schedule_delayed_work(&bcdev->voocphy_enable_check_work,
			      round_jiffies_relative(msecs_to_jiffies(0)));
}

static bool is_chg_disable_votable_available(struct battery_chg_dev *bcdev)
{
	if (!bcdev->chg_disable_votable)
		bcdev->chg_disable_votable = find_votable("CHG_DISABLE");

	return !!bcdev->chg_disable_votable;
}

static void oplus_voocphy_enable_check_func(struct work_struct *work)
{
	int rc;
	int voocphy_enable = 0;
	int mmi_chg = 1;
	int charger_type;
	int prop_id = 0;
	struct psy_state *pst;
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, voocphy_enable_check_work.work);

	if (oplus_chg_get_voocphy_support(bcdev) != ADSP_VOOCPHY)
		return;

	if (is_chg_disable_votable_available(bcdev))
		mmi_chg = !get_client_vote(bcdev->chg_disable_votable, MMI_CHG_VOTER);
	if (mmi_chg == 0)
		goto done;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		return;
	}
	charger_type = pst->prop[prop_id];
	chg_info("%s: mmi_chg = %d, charger_type = %d\n", __func__, mmi_chg, charger_type);

	if (charger_type != POWER_SUPPLY_TYPE_USB_DCP) {
		chg_err("charger_type != POWER_SUPPLY_TYPE_USB_DCP\n");
		goto done;
	}

	voocphy_enable = oplus_get_voocphy_enable(bcdev);
	if (voocphy_enable == 0) {
		chg_err("need enable voocphy again\n");
		rc = oplus_voocphy_enable(bcdev, true);
		schedule_delayed_work(&bcdev->voocphy_enable_check_work,
				      round_jiffies_relative(msecs_to_jiffies(500)));
		return;
	}
done:
	schedule_delayed_work(&bcdev->voocphy_enable_check_work,
			      round_jiffies_relative(msecs_to_jiffies(5000)));
}

static void otg_notification_handler(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, otg_vbus_enable_work.work);

	if (!bcdev) {
		chg_info("bcdev is null, return\n");
		return;
	}

	if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_EXTERNAL) {
		if (bcdev->otg_online) {
			oplus_set_otg_ovp_en_val(bcdev, 1);
			oplus_set_otg_boost_en_val(bcdev, 1);
		} else {
			oplus_set_otg_ovp_en_val(bcdev, 0);
			oplus_set_otg_boost_en_val(bcdev, 0);
		}
	}
	oplus_wired_otg_boost_enable(!!bcdev->otg_online);
}

static bool oplus_chg_is_usb_present(struct battery_chg_dev *bcdev)
{
	bool vbus_rising = bcdev->usb_in_status;

	if (oplus_vchg_trig_is_support() == true
			&& oplus_get_vchg_trig_status() == 1 && vbus_rising == true) {
		vbus_rising = false;
	}

#ifdef OPLUS_CHG_UNDEF /* TODO */
	if (vbus_rising == false && (oplus_wpc_get_wireless_charge_start() || oplus_chg_is_wls_present())) {
		chg_err("USBIN_PLUGIN_RT_STS_BIT low but wpc has started\n");
		vbus_rising = true;
	}
#endif
	return vbus_rising;
}

static void oplus_hvdcp_disable_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, hvdcp_disable_work.work);

	if (oplus_chg_is_usb_present(bcdev) == false) {
		chg_info("set bcdev->hvdcp_disable false\n");
		bcdev->hvdcp_disable = false;
	}
}

static void oplus_pd_only_check_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, pd_only_check_work.work);

	if (bcdev->pd_svooc == false) {
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_SVID);
		chg_info("!!!pd_svooc[%d]\n", bcdev->pd_svooc);
	}
}

static int oplus_chg_get_match_temp(struct battery_chg_dev *bcdev)
{
	int temp;
	union mms_msg_data data = {0};

	if (is_common_topic_available(bcdev)) {
		oplus_mms_get_item_data(bcdev->common_topic,
					COMM_ITEM_SHELL_TEMP, &data, false);
		temp = data.intval;
	} else {
		chg_err("common topic not found\n");
		temp = 320;
	}

	return temp;
}

#define OTG_SKIN_TEMP_HIGH 450
#define OTG_SKIN_TEMP_MAX 540
static int oplus_get_bat_info_for_otg_status_check(struct battery_chg_dev *bcdev,
						   int *soc, int *ichaging)
{
	struct psy_state *pst = NULL;
	int rc = 0;
	int prop_id = 0;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return -1;
	}
	*ichaging = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return -1;
	}
	*soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return 0;
}

#define OTG_PROHIBITED_CURR_HIGH_THR	3000
#define OTG_PROHIBITED_CURR_LOW_THR	1700
static void oplus_otg_status_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork,
			struct battery_chg_dev, otg_status_check_work);
	int rc;
	int skin_temp = 0, batt_current = 0, real_soc = 0;
	bool contion1 = false, contion2 = false, contion3 = false, contion4 = false, contion5 = false;
	static int otg_protect_cnt = 0;

	if (bcdev == NULL) {
		pr_err("battery_chg_dev is NULL\n");
		return;
	}

	skin_temp = oplus_chg_get_match_temp(bcdev);
	rc = oplus_get_bat_info_for_otg_status_check(bcdev, &real_soc, &batt_current);
	if (rc < 0) {
		pr_err("Error oplus_get_bat_info_for_otg_status_check, rc = %d\n", rc);
		return;
	}

	real_soc = fg_sm8350_get_battery_soc();
	chg_info("batt_current = %d, skin_temp = %d, real_soc = %d, otg_protect_cnt(%d)\n",
		batt_current, skin_temp, real_soc, otg_protect_cnt);
	contion1 = ((batt_current > bcdev->otg_curr_limit_high) && (skin_temp > OTG_SKIN_TEMP_HIGH));
	contion2 = (batt_current > bcdev->otg_curr_limit_max);
	contion3 = (skin_temp > OTG_SKIN_TEMP_MAX);
	contion4 = ((real_soc < bcdev->otg_real_soc_min) && (batt_current > bcdev->otg_curr_limit_high));
	contion5 = ((skin_temp < 0) && (batt_current > bcdev->otg_curr_limit_high));

	if ((contion1 || contion2 || contion3 || contion4 || contion5) && (get_eng_version() != HIGH_TEMP_AGING)) {
		otg_protect_cnt++;
		if (otg_protect_cnt >= 2) {
			if (!bcdev->otg_prohibited) {
				bcdev->otg_prohibited = true;
				schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
				chg_err("OTG prohibited, batt_current = %d, skin_temp = %d, real_soc = %d\n",
					batt_current, skin_temp, real_soc);
			}
		}
	} else {
		otg_protect_cnt = 0;
	}

	if (!bcdev->otg_online) {
		if (bcdev->otg_prohibited) {
			bcdev->otg_prohibited = false;
		}
		chg_err("otg_online is false, exit\n");
		return;
	}

	schedule_delayed_work(&bcdev->otg_status_check_work, msecs_to_jiffies(1000));
}

static void oplus_vbus_enable_adc_work(struct work_struct *work)
{
	oplus_chg_disable_charger(true, FASTCHG_VOTER);
	oplus_chg_suspend_charger(true, FASTCHG_VOTER);
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

bool oplus_get_wired_otg_online(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return false;
	}

	if (bcdev->wls_fw_update == true)
		return false;
	return bcdev->otg_online;
}

bool oplus_get_wired_chg_present(void)
{
	if (oplus_get_wired_otg_online() == true)
		return false;
	if (oplus_vchg_trig_is_support() == true && oplus_get_vchg_trig_status() == 0)
		return true;
	return false;
}

static void battery_chg_state_cb(void *priv, enum pmic_glink_state state)
{
	struct battery_chg_dev *bcdev = priv;

	pr_debug("state: %d\n", state);

	atomic_set(&bcdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&bcdev->subsys_up_work);
}

/**
 * qti_battery_charger_get_prop() - Gets the property being requested
 *
 * @name: Power supply name
 * @prop_id: Property id to be read
 * @val: Pointer to value that needs to be updated
 *
 * Return: 0 if success, negative on error.
 */
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	struct power_supply *psy;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst;
	int rc = 0;

	if (prop_id >= BATTERY_CHARGER_PROP_MAX)
		return -EINVAL;

	if (strcmp(name, "battery") && strcmp(name, "usb") &&
	    strcmp(name, "wireless"))
		return -EINVAL;

	psy = power_supply_get_by_name(name);
	if (!psy)
		return -ENODEV;

#ifndef OPLUS_FEATURE_CHG_BASIC
	bcdev = power_supply_get_drvdata(psy);
#else
	bcdev = g_bcdev;
#endif
	if (!bcdev)
		return -ENODEV;

	power_supply_put(psy);

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
		if (!rc)
			*val = pst->prop[BATT_RESISTANCE];
		break;
	default:
		break;
	}

	return rc;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static bool validate_message(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		chg_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		chg_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

static bool current_message_check(struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code == BATTMNGR_EFAILED &&
	    resp_msg->property_id == BATT_CURR_NOW) {
		return true;
	}

	return false;
}

#define MODEL_DEBUG_BOARD	"Debug_Board"
static void handle_message(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_resp_msg *resp_msg = data;
	struct battery_model_resp_msg *model_resp_msg = data;
	struct wireless_fw_check_resp *fw_check_msg;
	struct wireless_fw_push_buf_resp *fw_resp_msg;
	struct wireless_fw_update_status *fw_update_msg;
	struct wireless_fw_get_version_resp *fw_ver_msg;
	struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			bcdev->debug_battery_detected = !strcmp(pst->model,
					MODEL_DEBUG_BOARD);
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		if (current_message_check(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
	case BC_SHUTDOWN_NOTIFY:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		chg_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set)
		complete(&bcdev->ack);
}

static struct power_supply_desc usb_psy_desc;

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
#ifdef OPLUS_FEATURE_CHG_BASIC
	static int last_usb_adap_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
#endif
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0) {
		chg_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		return;
	}

	chg_info("usb_adap_type: %u\n", pst->prop[USB_ADAP_TYPE]);

	switch (pst->prop[USB_ADAP_TYPE]) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
#ifndef OPLUS_FEATURE_CHG_BASIC
		rc = read_property_id(bcdev, pst, USB_ONLINE);
		if (rc < 0) {
			chg_err("Failed to read USB_ONLINE rc=%d\n", rc);
			return;
		}
		if (pst->prop[USB_ONLINE] == 0)
			usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
#else
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
#endif
		break;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	if (!((last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD_PPS ||
	    last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD ||
	    last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD_DRP) &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD_PPS &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD_DRP) &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_UNKNOWN &&
	    last_usb_adap_type != pst->prop[USB_ADAP_TYPE]) {
		chg_debug("trigger virq OPLUS_IC_VIRQ_CHG_TYPE_CHANGE");
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
	}
	last_usb_adap_type = pst->prop[USB_ADAP_TYPE];
#endif
}

static void handle_notification(struct battery_chg_dev *bcdev, void *data,
				size_t len)
{
	struct battery_charger_notify_msg *notify_msg = data;
	struct psy_state *pst = NULL;
	int protocol = CHG_PROTOCOL_INVALID;

	if (len != sizeof(*notify_msg)) {
		chg_err("Incorrect response length %zu\n", len);
		return;
	}

	chg_info("%s: notification: 0x%x\n", __func__, notify_msg->notification);

	switch (notify_msg->notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
#ifdef OPLUS_FEATURE_CHG_BASIC
	case BC_PD_SVOOC:
#ifdef OPLUS_CHG_UNDEF /* TODO */
		if ((get_oplus_chg_chip() && get_oplus_chg_chip()->wireless_support == false)
			|| oplus_get_wired_chg_present() == true) {
			chg_info("should set pd_svooc\n");
			oplus_usb_set_none_role();
			bcdev->pd_svooc = true;
		}
#endif
		bcdev->pd_svooc = true;
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_SVID);
		chg_info("pd_svooc = %d\n", bcdev->pd_svooc);
		break;
	case BC_VOOC_STATUS_GET:
		schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
		break;
	case BC_OTG_ENABLE:
		chg_info("enable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_OTG_ENABLE);
		if (pst && is_usb_psy_available(bcdev))
			power_supply_changed(pst->psy);
		break;
	case BC_OTG_DISABLE:
		chg_info("disable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_OTG_ENABLE);
		if (pst && is_usb_psy_available(bcdev)) {
			power_supply_changed(pst->psy);
		}
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		bcdev->voocphy_err_check = true;
		cancel_delayed_work_sync(&bcdev->voocphy_err_work);
		schedule_delayed_work(&bcdev->voocphy_err_work, msecs_to_jiffies(8500));
		if (bcdev->is_external_chg) {
			/* excute in glink loop for real time */
			oplus_chg_disable_charger(true, FASTCHG_VOTER);
			oplus_chg_suspend_charger(true, FASTCHG_VOTER);
		} else {
			/* excute in work to avoid glink dead loop */
			schedule_delayed_work(&bcdev->vbus_adc_enable_work, 0);
		}
		break;
	case BC_CID_DETECT:
		chg_info("cid detect\n");
		schedule_delayed_work(&bcdev->cid_status_change_work, 0);
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_DETECT);
		break;
	case BC_QC_DETECT:
		bcdev->hvdcp_detect_ok = true;
		break;
	case BC_TYPEC_STATE_CHANGE:
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_CHANGED);
		break;
	case BC_PLUGIN_IRQ:
		chg_info("BC_PLUGIN_IRQ\n");
		schedule_delayed_work(&bcdev->plugin_irq_work, 0);
		break;
	case BC_APSD_DONE:
		bcdev->bc12_completed = true;
		chg_info("BC_APSD_DONE\n");
		break;
	case BC_CHG_STATUS_SET:
		chg_info("BC_CHG_STATUS_SET");
		schedule_delayed_work(&bcdev->unsuspend_usb_work, 0);
		break;
	case BC_UFCS_TEST_MODE_TRUE:
		bcdev->ufcs_test_mode = true;
		chg_info("ufcs test mode change = %d\n", bcdev->ufcs_test_mode);
		break;
	case BC_UFCS_TEST_MODE_FALSE:
		bcdev->ufcs_test_mode = false;
		chg_info("ufcs test mode change = %d\n", bcdev->ufcs_test_mode);
		break;
	case BC_UFCS_POWER_READY:
		bcdev->ufcs_power_ready = true;
		chg_info("ufcs power ready = %d\n", bcdev->ufcs_power_ready);
		break;
	case BC_UFCS_HANDSHAKE_OK:
		bcdev->ufcs_handshake_ok = true;
		chg_info("ufcs handshake ok = %d\n", bcdev->ufcs_handshake_ok);
		break;
	case BC_UFCS_DISABLE_MOS:
		protocol = oplus_cpa_get_protocol_allow(bcdev);
		if (protocol != CHG_PROTOCOL_VOOC) {
			chg_info("ufcs exit and disabe mos");
			plat_ufcs_send_state(PLAT_UFCS_NOTIFY_EXIT, NULL);
		} else {
			chg_info("the current protol is %d.", protocol);
		}
		break;
	case BC_UFCS_PDO_READY:
		bcdev->ufcs_pdo_ready = true;
		chg_info("ufcs pdo ready = %d\n", bcdev->ufcs_pdo_ready);
		break;
#endif
	default:
		break;
	}

	if (pst && pst->psy) {
		/*
		 * For charger mode, keep the device awake at least for 50 ms
		 * so that device won't enter suspend when a non-SDP charger
		 * is removed. This would allow the userspace process like
		 * "charger" to be able to read power supply uevents to take
		 * appropriate actions (e.g. shutting down when the charger is
		 * unplugged).
		 */
		pm_wakeup_dev_event(bcdev->dev, 50, true);
	}
}

static int battery_chg_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct battery_chg_dev *bcdev = priv;

	if (!bcdev->is_chargepd_ready)
		bcdev->is_chargepd_ready = true;

	if (hdr->opcode == BC_NOTIFY_IND)
		handle_notification(bcdev, data, len);
#ifdef OPLUS_FEATURE_CHG_BASIC
	else if (hdr->opcode == OEM_OPCODE_READ_BUFFER)
		handle_oem_read_buffer(bcdev, data, len);
	else if (hdr->opcode == BCC_OPCODE_READ_BUFFER)
		handle_bcc_read_buffer(bcdev, data, len);
	else if (hdr->opcode == AP_OPCODE_UFCS_BUFFER)
		handle_ufcs_read_buffer(bcdev, data, len);
#endif
	else
		handle_message(bcdev, data, len);

	return 0;
}

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

	return 0;
}

static int wls_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	return 0;
}

static int wls_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	return 0;
}

static enum power_supply_property wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
#ifdef OPLUS_FEATURE_CHG_BASIC
	POWER_SUPPLY_PROP_PRESENT,
#endif
};

static const struct power_supply_desc wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= wls_props,
	.num_properties		= ARRAY_SIZE(wls_props),
	.get_property		= wls_psy_get_prop,
	.set_property		= wls_psy_set_prop,
	.property_is_writeable	= wls_psy_prop_is_writeable,
};

static const char *get_usb_type_name(u32 usb_type)
{
	u32 i;

	if (usb_type >= QTI_POWER_SUPPLY_USB_TYPE_HVDCP &&
	    usb_type <= QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5) {
		for (i = 0; i < ARRAY_SIZE(qc_power_supply_usb_type_text);
		     i++) {
			if (i == (usb_type - QTI_POWER_SUPPLY_USB_TYPE_HVDCP))
				return qc_power_supply_usb_type_text[i];
		}
		return "Unknown";
	}

	for (i = 0; i < ARRAY_SIZE(power_supply_usb_type_text); i++) {
		if (i == usb_type)
			return power_supply_usb_type_text[i];
	}

	return "Unknown";
}

#ifndef OPLUS_FEATURE_CHG_BASIC
static int usb_psy_set_icl(struct battery_chg_dev *bcdev, u32 prop_id, int val)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	u32 temp;
	int rc;

	rc = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (rc < 0)
		return rc;

	/* Allow this only for SDP or USB_PD and not for other charger types */
	if (pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_SDP &&
	    pst->prop[USB_ADAP_TYPE] != POWER_SUPPLY_USB_TYPE_PD)
		return -EINVAL;

	/*
	 * Input current limit (ICL) can be set by different clients. E.g. USB
	 * driver can request for a current of 500/900 mA depending on the
	 * port type. Also, clients like EUD driver can pass 0 or -22 to
	 * suspend or unsuspend the input for its use case.
	 */

	temp = val;
	if (val < 0)
		temp = UINT_MAX;

	rc = write_property_id(bcdev, pst, prop_id, temp);
	if (!rc)
		pr_debug("Set ICL to %u\n", temp);

	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
static void oplus_chg_set_match_temp_to_voocphy(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;
	int match_temp = 0;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return;
	}
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	match_temp = oplus_chg_get_match_temp(bcdev);
	rc = write_property_id(bcdev, pst, BATT_SET_MATCH_TEMP, match_temp);
	if (rc) {
		chg_err("set match temp fail, rc=%d\n", rc);
		return;
	}

	chg_debug("ap set match temp[%d] to voocphy\n", match_temp);
}

int oplus_set_bcc_curr_to_voocphy(struct oplus_chg_ic_dev *ic_dev, int *bcc_curr)
{
	int rc = 0;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (bcc_curr == NULL) {
		chg_err("bcc_curr is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_BCC_CURRENT, *bcc_curr);
	if (rc) {
		chg_err("set bcc current fail, rc=%d\n", rc);
		return rc;
	}

	chg_debug("ap set bcc current[%d] to voocphy\n", *bcc_curr);
	return rc;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC

#ifdef OPLUS_CHG_UNDEF
static unsigned int get_chg_ctl_param_info(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = NULL;
	int rc = 0;
	int intval = 0;
	unsigned int project = 0, index = 0;

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_VOOC_CHG_PARAM_INFO);
	if (rc < 0) {
		chg_err("read USB_VOOC_CHG_PARAM_INFO fail\n");
		return 0;
	}
	intval = pst->prop[USB_VOOC_CHG_PARAM_INFO];
	index = (intval & 0xFF);
	project = ((intval >> 8) & 0xFFFFFF);
	return (project * 100 + index);
}
#endif
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];
	if (prop == POWER_SUPPLY_PROP_TEMP)
		pval->intval = DIV_ROUND_CLOSEST((int)pval->intval, 10);

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifdef OPLUS_CHG_UNDEF /* TODO */
int oplus_get_fast_chg_type(void)
{
	int fast_chg_type = 0;

	fast_chg_type = oplus_vooc_get_fast_chg_type();
	if (fast_chg_type == 0) {
		fast_chg_type = oplus_chg_get_charger_subtype();
	}
	if (fast_chg_type == 0) {
		if (oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_VOOC
			|| oplus_wpc_get_adapter_type() == CHARGER_SUBTYPE_FASTCHG_SVOOC)
			fast_chg_type = oplus_wpc_get_adapter_type();
	}

	return fast_chg_type;
}
#endif
#endif

static int usb_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc = 0;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
#ifndef OPLUS_FEATURE_CHG_BASIC
		rc = usb_psy_set_icl(bcdev, prop_id, pval->intval);
#endif
		break;
	default:
		break;
	}

	return rc;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_usb_type usb_psy_supported_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static struct power_supply_desc usb_psy_desc = {
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.set_property		= usb_psy_set_prop,
	.usb_types		= usb_psy_supported_types,
	.num_usb_types		= ARRAY_SIZE(usb_psy_supported_types),
	.property_is_writeable	= usb_psy_prop_is_writeable,
};

static int __battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					u32 fcc_ua)
{
	int rc;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (rc < 0)
		chg_err("Failed to set FCC %u, rc=%d\n", fcc_ua, rc);
	else
		pr_debug("Set FCC to %u uA\n", fcc_ua);

	return rc;
}

static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		chg_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];
	prev_fcc_ua = bcdev->thermal_fcc_ua;
	bcdev->thermal_fcc_ua = fcc_ua;

	rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
	if (!rc)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = get_property_id(pst, prop);
	if (prop_id < 0)
		return prop_id;
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		pval->intval = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);
		if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) &&
		   (bcdev->fake_soc >= 0 && bcdev->fake_soc <= 100))
			pval->intval = bcdev->fake_soc;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		pval->intval = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		pval->intval = bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		pval->intval = bcdev->num_thermal_levels;
		break;
	default:
		pval->intval = pst->prop[prop_id];
		break;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct battery_chg_dev *bcdev = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(bcdev, pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
#ifdef OPLUS_FEATURE_CHG_BASIC
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
#endif
};

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};

__maybe_unused static int battery_chg_init_psy(struct battery_chg_dev *bcdev)
{
	struct power_supply_config psy_cfg = {};
	int rc;

	psy_cfg.drv_data = bcdev;
	psy_cfg.of_node = bcdev->dev->of_node;
	bcdev->psy_list[PSY_TYPE_BATTERY].psy =
		devm_power_supply_register(bcdev->dev, &batt_psy_desc,
						&psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_BATTERY].psy);
		chg_err("Failed to register battery power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_USB].psy =
		devm_power_supply_register(bcdev->dev, &usb_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_USB].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_USB].psy);
		chg_err("Failed to register USB power supply, rc=%d\n", rc);
		return rc;
	}

	bcdev->psy_list[PSY_TYPE_WLS].psy =
		devm_power_supply_register(bcdev->dev, &wls_psy_desc, &psy_cfg);
	if (IS_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy)) {
		rc = PTR_ERR(bcdev->psy_list[PSY_TYPE_WLS].psy);
		chg_err("Failed to register wireless power supply, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int wireless_fw_send_firmware(struct battery_chg_dev *bcdev,
					const struct firmware *fw)
{
	struct wireless_fw_push_buf_req msg = {};
	const u8 *ptr;
	u32 i, num_chunks, partial_chunk_size;
	int rc;

	num_chunks = fw->size / WLS_FW_BUF_SIZE;
	partial_chunk_size = fw->size % WLS_FW_BUF_SIZE;

	if (!num_chunks)
		return -EINVAL;

	pr_debug("Updating FW...\n");

	ptr = fw->data;
	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_WLS_FW_PUSH_BUF_REQ;

	for (i = 0; i < num_chunks; i++, ptr += WLS_FW_BUF_SIZE) {
		msg.fw_chunk_id = i + 1;
		memcpy(msg.buf, ptr, WLS_FW_BUF_SIZE);

		pr_debug("sending FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	if (partial_chunk_size) {
		msg.fw_chunk_id = i + 1;
		memset(msg.buf, 0, WLS_FW_BUF_SIZE);
		memcpy(msg.buf, ptr, partial_chunk_size);

		pr_debug("sending partial FW chunk %u\n", i + 1);
		rc = battery_chg_fw_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wireless_fw_check_for_update(struct battery_chg_dev *bcdev,
					u32 version, size_t size)
{
	struct wireless_fw_check_req req_msg = {};

	bcdev->wls_fw_update_reqd = false;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_CHECK_UPDATE;
	req_msg.fw_version = version;
	req_msg.fw_size = size;
	req_msg.fw_crc = bcdev->wls_fw_crc;

	return battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
}

#define IDT_FW_MAJOR_VER_OFFSET		0x94
#define IDT_FW_MINOR_VER_OFFSET		0x96
static int wireless_fw_update(struct battery_chg_dev *bcdev, bool force)
{
	const struct firmware *fw;
	struct psy_state *pst;
	u32 version;
	u16 maj_ver, min_ver;
	int rc;

	pm_stay_awake(bcdev->dev);

	/*
	 * Check for USB presence. If nothing is connected, check whether
	 * battery SOC is at least 50% before allowing FW update.
	 */
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_ONLINE);
	if (rc < 0)
		goto out;

	if (!pst->prop[USB_ONLINE]) {
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		rc = read_property_id(bcdev, pst, BATT_CAPACITY);
		if (rc < 0)
			goto out;

		if ((pst->prop[BATT_CAPACITY] / 100) < 50) {
			chg_err("Battery SOC should be at least 50%% or connect charger\n");
			rc = -EINVAL;
			goto out;
		}
	}

	rc = firmware_request_nowarn(&fw, bcdev->wls_fw_name, bcdev->dev);
	if (rc) {
		chg_err("Couldn't get firmware rc=%d\n", rc);
		goto out;
	}

	if (!fw || !fw->data || !fw->size) {
		chg_err("Invalid firmware\n");
		rc = -EINVAL;
		goto release_fw;
	}

	if (fw->size < SZ_16K) {
		chg_err("Invalid firmware size %zu\n", fw->size);
		rc = -EINVAL;
		goto release_fw;
	}

	maj_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MAJOR_VER_OFFSET));
	min_ver = le16_to_cpu(*(__le16 *)(fw->data + IDT_FW_MINOR_VER_OFFSET));
	version = maj_ver << 16 | min_ver;

	if (force)
		version = UINT_MAX;

	pr_debug("FW size: %zu version: %#x\n", fw->size, version);

	rc = wireless_fw_check_for_update(bcdev, version, fw->size);
	if (rc < 0) {
		chg_err("Wireless FW update not needed, rc=%d\n", rc);
		goto release_fw;
	}

	if (!bcdev->wls_fw_update_reqd) {
		pr_warn("Wireless FW update not required\n");
		goto release_fw;
	}

	/* Wait for IDT to be setup by charger firmware */
	msleep(WLS_FW_PREPARE_TIME_MS);

	reinit_completion(&bcdev->fw_update_ack);
	rc = wireless_fw_send_firmware(bcdev, fw);
	if (rc < 0) {
		chg_err("Failed to send FW chunk, rc=%d\n", rc);
		goto release_fw;
	}

	rc = wait_for_completion_timeout(&bcdev->fw_update_ack,
				msecs_to_jiffies(WLS_FW_WAIT_TIME_MS));
	if (!rc) {
		chg_err("Error, timed out updating firmware\n");
		rc = -ETIMEDOUT;
		goto release_fw;
	} else {
		rc = 0;
	}

	chg_info("Wireless FW update done\n");

release_fw:
	release_firmware(fw);
out:
	pm_relax(bcdev->dev);

	return rc;
}

static ssize_t wireless_fw_version_show(struct class *c,
					struct class_attribute *attr,
					char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct wireless_fw_get_version_req req_msg = {};
	int rc;

	req_msg.hdr.owner = MSG_OWNER_BC;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = BC_WLS_FW_GET_VERSION;

	rc = battery_chg_write(bcdev, &req_msg, sizeof(req_msg));
	if (rc < 0) {
		chg_err("Failed to get FW version rc=%d\n", rc);
		return rc;
	}

	return scnprintf(buf, PAGE_SIZE, "%#x\n", bcdev->wls_fw_version);
}
static CLASS_ATTR_RO(wireless_fw_version);

static ssize_t wireless_fw_force_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, true);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_force_update);

static ssize_t wireless_fw_update_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	bool val;
	int rc;

	if (kstrtobool(buf, &val) || !val)
		return -EINVAL;

	rc = wireless_fw_update(bcdev, false);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_WO(wireless_fw_update);

static ssize_t usb_typec_compliant_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_TYPEC_COMPLIANT);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(int)pst->prop[USB_TYPEC_COMPLIANT]);
}
static CLASS_ATTR_RO(usb_typec_compliant);

static ssize_t usb_real_type_show(struct class *c,
				struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_REAL_TYPE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			get_usb_type_name(pst->prop[USB_REAL_TYPE]));
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t restrict_cur_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	u32 fcc_ua, prev_fcc_ua;

	if (kstrtou32(buf, 0, &fcc_ua) || fcc_ua > bcdev->thermal_fcc_ua)
		return -EINVAL;

	prev_fcc_ua = bcdev->restrict_fcc_ua;
	bcdev->restrict_fcc_ua = fcc_ua;
	if (bcdev->restrict_chg_en) {
		rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
		if (rc < 0) {
			bcdev->restrict_fcc_ua = prev_fcc_ua;
			return rc;
		}
	}

	return count;
}

static ssize_t restrict_cur_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%u\n", bcdev->restrict_fcc_ua);
}
static CLASS_ATTR_RW(restrict_cur);

static ssize_t restrict_chg_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	bcdev->restrict_chg_en = val;
	rc = __battery_psy_set_charge_current(bcdev, bcdev->restrict_chg_en ?
			bcdev->restrict_fcc_ua : bcdev->thermal_fcc_ua);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t restrict_chg_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->restrict_chg_en);
}
static CLASS_ATTR_RW(restrict_chg);

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	bcdev->fake_soc = val;
	pr_debug("Set fake soc to %d\n", val);

	if (IS_ENABLED(CONFIG_QTI_PMIC_GLINK_CLIENT_DEBUG) && is_batt_psy_available(bcdev))
		power_supply_changed(pst->psy);

	return count;
}

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->fake_soc);
}
static CLASS_ATTR_RW(fake_soc);

static ssize_t wireless_boost_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_WLS],
				WLS_BOOST_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t wireless_boost_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int rc;

	rc = read_property_id(bcdev, pst, WLS_BOOST_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[WLS_BOOST_EN]);
}
static CLASS_ATTR_RW(wireless_boost_en);

static ssize_t moisture_detection_en_store(struct class *c,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_USB],
				USB_MOISTURE_DET_EN, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t moisture_detection_en_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_EN);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_EN]);
}
static CLASS_ATTR_RW(moisture_detection_en);

static ssize_t moisture_detection_status_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int rc;

	rc = read_property_id(bcdev, pst, USB_MOISTURE_DET_STS);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pst->prop[USB_MOISTURE_DET_STS]);
}
static CLASS_ATTR_RO(moisture_detection_status);

static ssize_t resistance_show(struct class *c,
					struct class_attribute *attr, char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_RESISTANCE);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", pst->prop[BATT_RESISTANCE]);
}
static CLASS_ATTR_RO(resistance);

static ssize_t soh_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int rc;

	rc = read_property_id(bcdev, pst, BATT_SOH);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pst->prop[BATT_SOH]);
}
static CLASS_ATTR_RO(soh);

static ssize_t ship_mode_en_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	if (kstrtobool(buf, &bcdev->ship_mode_en))
		return -EINVAL;

	return count;
}

static ssize_t ship_mode_en_show(struct class *c, struct class_attribute *attr,
				char *buf)
{
	struct battery_chg_dev *bcdev = container_of(c, struct battery_chg_dev,
						battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bcdev->ship_mode_en);
}
static CLASS_ATTR_RW(ship_mode_en);

static struct attribute *battery_class_attrs[] = {
	&class_attr_soh.attr,
	&class_attr_resistance.attr,
	&class_attr_moisture_detection_status.attr,
	&class_attr_moisture_detection_en.attr,
	&class_attr_wireless_boost_en.attr,
	&class_attr_fake_soc.attr,
	&class_attr_wireless_fw_update.attr,
	&class_attr_wireless_fw_force_update.attr,
	&class_attr_wireless_fw_version.attr,
	&class_attr_ship_mode_en.attr,
	&class_attr_restrict_chg.attr,
	&class_attr_restrict_cur.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_usb_typec_compliant.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

#ifdef CONFIG_DEBUG_FS
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev)
{
	int rc;
	struct dentry *dir;

	dir = debugfs_create_dir("battery_charger", NULL);
	if (IS_ERR(dir)) {
		rc = PTR_ERR(dir);
		chg_err("Failed to create charger debugfs directory, rc=%d\n",
			rc);
		return;
	}

	debugfs_create_bool("block_tx", 0600, dir, &bcdev->block_tx);
	bcdev->debugfs_dir = dir;

	return;
}
#else
static void battery_chg_add_debugfs(struct battery_chg_dev *bcdev) { }
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static bool oplus_vchg_trig_is_support(void)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return false;
	}
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0)
		return false;

	if (get_PCB_Version() >= EVT1)
		return true;
	return false;
}

static int oplus_vchg_trig_gpio_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.vchg_trig_pinctrl = devm_pinctrl_get(bcdev->dev);

	bcdev->oplus_custom_gpio.vchg_trig_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl, "vchg_trig_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("get vchg_trig_default\n");
		return -EINVAL;
	}

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio > 0) {
		gpio_direction_input(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}
	pinctrl_select_state(bcdev->oplus_custom_gpio.vchg_trig_pinctrl,
		bcdev->oplus_custom_gpio.vchg_trig_default);

	chg_err("get vchg_trig_default level[%d]\n", gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio));
	return 0;
}

static int oplus_get_vchg_trig_gpio_val(void)
{
	int level = 1;
	static int pre_level = 1;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		chg_err("chip is NULL!\n");
		return -1;
	}

	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("vchg_trig_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_pinctrl)
			|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.vchg_trig_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	level = gpio_get_value(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	if (pre_level ^ level) {
		pre_level = level;
		chg_err("!!!!! vchg_trig gpio level[%d], wired[%d]\n", level, !level);
	}
	return level;
}

static int oplus_mcu_en_gpio_init(struct battery_chg_dev *bcdev)
{
	int level;

	if (bcdev->oplus_custom_gpio.mcu_en_gpio <= 0) {
		chg_err("don't need init mcu_en_gpio\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.mcu_en_pinctrl = devm_pinctrl_get(bcdev->dev);
	bcdev->oplus_custom_gpio.mcu_en_active =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.mcu_en_pinctrl,
		"mcu_en_active");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.mcu_en_active)) {
		chg_err("get mcu_en_active fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.mcu_en_sleep =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.mcu_en_pinctrl,
		"mcu_en_sleep");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.mcu_en_sleep)) {
		chg_err("get mcu_en_sleep fail\n");
		return -EINVAL;
	}

	mutex_lock(&bcdev->oplus_custom_gpio.pinctrl_mutex);
	pinctrl_select_state(bcdev->oplus_custom_gpio.mcu_en_pinctrl, bcdev->oplus_custom_gpio.mcu_en_sleep);
	usleep_range(1000, 1000);
	pinctrl_select_state(bcdev->oplus_custom_gpio.mcu_en_pinctrl, bcdev->oplus_custom_gpio.mcu_en_active);
	usleep_range(20000, 20000);
	pinctrl_select_state(bcdev->oplus_custom_gpio.mcu_en_pinctrl, bcdev->oplus_custom_gpio.mcu_en_sleep);
	level = gpio_get_value(bcdev->oplus_custom_gpio.mcu_en_gpio);
	chg_info("mcu_en_gpio level=%d\n", level);
	mutex_unlock(&bcdev->oplus_custom_gpio.pinctrl_mutex);

	return 0;
}

static void oplus_chg_mcu_en_init_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, mcu_en_init_work.work);

	int rc;

	rc = oplus_mcu_en_gpio_init(bcdev);
	if (rc < 0)
		chg_err("failed to init mcu_en gpio,rc=%d\n", rc);
}

static int vchg_trig_status = -1;
static int oplus_get_vchg_trig_status(void)
{
	if (vchg_trig_status == -1) {
		vchg_trig_status = !!oplus_get_vchg_trig_gpio_val();
	}
	return vchg_trig_status;
}

static void oplus_vchg_trig_work(struct work_struct *work)
{
#ifdef OPLUS_CHG_UNDEF /* TODO */
	int level;
	static bool pre_otg = false;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct oplus_chg_chip *chip = get_oplus_chg_chip();

	if (!chip || !bcdev) {
		chg_err("chip or bcdev is NULL!\n");
		return;
	}

	level = oplus_get_vchg_trig_gpio_val();
	vchg_trig_status = !!level;
	if (level == 0) {
		if (bcdev->otg_online == true) {
			pre_otg = true;
			return;
		}
		if (chip->wireless_support)
			oplus_switch_to_wired_charge(bcdev);
	} else {
		if (pre_otg == true) {
			pre_otg = false;
			return;
		}
		if (chip->wireless_support
			&& chip->voocphy.fastchg_to_warm == false
			&& chip->voocphy.fastchg_to_normal == false)
			oplus_switch_from_wired_charge(bcdev);
	}

	if (chip->voocphy.fastchg_to_warm == false
		&& chip->voocphy.fastchg_to_normal == false) {
		oplus_chg_wake_update_work();
	}
#endif
}

static void oplus_vchg_trig_irq_init(struct battery_chg_dev *bcdev)
{
	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return;
	}

	bcdev->vchg_trig_irq = gpio_to_irq(bcdev->oplus_custom_gpio.vchg_trig_gpio);
	chg_info("vchg_trig_irq[%d]\n", bcdev->vchg_trig_irq);
}

#define VCHG_TRIG_DELAY_MS	50
irqreturn_t oplus_vchg_trig_change_handler(int irq, void *data)
{
	struct battery_chg_dev *bcdev = data;

	cancel_delayed_work_sync(&bcdev->vchg_trig_work);
	chg_info("scheduling vchg_trig work\n");
	schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(VCHG_TRIG_DELAY_MS));

	return IRQ_HANDLED;
}

static void oplus_vchg_trig_irq_register(struct battery_chg_dev *bcdev)
{
	int ret = 0;

	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return;
	}

	ret = devm_request_threaded_irq(bcdev->dev, bcdev->vchg_trig_irq,
			NULL, oplus_vchg_trig_change_handler, IRQF_TRIGGER_FALLING
			| IRQF_TRIGGER_RISING | IRQF_ONESHOT, "vchg_trig_change", bcdev);
	if (ret < 0)
		chg_err("Unable to request vchg_trig_change irq: %d\n", ret);

	ret = enable_irq_wake(bcdev->vchg_trig_irq);
	if (ret != 0)
		chg_err("enable_irq_wake: vchg_trig_irq failed %d\n", ret);
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

#ifdef OPLUS_FEATURE_CHG_BASIC
static void smbchg_enter_shipmode_pmic(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = write_property_id(bcdev, pst, BATT_SET_SHIP_MODE, 1);
	if (rc) {
		chg_err("set ship mode fail, rc=%d\n", rc);
		return;
	}
	chg_debug("power off after 15s\n");
}

#define BTB_TEMP_DEFAULT	25
static int oplus_chg_get_battery_btb_temp_cal(struct battery_chg_dev *bcdev)
{
	int rc;
	int temp = BTB_TEMP_DEFAULT;
	int batt0_con_btb_temp = BTB_TEMP_DEFAULT;
	int batt1_con_btb_temp = BTB_TEMP_DEFAULT;

	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return temp;
	}

	if (!IS_ERR_OR_NULL(bcdev->iio.batt0_con_btb_chan)) {
		rc = iio_read_channel_processed(bcdev->iio.batt0_con_btb_chan, &temp);
		if (rc < 0) {
			chg_err("iio_read_channel_processed get error\n");
		} else {
			 batt0_con_btb_temp = temp / 1000;
		}
	} else {
		chg_err("batt0_con_btb_chan is NULL !\n");
	}

	if (!IS_ERR_OR_NULL(bcdev->iio.batt1_con_btb_chan)) {
		rc = iio_read_channel_processed(bcdev->iio.batt1_con_btb_chan, &temp);
		if (rc < 0) {
			chg_err("iio_read_channel_processed get error\n");
		} else {
			batt1_con_btb_temp = temp / 1000;
		}
	} else {
		chg_err("batt1_con_btb_chan is NULL !\n");
	}

	chg_info("batt_con_btb_temp %d %d\n", batt0_con_btb_temp, batt1_con_btb_temp);

	return batt0_con_btb_temp > batt1_con_btb_temp ? batt0_con_btb_temp : batt1_con_btb_temp;
}

static int oplus_chg_get_usb_btb_temp_cal(struct battery_chg_dev *bcdev)
{
	int rc;
	int temp = 25;

	if (!bcdev) {
		chg_err("bcdev not ready\n");
		return temp;
	}

	if (IS_ERR_OR_NULL(bcdev->iio.usbcon_btb_chan)) {
		chg_err("bcdev->iio.usbcon_btb_chan is NULL\n");
		return temp;
	}

	rc = iio_read_channel_processed(bcdev->iio.usbcon_btb_chan, &temp);
	if (rc < 0) {
		chg_err("iio_read_channel_processed get error\n");
		return temp;
	}

	return temp / 1000;
}

static int oplus_usbtemp_iio_init(struct battery_chg_dev *bcdev)
{
	int rc = 0;

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names", "batt0_con_therm_adc");
	if (rc >= 0) {
		bcdev->iio.batt0_con_btb_chan = iio_channel_get(bcdev->dev, "batt0_con_therm_adc");
		if (IS_ERR(bcdev->iio.batt0_con_btb_chan)) {
			rc = PTR_ERR(bcdev->iio.batt0_con_btb_chan);
			if (rc != -EPROBE_DEFER) {
				dev_err(bcdev->dev, "batt0_con_btb_chan  get error, %d\n", rc);
				bcdev->iio.batt0_con_btb_chan = NULL;
				return rc;
			}
		}
		chg_info("test bcdev->iio.batt0_con_btb_chan\n");
	}

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names", "batt1_con_therm_adc");
	if (rc >= 0) {
		bcdev->iio.batt1_con_btb_chan = iio_channel_get(bcdev->dev, "batt1_con_therm_adc");
		if (IS_ERR(bcdev->iio.batt1_con_btb_chan)) {
			rc = PTR_ERR(bcdev->iio.batt1_con_btb_chan);
			if (rc != -EPROBE_DEFER) {
				chg_err("batt1_con_btb_chan  get error, %d\n", rc);
				bcdev->iio.batt1_con_btb_chan = NULL;
				return rc;
			}
		}
		chg_info("test bcdev->iio.batt1_con_btb_chan\n");
	}

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names", "conn_therm");
	if (rc >= 0) {
		bcdev->iio.usbcon_btb_chan = iio_channel_get(bcdev->dev, "conn_therm");
		if (IS_ERR(bcdev->iio.usbcon_btb_chan)) {
			rc = PTR_ERR(bcdev->iio.usbcon_btb_chan);
			if (rc != -EPROBE_DEFER) {
				chg_err("usbcon_btb_chan  get error, %d\n", rc);
				bcdev->iio.usbcon_btb_chan = NULL;
				return rc;
			}
		}
		chg_info("test bcdev->iio.usbcon_btb_chan\n");
	}

	return rc;
}

static int oplus_subboard_temp_iio_init(struct battery_chg_dev *bcdev)
{
	int rc = 0;

	rc = of_property_match_string(bcdev->dev->of_node, "io-channel-names",
				      "subboard_temp_adc");
	if (rc >= 0) {
		bcdev->iio.subboard_temp_chan = iio_channel_get(bcdev->dev,
					"subboard_temp_adc");
		if (IS_ERR(bcdev->iio.subboard_temp_chan)) {
			rc = PTR_ERR(bcdev->iio.subboard_temp_chan);
			if (rc != -EPROBE_DEFER)
				chg_err("subboard_temp chan  get  error, %d\n",	rc);
				bcdev->iio.subboard_temp_chan = NULL;
				return rc;
		}
		chg_info("test bcdev->iio.subboard_temp_chan \n");
	}
	chg_info("test bcdev->iio.subboard_temp_chan out here\n");

	return rc;
}

#define SUBBORD_HIGH_TEMP 690
#define SUBBORD_TEMP_PRE_DEFAULT 250
static int oplus_get_subboard_temp(struct oplus_chg_ic_dev *ic_dev, int *get_temp)
{
	int rc = 0;
	int i = 0;
	int subboard_temp_volt = 0;
	int subboard_temp = 0;
	static int subboard_temp_pre = SUBBORD_TEMP_PRE_DEFAULT;
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (IS_ERR_OR_NULL(bcdev->iio.subboard_temp_chan)) {
		chg_err("bcdev->iio.subboard_temp_v_chan is NULL\n");
		subboard_temp = subboard_temp_pre;
		goto exit;
	}

	rc = iio_read_channel_processed(bcdev->iio.subboard_temp_chan,
					&subboard_temp_volt);
	if (rc < 0) {
		chg_err("iio_read_channel_processed get error[%d]\n", rc);
		subboard_temp = subboard_temp_pre;
		goto exit;
	}

	subboard_temp_volt = 18 * subboard_temp_volt / 10000;

	resistance_convert_temperature_855(subboard_temp_volt, subboard_temp, i, con_temp_volt_855);

	subboard_temp_pre = subboard_temp;
	*get_temp = subboard_temp;
exit:
	return rc;
}

static int oplus_subboard_temp_gpio_init(struct battery_chg_dev *bcdev)
{
	bcdev->oplus_custom_gpio.subboard_temp_gpio_pinctrl = devm_pinctrl_get(bcdev->dev);
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.subboard_temp_gpio_pinctrl)) {
		chg_err("get usbtemp_r_gpio_pinctrl fail\n");
		return -EINVAL;
	}

	bcdev->oplus_custom_gpio.subboard_temp_gpio_default =
		pinctrl_lookup_state(bcdev->oplus_custom_gpio.subboard_temp_gpio_pinctrl,
				     "subboard_temp_gpio_default");
	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.subboard_temp_gpio_default)) {
		chg_err("set subboard_temp_gpio_default error\n");
		return -EINVAL;
	}

	mutex_lock(&bcdev->oplus_custom_gpio.pinctrl_mutex);
	pinctrl_select_state(bcdev->oplus_custom_gpio.subboard_temp_gpio_pinctrl,
		bcdev->oplus_custom_gpio.subboard_temp_gpio_default);
	mutex_unlock(&bcdev->oplus_custom_gpio.pinctrl_mutex);

	return 0;
}

static int oplus_chg_parse_custom_dt(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	node = bcdev->dev->of_node;

	bcdev->oplus_custom_gpio.mcu_en_gpio =
			of_get_named_gpio(node, "qcom,mcu-en-gpio", 0);
	if (bcdev->oplus_custom_gpio.mcu_en_gpio <= 0) {
		chg_err("Couldn't read qcom,mcu-en-gpio rc = %d, qcom,mcu-en-gpio:%d\n",
			rc, bcdev->oplus_custom_gpio.mcu_en_gpio);
	} else {
		rc = gpio_request(bcdev->oplus_custom_gpio.mcu_en_gpio, "mcu-en-gpio");
		if (rc)
			chg_err("unable to request mcu-en-gpio:%d\n",
				bcdev->oplus_custom_gpio.mcu_en_gpio);
		else
			chg_err("init mcu-en-gpio level[%d]\n",
				gpio_get_value(bcdev->oplus_custom_gpio.mcu_en_gpio));
	}

	bcdev->oplus_custom_gpio.vchg_trig_gpio =
		of_get_named_gpio(node, "qcom,vchg_trig-gpio", 0);
	if (bcdev->oplus_custom_gpio.vchg_trig_gpio <= 0) {
		chg_err("Couldn't read qcom,vchg_trig-gpio rc = %d, vchg_trig-gpio:%d\n",
					rc, bcdev->oplus_custom_gpio.vchg_trig_gpio);
	} else {
		if (oplus_vchg_trig_is_support() == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.vchg_trig_gpio, "vchg_trig-gpio");
			if (rc) {
				chg_err("unable to vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
			} else {
				rc = oplus_vchg_trig_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init vchg_trig-gpio:%d\n",
							bcdev->oplus_custom_gpio.vchg_trig_gpio);
				else
					oplus_vchg_trig_irq_init(bcdev);
			}
		}
		chg_err("vchg_trig-gpio:%d\n", bcdev->oplus_custom_gpio.vchg_trig_gpio);
	}

	bcdev->oplus_custom_gpio.otg_boost_en_gpio =
		of_get_named_gpio(node, "qcom,otg-booster-en-gpio", 0);
	if (bcdev->oplus_custom_gpio.otg_boost_en_gpio <= 0) {
		chg_err("Couldn't read qcom,otg_booster-en-gpio rc = %d, qcom,otg-booster-en-gpio:%d\n",
			rc, bcdev->oplus_custom_gpio.otg_boost_en_gpio);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.otg_boost_en_gpio) == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.otg_boost_en_gpio, "otg-boost-en-gpio");
			if (rc) {
				chg_err("unable to request otg-boost-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_boost_en_gpio);
			} else {
				rc = oplus_otg_boost_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init otg-boost-en-gpio:%d\n",
						bcdev->oplus_custom_gpio.otg_boost_en_gpio);
				else
					chg_err("init otg-boost-en-gpio level[%d]\n",
						gpio_get_value(bcdev->oplus_custom_gpio.otg_boost_en_gpio));
			}
		}
		chg_err("otg-boost-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_boost_en_gpio);
	}

	bcdev->oplus_custom_gpio.otg_ovp_en_gpio =
			of_get_named_gpio(node, "qcom,otg-ovp-en-gpio", 0);
	if (bcdev->oplus_custom_gpio.otg_ovp_en_gpio <= 0) {
		chg_err("Couldn't read qcom,otg-ovp-en-gpio rc = %d, qcom,otg-ovp-en-gpio:%d\n",
			rc, bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
	} else {
		if (gpio_is_valid(bcdev->oplus_custom_gpio.otg_ovp_en_gpio) == true) {
			rc = gpio_request(bcdev->oplus_custom_gpio.otg_ovp_en_gpio, "otg-ovp-en-gpio");
			if (rc) {
				chg_err("unable to request otg-ovp-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
			} else {
				rc = oplus_otg_ovp_en_gpio_init(bcdev);
				if (rc)
					chg_err("unable to init otg-ovp-en-gpio:%d\n",
						bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
				else
					chg_err("init otg-ovp-en-gpio level[%d]\n",
						gpio_get_value(bcdev->oplus_custom_gpio.otg_ovp_en_gpio));
			}
		}
		chg_err("otg-ovp-en-gpio:%d\n", bcdev->oplus_custom_gpio.otg_ovp_en_gpio);
	}

	rc = of_property_read_u32(node, "oplus,otg_scheme",
				  &bcdev->otg_scheme);
	if (rc) {
		bcdev->otg_scheme = OTG_SCHEME_UNDEFINE;
	}

	rc = of_property_read_u32(node, "oplus,otg_boost_src",
				  &bcdev->otg_boost_src);
	if (rc) {
		bcdev->otg_boost_src = OTG_BOOST_SOURCE_EXTERNAL;
	}

	rc = of_property_read_u32(node, "oplus,otg_curr_limit_max",
				  &bcdev->otg_curr_limit_max);
	if (rc) {
		chg_err("failed to get oplus,otg_curr_limit_max, rc = %d\n", rc);
		bcdev->otg_curr_limit_max = USB_OTG_CURR_LIMIT_MAX;
	}

	rc = of_property_read_u32(node, "oplus,otg_curr_limit_high",
				  &bcdev->otg_curr_limit_high);
	if (rc) {
		chg_err("failed to get oplus,otg_curr_limit_high, rc = %d\n", rc);
		bcdev->otg_curr_limit_high = USB_OTG_CURR_LIMIT_HIGH;
	}

	rc = of_property_read_u32(node, "oplus,otg_real_soc_min",
				  &bcdev->otg_real_soc_min);
	if (rc) {
		chg_err("failed to get oplus,otg_real_soc_min, rc = %d\n", rc);
		bcdev->otg_real_soc_min = USB_OTG_REAL_SOC_MIN;
	}

	bcdev->ufcs_run_check_support = of_property_read_bool(node, "oplus,ufcs_run_check_support");
	return 0;
}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

static int battery_chg_parse_dt(struct battery_chg_dev *bcdev)
{
	struct device_node *node = bcdev->dev->of_node;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int i, rc, len;
	u32 prev, val;

#ifdef OPLUS_FEATURE_CHG_BASIC
	bcdev->otg_online = false;
	bcdev->pd_svooc = false;
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_subboard_temp_gpio_init(bcdev);
#endif
	of_property_read_string(node, "qcom,wireless-fw-name",
				&bcdev->wls_fw_name);
	bcdev->oem_lcm_check = of_property_read_bool(node, "oplus,oem-lcm-check");
	rc = of_property_count_elems_of_size(node, "qcom,thermal-mitigation",
						sizeof(u32));
	if (rc <= 0)
		return 0;

	len = rc;

	rc = read_property_id(bcdev, pst, BATT_CHG_CTRL_LIM_MAX);
	if (rc < 0)
		return rc;

	prev = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	for (i = 0; i < len; i++) {
		rc = of_property_read_u32_index(node, "qcom,thermal-mitigation",
						i, &val);
		if (rc < 0)
			return rc;

		if (val > prev) {
			chg_err("Thermal levels should be in descending order\n");
			bcdev->num_thermal_levels = -EINVAL;
			return 0;
		}

		prev = val;
	}

	bcdev->thermal_levels = devm_kcalloc(bcdev->dev, len + 1,
					sizeof(*bcdev->thermal_levels),
					GFP_KERNEL);
	if (!bcdev->thermal_levels)
		return -ENOMEM;

	/*
	 * Element 0 is for normal charging current. Elements from index 1
	 * onwards is for thermal mitigation charging currents.
	 */

	bcdev->thermal_levels[0] = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	rc = of_property_read_u32_array(node, "qcom,thermal-mitigation",
					&bcdev->thermal_levels[1], len);
	if (rc < 0) {
		chg_err("Error in reading qcom,thermal-mitigation, rc=%d\n", rc);
		return rc;
	}

	bcdev->num_thermal_levels = len;
	bcdev->thermal_fcc_ua = pst->prop[BATT_CHG_CTRL_LIM_MAX];

	return 0;
}

static int battery_chg_ship_mode(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct battery_charger_ship_mode_req_msg msg = { { 0 } };
	struct battery_chg_dev *bcdev = container_of(nb, struct battery_chg_dev,
						     reboot_notifier);
	int rc;

	if (!bcdev->ship_mode_en)
		return NOTIFY_DONE;

	msg.hdr.owner = MSG_OWNER_BC;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = BC_SHIP_MODE_REQ_SET;
	msg.ship_mode_type = SHIP_MODE_PMIC;

	if (code == SYS_POWER_OFF) {
		rc = battery_chg_write(bcdev, &msg, sizeof(msg));
		if (rc < 0)
			pr_emerg("Failed to write ship mode: %d\n", rc);
	}

	return NOTIFY_DONE;
}

/**********************************************************************
 * battery charge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_get_voocphy_enable(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return 0;
	}
	if (oplus_chg_get_voocphy_support(bcdev) != ADSP_VOOCPHY)
		return 1;

	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VOOCPHY_ENABLE);
	if (rc) {
		chg_err("get enable voocphy fail, rc=%d\n", rc);
		return 0;
	} else {
		chg_err("get enable voocphy success, rc=%d\n", rc);
	}

	return pst->prop[USB_VOOCPHY_ENABLE];
}

static int oplus_voocphy_enable(struct battery_chg_dev *bcdev, bool enable)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	if (oplus_chg_get_voocphy_support(bcdev) != ADSP_VOOCPHY)
		return rc;

	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, enable);
	if (rc) {
		chg_err("set %s voocphy fail, rc=%d\n", enable ? "enable" : "disable", rc);
	} else {
		chg_err("set %s voocphy success, rc=%d\n", enable ? "enable" : "disable", rc);
	}

	return rc;
}


int oplus_adsp_voocphy_reset_again(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_VOOCPHY_RESET_AGAIN, true);
	if (rc) {
		chg_err("set voocphy_reset_again fail, rc=%d\n", rc);
	} else {
		chg_err("set voocphy_reset_again success, rc=%d\n", rc);
	}

	return rc;
}

static void oplus_voocphy_err_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, voocphy_err_work.work);
	int mmi_chg = 1;

	chg_info("start voocphy error check\n");
	if (oplus_vooc_get_fastchg_ing(bcdev) == false && bcdev->voocphy_err_check) {
		chg_err("!!!happend\n");
		bcdev->voocphy_err_check = false;
		oplus_chg_suspend_charger(true, DEF_VOTER);
		usleep_range(1000000, 1000010);
		if (is_chg_disable_votable_available(bcdev))
			mmi_chg = !get_client_vote(bcdev->chg_disable_votable, MMI_CHG_VOTER);
		if (mmi_chg) {
			oplus_chg_suspend_charger(false, DEF_VOTER);
			oplus_chg_disable_charger(false, DEF_VOTER);
			oplus_adsp_voocphy_reset_again();
		}
	}
}

static int smbchg_lcm_en(struct battery_chg_dev *bcdev, bool en)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (en)
		rc = write_property_id(bcdev, pst, USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE, 0);
	else
		rc = write_property_id(bcdev, pst, USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE, 1);
	if (rc < 0)
		chg_info("set lcm to %u error, rc = %d\n", en, rc);
	else
		chg_info("set lcm to %d \n", en);

	return rc;
}

static int oplus_get_batt_full_status(struct battery_chg_dev *bcdev)
{
	union mms_msg_data data = {0};

	if (is_common_topic_available(bcdev)) {
		oplus_mms_get_item_data(bcdev->common_topic,
					COMM_ITEM_CHG_FULL, &data, false);
	} else {
		chg_err("common topic not found\n");
	}

	return data.intval;
}

void lcm_frequency_ctrl(struct battery_chg_dev *bcdev)
{
	static int lcm_en_flag = LCM_EN_DEAFULT;
	static int  check_count = 0;

	check_count++;
	if (check_count > LCM_CHECK_COUNT) {
		lcm_en_flag = LCM_EN_DEAFULT;
		check_count = 0;
	}

	if ((oplus_wired_get_vbus() > LCM_CHARGER_VOL_THR_MV)) {
		if (oplus_get_batt_full_status(bcdev) || oplus_wired_output_is_enable()) {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				smbchg_lcm_en(bcdev, true);
				chg_info("lcm_en_flag:%d\n", lcm_en_flag);
			}
		} else {
			if (lcm_en_flag != LCM_EN_DISABLE) {
				lcm_en_flag = LCM_EN_DISABLE;
				smbchg_lcm_en(bcdev, false);
				chg_info(" lcm_en_flag:%d\n", lcm_en_flag);
			}
		}

		mod_delayed_work(system_highpri_wq, &bcdev->ctrl_lcm_frequency,
				 LCM_FREQUENCY_INTERVAL);
	} else {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				smbchg_lcm_en(bcdev, true);
				chg_info(" lcm_en_flag:%d\n", lcm_en_flag);
			}
	}
}

static void oplus_chg_ctrl_lcm_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, ctrl_lcm_frequency.work);

	lcm_frequency_ctrl(bcdev);
}

static void oplus_plugin_irq_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, plugin_irq_work.work);
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	static bool usb_pre_plugin_status;
	static bool usb_plugin_status;
	int rc = 0;

	rc = read_property_id(bcdev, pst, USB_IN_STATUS);
	if (rc) {
		bcdev->usb_in_status = 0;
		chg_err("read usb_in_status fail, rc=%d\n", rc);
		return;
	}
	if (pst->prop[USB_IN_STATUS] > 0) {
		bcdev->rerun_max = 3;
		bcdev->usb_in_status = 1;
	} else {
		bcdev->usb_in_status = 0;
	}
	usb_plugin_status = pst->prop[USB_IN_STATUS] & 0xff;
	chg_info("prop[%d], usb_online[%d]\n", pst->prop[USB_IN_STATUS],
		 bcdev->usb_in_status);

#ifdef OPLUS_CHG_UNDEF /* TODO */
	if (bcdev && bcdev->ctrl_lcm_frequency.work.func) {
		mod_delayed_work(system_highpri_wq, &bcdev->ctrl_lcm_frequency, 50);
	}
#endif

#ifdef OPLUS_CHG_UNDEF /* TODO */
	if (bcdev->usb_ocm) {
		if (bcdev->usb_in_status == 1) {
			if (g_oplus_chip && g_oplus_chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS)
				g_oplus_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
			oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_ONLINE);
		} else {
			if ((oplus_get_wired_chg_present() == false)
			    && (g_oplus_chip->charger_volt < CHARGER_PRESENT_VOLT_MV)) {
				bcdev->pd_svooc = false; /* remove svooc flag */
			}
			oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OFFLINE);
		}
	}
#endif
	chg_info("usb_pre_plugin_status[%d], usb_plugin_status[%d]\n",
		 usb_pre_plugin_status, usb_plugin_status);
	if (usb_pre_plugin_status != usb_plugin_status || !usb_plugin_status) {
		oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_PLUGIN);
	}
	if (usb_pre_plugin_status != usb_plugin_status && !usb_pre_plugin_status)
		bcdev->read_by_reg = 0;
	usb_pre_plugin_status = usb_plugin_status;

	if (bcdev->usb_in_status == 0) {
		bcdev->pd_svooc = false;
		bcdev->ufcs_power_ready = false;
		bcdev->ufcs_handshake_ok = false;
		bcdev->ufcs_pdo_ready = false;
		bcdev->bc12_completed = false;
		bcdev->hvdcp_detach_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
		chg_err("the hvdcp_detach_time:%llu, detect time %llu \n",
			bcdev->hvdcp_detach_time, bcdev->hvdcp_detect_time);
		if (bcdev->hvdcp_detach_time - bcdev->hvdcp_detect_time <= OPLUS_HVDCP_DETECT_TO_DETACH_TIME) {
			bcdev->hvdcp_disable = true;
			schedule_delayed_work(&bcdev->hvdcp_disable_work, OPLUS_HVDCP_DISABLE_INTERVAL);
		} else {
			bcdev->hvdcp_detect_ok = false;
			bcdev->hvdcp_detect_time = 0;
			bcdev->hvdcp_disable = false;
		}
		bcdev->voocphy_err_check = false;
		cancel_delayed_work_sync(&bcdev->qc_type_check_work);
		cancel_delayed_work_sync(&bcdev->voocphy_err_work);
	}
	if (bcdev->usb_in_status == 1) {
		schedule_delayed_work(&bcdev->pd_only_check_work, OPLUS_PD_ONLY_CHECK_INTERVAL);
	}
}

#endif /* OPLUS_FEATURE_CHG_BASIC */

/**********************************************************************
 * battery gauge ops *
 **********************************************************************/
#ifdef OPLUS_FEATURE_CHG_BASIC
__maybe_unused static int fg_sm8350_get_battery_mvolts(void)
{
	int rc = 0;
	int prop_id = 0;
	static int volt = 4000;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		volt = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[2], 1000);
		return volt;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery volt fail, rc=%d\n", rc);
		return volt;
	}
	volt = DIV_ROUND_CLOSEST(pst->prop[prop_id], 1000);

	return volt;
}

static int fg_sm8350_get_battery_temperature(void)
{
	int rc = 0;
	int prop_id = 0;
	static int temp = 250;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		temp = bcdev->read_buffer_dump.data_buffer[0];
		goto HIGH_TEMP;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_TEMP);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery temp fail, rc=%d\n", rc);
		return temp;
	}
	temp = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 10);
HIGH_TEMP:
	if (get_eng_version() == HIGH_TEMP_AGING) {
		chg_err("CONFIG_HIGH_TEMP_VERSION enable here,"
			 "disable high tbat shutdown\n");
		if (temp > 690)
			temp = 690;
	}

	return temp;
}

static int fg_sm8350_get_batt_remaining_capacity(void)
{
	int rc = 0;
	static int batt_rm = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return batt_rm;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		batt_rm = bcdev->read_buffer_dump.data_buffer[4];
		return batt_rm;
	}

	rc = read_property_id(bcdev, pst, BATT_CHG_COUNTER);
	if (rc < 0) {
		chg_err("read battery chg counter fail, rc=%d\n", rc);
		return batt_rm;
	}
	batt_rm = DIV_ROUND_CLOSEST(pst->prop[BATT_CHG_COUNTER], 1000);

	return batt_rm;
}

static int fg_sm8350_get_battery_soc(void)
{
	int rc = 0;
	int prop_id = 0;
	static int soc = 50;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		soc = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[3], 100);
		return soc;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CAPACITY);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery soc fail, rc=%d\n", rc);
		return soc;
	}
	soc = DIV_ROUND_CLOSEST(pst->prop[prop_id], 100);

	return soc;
}

static int fg_sm8350_get_average_current(void)
{
	int rc = 0;
	int prop_id = 0;
	static int curr = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return -1;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY &&
		!bcdev->read_by_reg) {
		curr = DIV_ROUND_CLOSEST((int)bcdev->read_buffer_dump.data_buffer[1], 1000);
		return curr;
	}

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read battery curr fail, rc=%d\n", rc);
		return curr;
	}
	curr = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return curr;
}

static int fg_sm8350_get_battery_fcc(void)
{
	static int fcc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	fcc = bcdev->read_buffer_dump.data_buffer[6];
	return fcc;
}

static int fg_sm8350_get_battery_cc(void)
{
	static int cc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	cc = bcdev->read_buffer_dump.data_buffer[7];
	return cc;
}

static int fg_sm8350_get_battery_soh(void)
{
	static int soh = 0;
	struct battery_chg_dev *bcdev = g_bcdev;

	if (!bcdev) {
		return -1;
	}

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		soh = bcdev->read_buffer_dump.data_buffer[8];
		return soh;
	}

	return soh;
}

static bool fg_sm8350_get_battery_authenticate(void)
{
	int rc = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		return false;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_BATTERY_AUTH);
	if (rc < 0) {
		chg_err("read battery auth fail, rc=%d\n", rc);
		return false;
	}
	chg_info("read battery auth success, auth=%d\n", pst->prop[BATT_BATTERY_AUTH]);

	return pst->prop[BATT_BATTERY_AUTH];
}

static bool fg_sm8350_get_battery_hmac(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_BATTERY_HMAC);
	if (rc < 0) {
		chg_err("read battery hmac fail, rc=%d\n", rc);
		return false;
	}
	chg_info("read battery hmac success, hmac = %d\n", pst->prop[BATT_BATTERY_HMAC]);

	return pst->prop[BATT_BATTERY_HMAC];
}

static void fg_sm8350_set_battery_full(bool full)
{
	/*Do nothing*/
}

static int fg_sm8350_get_battery_mvolts_2cell_max(void)
{
	return fg_sm8350_get_battery_mvolts();
}

static int fg_sm8350_get_battery_mvolts_2cell_min(void)
{
	return fg_sm8350_get_battery_mvolts();
}

static int fg_bq28z610_modify_dod0(void)
{
	return 0;
}

static int fg_bq28z610_update_soc_smooth_parameter(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	int sleep_mode_status = -1;
	int retry = 0;

	while (retry++ < 3) {
		rc = write_property_id(bcdev, pst, BATT_UPDATE_SOC_SMOOTH_PARAM, 1);
		if (rc) {
			chg_err("set smooth fail, rc=%d\n", rc);
			return -1;
		}

		rc = read_property_id(bcdev, pst, BATT_UPDATE_SOC_SMOOTH_PARAM);
		if (rc)
			chg_err("read debug reg fail, rc=%d\n", rc);
		else
			sleep_mode_status = pst->prop[BATT_UPDATE_SOC_SMOOTH_PARAM];

		chg_info("bq8z610 sleep mode status = %d\n", sleep_mode_status);
		if (sleep_mode_status != 1) {
			chg_err("bq8z610 sleep mode status = %d, retry = %d, enable failed!\n",
				 sleep_mode_status, retry);
			msleep(2000);
			continue;
		} else {
			chg_info("bq8z610 sleep mode status = %d, retry = %d, enable success!\n",
                                 sleep_mode_status, retry);
			return 0;
		}
	}

	return rc;
}

static bool fg_zy0603_check_rc_sfr(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_ZY0603_CHECK_RC_SFR);
	if (rc < 0) {
		chg_err("read sfr fail, rc=%d\n", rc);
		return false;
	}
	chg_info("read sfr success, sfr err=%d\n",
		 pst->prop[BATT_ZY0603_CHECK_RC_SFR]);

	if (pst->prop[BATT_ZY0603_CHECK_RC_SFR])
		return true;
	else
		return false;
}

static int fg_zy0603_soft_reset(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_ZY0603_SOFT_RESET, 1);
	if (rc)
		chg_err("soft reset fail, rc=%d\n", rc);

	return rc;
}

static bool fg_zy0603_get_afi_update_done(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = NULL;

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_AFI_UPDATE_DONE);
	if (rc < 0) {
		chg_err("read afi update fail, rc=%d\n", rc);
		return false;
	}

	chg_info("read afi update success, afi update done=%d\n",
		 pst->prop[BATT_AFI_UPDATE_DONE]);

	if (pst->prop[BATT_AFI_UPDATE_DONE]) {
		return true;
	} else {
		return false;
	}
}

static int fg_bq28z610_get_battery_balancing_status(void)
{
	return 0;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
static ssize_t proc_debug_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[10];
	int rc = 0;
	int reg_data = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return 0;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_DEBUG_REG);
	if (rc) {
		chg_err("get enable voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("get enable voocphy success, rc=%d\n", rc);
	}

	reg_data = pst->prop[USB_DEBUG_REG];

	sprintf(page, "0x%x\n", reg_data);
	ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_debug_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
	int rc = 0;
	char buffer[10] = {0};
	int add_data = 0;
	struct battery_chg_dev *bcdev = g_bcdev;
	struct psy_state *pst = NULL;

	if (!bcdev) {
		chg_err("bcdev is NULL!\n");
		return -1;
	}
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (count > 10) {
		chg_err("%s: count so len.\n", __func__);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s: read proc input error.\n", __func__);
		return -EFAULT;
	}

	if (1 != sscanf(buffer, "0x%x", &add_data)) {
		chg_err("invalid content: '%s', length = %zd\n", buf, count);
		return -EFAULT;
	}
	chg_info("%s: add:0x%x, data:0x%x\n", __func__, (add_data >> 8) & 0xffff, (add_data & 0xff));

	rc = write_property_id(bcdev, pst, USB_DEBUG_REG, add_data);
	if (rc) {
		chg_err("set usb_debug_reg fail, rc=%d\n", rc);
	} else {
		chg_err("set usb_debug_reg success, rc=%d\n", rc);
	}

	return count;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_debug_reg_ops =
{
	.read = proc_debug_reg_read,
	.write  = proc_debug_reg_write,
	.open  = simple_open,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_debug_reg_ops =
{
	.proc_write  = proc_debug_reg_write,
	.proc_read  = proc_debug_reg_read,
};
#endif

static int init_debug_reg_proc(struct battery_chg_dev *bcdev)
{
	int ret = 0;
	struct proc_dir_entry *pr_entry_da = NULL;
	struct proc_dir_entry *pr_entry_tmp = NULL;

	pr_entry_da = proc_mkdir("8350_reg", NULL);
	if (pr_entry_da == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create debug_reg proc entry\n", __func__);
	}

	pr_entry_tmp = proc_create_data("reg", 0644, pr_entry_da, &proc_debug_reg_ops, bcdev);
	if (pr_entry_tmp == NULL) {
		ret = -ENOMEM;
		chg_debug("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	return 0;
}

static int battery_chg_pm_resume(struct device *dev)
{
	struct battery_chg_dev *bcdev = dev_get_drvdata(dev);
	atomic_set(&bcdev->suspended, 0);
	oplus_chg_ic_virq_trigger(bcdev->gauge_ic, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int battery_chg_pm_suspend(struct device *dev)
{
	struct battery_chg_dev *bcdev = dev_get_drvdata(dev);
	atomic_set(&bcdev->suspended, 1);
	return 0;
}

static const struct dev_pm_ops battery_chg_pm_ops = {
	.resume		= battery_chg_pm_resume,
	.suspend	= battery_chg_pm_suspend,
};
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
static int oplus_chg_ssr_notifier_cb(struct notifier_block *nb,
				     unsigned long code, void *data)
{
	chg_err("code: %lu\n", code);

	switch (code) {
	case QCOM_SSR_BEFORE_SHUTDOWN:
		oplus_turn_off_power_when_adsp_crash();
		break;
	case QCOM_SSR_AFTER_POWERUP:
		oplus_adsp_crash_recover_work();
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}
#endif
static int oplus_chg_8350_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int oplus_chg_8350_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static int oplus_chg_8350_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *bcdev;
	const int extra_num = 16;
	bool chg_en = false;
	int chg_type;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	oplus_chg_8350_output_is_suspend(ic_dev, &chg_en);
	oplus_chg_8350_get_charger_type(ic_dev, &chg_type);
	oem_read_buffer(bcdev);
	chg_info("sm8450_st_dump: [chg_en=%d, suspend=%d, pd_svooc=%d], subtype=0x%02x],"
	 "[oplus_UsbCommCapable=%d, oplus_pd_svooc=%d, typec_mode=%d, cid_status=0x%02x, usb_in_status=%d],"
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x, 0x%4x=0x%02x], "
	 "[0x%4x=0x%02x], \n",
	 chg_en, bcdev->read_buffer_dump.data_buffer[9], bcdev->read_buffer_dump.data_buffer[11], chg_type,
	 bcdev->read_buffer_dump.data_buffer[10], bcdev->read_buffer_dump.data_buffer[11],
	 bcdev->read_buffer_dump.data_buffer[12], bcdev->cid_status, bcdev->usb_in_status,
	 bcdev->read_buffer_dump.data_buffer[extra_num - 1], bcdev->read_buffer_dump.data_buffer[extra_num],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 1], bcdev->read_buffer_dump.data_buffer[extra_num + 2],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 3], bcdev->read_buffer_dump.data_buffer[extra_num + 4],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 5], bcdev->read_buffer_dump.data_buffer[extra_num + 6],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 7], bcdev->read_buffer_dump.data_buffer[extra_num + 8],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 9], bcdev->read_buffer_dump.data_buffer[extra_num + 10],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 11], bcdev->read_buffer_dump.data_buffer[extra_num + 12],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 13], bcdev->read_buffer_dump.data_buffer[extra_num + 14],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 15], bcdev->read_buffer_dump.data_buffer[extra_num + 16],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 17], bcdev->read_buffer_dump.data_buffer[extra_num + 18],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 19], bcdev->read_buffer_dump.data_buffer[extra_num + 20],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 21], bcdev->read_buffer_dump.data_buffer[extra_num + 22],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 23], bcdev->read_buffer_dump.data_buffer[extra_num + 24],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 25], bcdev->read_buffer_dump.data_buffer[extra_num + 26],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 27], bcdev->read_buffer_dump.data_buffer[extra_num + 28],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 29], bcdev->read_buffer_dump.data_buffer[extra_num + 30],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 31], bcdev->read_buffer_dump.data_buffer[extra_num + 32],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 33], bcdev->read_buffer_dump.data_buffer[extra_num + 34],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 35], bcdev->read_buffer_dump.data_buffer[extra_num + 36],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 37], bcdev->read_buffer_dump.data_buffer[extra_num + 38],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 39], bcdev->read_buffer_dump.data_buffer[extra_num + 40],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 41], bcdev->read_buffer_dump.data_buffer[extra_num + 42],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 43], bcdev->read_buffer_dump.data_buffer[extra_num + 44],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 45], bcdev->read_buffer_dump.data_buffer[extra_num + 46],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 47], bcdev->read_buffer_dump.data_buffer[extra_num + 48],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 49], bcdev->read_buffer_dump.data_buffer[extra_num + 50],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 51], bcdev->read_buffer_dump.data_buffer[extra_num + 52],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 53], bcdev->read_buffer_dump.data_buffer[extra_num + 54],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 55], bcdev->read_buffer_dump.data_buffer[extra_num + 56],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 57], bcdev->read_buffer_dump.data_buffer[extra_num + 58],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 59], bcdev->read_buffer_dump.data_buffer[extra_num + 60],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 61], bcdev->read_buffer_dump.data_buffer[extra_num + 62],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 63], bcdev->read_buffer_dump.data_buffer[extra_num + 64],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 65], bcdev->read_buffer_dump.data_buffer[extra_num + 66],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 67], bcdev->read_buffer_dump.data_buffer[extra_num + 68],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 69], bcdev->read_buffer_dump.data_buffer[extra_num + 70],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 71], bcdev->read_buffer_dump.data_buffer[extra_num + 72],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 73], bcdev->read_buffer_dump.data_buffer[extra_num + 74],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 75], bcdev->read_buffer_dump.data_buffer[extra_num + 76],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 77], bcdev->read_buffer_dump.data_buffer[extra_num + 78],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 79], bcdev->read_buffer_dump.data_buffer[extra_num + 80],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 81], bcdev->read_buffer_dump.data_buffer[extra_num + 82],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 83], bcdev->read_buffer_dump.data_buffer[extra_num + 84],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 85], bcdev->read_buffer_dump.data_buffer[extra_num + 86],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 87], bcdev->read_buffer_dump.data_buffer[extra_num + 88],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 89], bcdev->read_buffer_dump.data_buffer[extra_num + 90],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 91], bcdev->read_buffer_dump.data_buffer[extra_num + 92],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 93], bcdev->read_buffer_dump.data_buffer[extra_num + 94],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 95], bcdev->read_buffer_dump.data_buffer[extra_num + 96],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 97], bcdev->read_buffer_dump.data_buffer[extra_num + 98],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 99], bcdev->read_buffer_dump.data_buffer[extra_num + 100],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 101], bcdev->read_buffer_dump.data_buffer[extra_num + 102],
	 bcdev->read_buffer_dump.data_buffer[extra_num + 103], bcdev->read_buffer_dump.data_buffer[extra_num + 104]);

	return 0;
}

static int  oplus_chg_8350_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_chg_8350_input_present(struct oplus_chg_ic_dev *ic_dev, bool *present)
{
	struct battery_chg_dev *bcdev;
	bool vbus_rising = false;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_IN_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_rising fail, rc=%d\n", rc);
		return rc;
	}
	vbus_rising = pst->prop[USB_IN_STATUS];

	*present = vbus_rising;
	chg_debug("vbus_rising=%d\n", vbus_rising);
	return vbus_rising;
}

static int oplus_chg_8350_input_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, suspend ? 0 : 0xFFFFFFFF);
	if (rc)
		chg_err("set %s fail, rc=%d\n",
			suspend ? "suspend" : "unsuspend", rc);

	return rc;
}

static int oplus_chg_8350_input_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	return 0;
}

static int oplus_chg_8350_output_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_EN, suspend ? 0 : 1);
	if (rc)
		chg_err("set %s charging fail, rc=%d\n", suspend ? "suspend" : "unsuspend", rc);

	return rc;
}

static int oplus_chg_8350_output_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_CHG_EN);
	if (rc) {
		chg_err("get battery charging status fail, rc=%d\n", rc);
		return rc;
	}

	*suspend = !!pst->prop[BATT_CHG_EN];

	return rc;
}

static int qpnp_get_prop_charger_voltage_now(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	int prop_id = 0;
	static int vbus_volt = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return vbus_volt;
	}
	vbus_volt = pst->prop[prop_id] / 1000;

	return vbus_volt;
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

static bool qpnp_get_prop_vbus_collapse_status(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	bool collapse_status = false;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return false;
	}
	collapse_status = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_info("read usb vbus_collapse_status[%d]\n",
			collapse_status);
	return collapse_status;
}

static int oplus_input_current_limit_ctrl_by_vooc_write(struct oplus_chg_ic_dev *ic_dev, int current_ma)
{
	struct battery_chg_dev *bcdev;
	int rc;
	int cur_usb_icl = 0;
	int temp_curr;

	rc = oplus_chg_8350_get_icl(ic_dev, &cur_usb_icl);
	chg_info(" get cur_usb_icl = %d\n", cur_usb_icl);
	if (rc)
		return rc;

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (current_ma > cur_usb_icl) {
		for (temp_curr = cur_usb_icl; temp_curr < current_ma; temp_curr += 500) {
			msleep(MSLEEP_35MS);
			rc = oplus_chg_set_input_current_with_no_aicl(bcdev, temp_curr);
			chg_info("[up] set input_current = %d\n", temp_curr);
		}
	} else {
		for (temp_curr = cur_usb_icl; temp_curr > current_ma; temp_curr -= 500) {
			msleep(MSLEEP_35MS);
			rc = oplus_chg_set_input_current_with_no_aicl(bcdev, temp_curr);
			chg_info("[down] set input_current = %d\n", temp_curr);
		}
	}

	rc = oplus_chg_set_input_current_with_no_aicl(bcdev, current_ma);
	return rc;
}

static int oplus_chg_set_input_current(struct battery_chg_dev *bcdev, int current_ma)
{
	int rc = 0, i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int prop_id = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	struct oplus_mms *gauge_topic;

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);

	chg_info("usb input max current limit=%d setting %02x\n", current_ma, i);
	gauge_topic = oplus_mms_get_by_name("gauge");
	if (gauge_topic) {
		int batt_volt;
		union mms_msg_data data = {0};

		oplus_mms_get_item_data(gauge_topic,
					GAUGE_ITEM_VOL_MAX, &data, false);
		batt_volt = data.intval;
		if (batt_volt > 4100) {
			aicl_point = 4550;
		} else {
			aicl_point = 4500;
		}
	} else {
		chg_info("gauge_topic is null, use default aicl_point 4500\n");
		aicl_point = 4500;
	}

	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	i = 1; /* 500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		if (bcdev->rerun_max > 0) {
			bcdev->g_icl_ma = current_ma;
			schedule_delayed_work(&bcdev->vbus_collapse_rerun_icl_work,
				msecs_to_jiffies(3000)); /* vbus_collapse_status resumes after three seconds */
			bcdev->rerun_max--;
		}
		chg_debug("vbus_collapse，use 500 here\n");
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(130000, 131000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000)
		goto aicl_end;

	i = 6; /* 1750 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 3;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}

	i = 7; /* 2000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(50000, 51000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 2;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i =  i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	usleep_range(90000, 91000);
	if (qpnp_get_prop_vbus_collapse_status(bcdev) == true) {
		i = i - 1;
		goto aicl_boost_back;
	}
	chg_vol = qpnp_get_prop_charger_voltage_now(bcdev);
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000)
		goto aicl_end;

aicl_pre_step:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_info("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_end:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_info("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_boost_back:
	rc = write_property_id(bcdev, pst, prop_id, usb_icl[i] * 1000);
	if (rc) {
		chg_err("set icl to %d mA fail, rc=%d\n", usb_icl[i], rc);
	} else {
		chg_err("set icl to %d mA\n", usb_icl[i]);
	}
	chg_info("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
	goto aicl_return;
aicl_return:
	schedule_delayed_work(&bcdev->ibus_collapse_rerun_aicl_work, OPLUS_RERUN_AICL_THRESHOLD_MS);
	return rc;
}

static void oplus_vbus_collapse_rerun_icl_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, vbus_collapse_rerun_icl_work.work);

	oplus_chg_set_input_current(bcdev, bcdev->g_icl_ma);
}

static void oplus_ibus_collapse_rerun_aicl_work(struct work_struct *work)
{
	int icl_ma;
	int ibus_ma;
	struct oplus_chg_ic_dev *ic_dev;

	struct battery_chg_dev *bcdev = container_of(work,
		struct battery_chg_dev, ibus_collapse_rerun_aicl_work.work);

	if (!bcdev || !bcdev->buck_ic) {
		chg_err("bcdev is NULL");
		return;
	}

	ic_dev = bcdev->buck_ic;
	oplus_chg_8350_get_icl(ic_dev, &icl_ma);
	oplus_chg_8350_get_input_curr(ic_dev, &ibus_ma);

	if (abs(icl_ma - ibus_ma) > ICL_IBUS_ABS_THRESHOLD_MA) {
		chg_info("icl_ma:%d, ibus_ma:%d, ibus error, rerun aicl", icl_ma, ibus_ma);
		oplus_chg_8350_aicl_rerun(ic_dev);
	}
}

static int oplus_chg_8350_set_icl(struct oplus_chg_ic_dev *ic_dev,
				  bool vooc_mode, bool step, int icl_ma)
{
	struct battery_chg_dev *bcdev;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (vooc_mode && icl_ma > 0) {
		return oplus_input_current_limit_ctrl_by_vooc_write(ic_dev, icl_ma);
	}

	if (step)
		rc = oplus_chg_set_input_current(bcdev, icl_ma);
	else
		rc = oplus_chg_set_input_current_with_no_aicl(bcdev, icl_ma);

	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", icl_ma, rc);
	else
		chg_info("set icl to %d mA\n", icl_ma);

	return rc;
}

static int oplus_chg_8350_get_icl(struct oplus_chg_ic_dev *ic_dev, int *icl_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb icl fail, rc=%d\n", rc);
		return rc;
	}
	*icl_ma = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return 0;
}

static int oplus_chg_8350_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, fcc_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", fcc_ma, rc);

	return rc;
}

static int oplus_chg_8350_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		bool fastchg_ing = oplus_vooc_get_fastchg_ing(bcdev);
		int fast_chg_type = oplus_vooc_get_fast_chg_type(bcdev);
		if (fastchg_ing && (fast_chg_type == BCC_TYPE_IS_SVOOC)) {
			chg_info("fastchg started, do not set fv\n");
			return rc;
		}
	}

	if (!bcdev->voocphy_bidirect_cp_support)
		fv_mv *= bcdev->batt_num;

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_MAX);
	rc = write_property_id(bcdev, pst, prop_id, fv_mv);
	if (rc)
		chg_err("set fv to %d mV fail, rc=%d\n", fv_mv, rc);

	return rc;
}

static int oplus_chg_8350_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm_ma)
{
#ifdef OPLUS_CHG_UNDEF /* TODO */
	int rc = 0;
	u8 val_raw = 0;
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(bcdev, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
				 TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
	return rc;
#endif

	return 0;
}

static int oplus_chg_8350_set_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_CURRENT_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read ibus fail, rc=%d\n", rc);
		return rc;
	}
	*curr_ma = DIV_ROUND_CLOSEST((int)pst->prop[prop_id], 1000);

	return rc;
}

static int oplus_chg_8350_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb vbus_volt fail, rc=%d\n", rc);
		return rc;
	}
	*vol_mv = pst->prop[prop_id] / 1000;

	return rc;
}

static int oplus_chg_8350_otg_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = write_property_id(bcdev, pst, USB_OTG_VBUS_REGULATOR_ENABLE, en ? 1 : 0);
	if (rc) {
		chg_err("%s otg boost fail, rc=%d\n", en ? "enable" : "disable", rc);
		return rc;
	}
	schedule_delayed_work(&bcdev->otg_status_check_work, 0);

	return rc;
}

static int oplus_chg_8350_set_otg_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_set_otg_boost_curr_limit(struct oplus_chg_ic_dev *ic_dev, int curr_ma)
{
	return 0;
}

static int oplus_chg_8350_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	/* TODO */

	return rc;
}

static int oplus_chg_8350_aicl_rerun(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

#ifndef CONFIG_OPLUS_SM8550_CHARGER
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_SET_RERUN_AICl, 0);
	if (rc)
		chg_err("rerun aicl fail, rc=%d\n", rc);
#endif

	return rc;
}

static int oplus_chg_8350_aicl_reset(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	/* TODO */

	return rc;
}

static int oplus_chg_8350_get_cc_orientation(struct oplus_chg_ic_dev *ic_dev, int *orientation)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_TYPEC_CC_ORIENTATION);
	if (rc < 0) {
		chg_err("read typec_cc_orientation fail\n");
		return rc;
	}
	*orientation = pst->prop[USB_TYPEC_CC_ORIENTATION];

	return rc;
}

static int oplus_chg_8350_get_hw_detect(struct oplus_chg_ic_dev *ic_dev, int *detected, bool recheck)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];
	rc = read_property_id(bcdev, pst, USB_CID_STATUS);
	if (rc < 0) {
		chg_err("read cid_status fail, rc=%d\n", rc);
		return rc;
	}
	*detected = pst->prop[USB_CID_STATUS];

	return 0;
}

static int oplus_chg_8350_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct battery_chg_dev *bcdev;
	int prop_id = 0;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_USB_TYPE);
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		return rc;
	}
	switch (pst->prop[prop_id]) {
	case POWER_SUPPLY_USB_TYPE_UNKNOWN:
		*type = OPLUS_CHG_USB_TYPE_UNKNOWN;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
		*type = OPLUS_CHG_USB_TYPE_SDP;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		*type = OPLUS_CHG_USB_TYPE_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		*type = OPLUS_CHG_USB_TYPE_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		*type = OPLUS_CHG_USB_TYPE_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		*type = OPLUS_CHG_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
		*type = OPLUS_CHG_USB_TYPE_PD;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
		*type = OPLUS_CHG_USB_TYPE_PD_DRP;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
		*type = OPLUS_CHG_USB_TYPE_PD_PPS;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_SDP:
		*type = OPLUS_CHG_USB_TYPE_PD_SDP;
		break;
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
		*type = OPLUS_CHG_USB_TYPE_APPLE_BRICK_ID;
		break;
	}

	if (*type != POWER_SUPPLY_USB_TYPE_PD &&
	    *type != POWER_SUPPLY_USB_TYPE_PD_DRP &&
	    *type != POWER_SUPPLY_USB_TYPE_PD_PPS) {
		rc = read_property_id(bcdev, pst, USB_ADAP_SUBTYPE);
		if (rc < 0) {
			chg_err("read charger subtype fail, rc=%d\n", rc);
			rc = 0;
		}
		switch (pst->prop[USB_ADAP_SUBTYPE]) {
		case CHARGER_SUBTYPE_FASTCHG_VOOC:
			*type = OPLUS_CHG_USB_TYPE_VOOC;
			break;
		case CHARGER_SUBTYPE_FASTCHG_SVOOC:
			*type = OPLUS_CHG_USB_TYPE_SVOOC;
			break;
		case CHARGER_SUBTYPE_QC:
			*type = OPLUS_CHG_USB_TYPE_QC2;
			break;
		default:
			break;
		}
	}

	bcdev->charger_type = *type;

	return 0;
}

static int oplus_chg_8350_rerun_bc12(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_chg_8350_qc_detect_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (bcdev->hvdcp_disable == true) {
		chg_err("hvdcp_disable!\n");
		return -EINVAL;
	}

	if (en) {
		rc = write_property_id(bcdev, pst, BATT_SET_QC, 0);
		bcdev->hvdcp_detect_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
		schedule_delayed_work(&bcdev->qc_type_check_work,
				      msecs_to_jiffies(QC_TYPE_CHECK_INTERVAL));
	} else {
		cancel_delayed_work_sync(&bcdev->qc_type_check_work);
	}

	return rc;
}

#define PWM_COUNT	5
static int oplus_chg_8350_shipmode_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	smbchg_enter_shipmode_pmic(bcdev);

	return 0;
}

#define SET_VBUS_5V 5000
#define GET_VBUS_8V 8000
static int oplus_chg_8350_set_qc_config(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_qc_version version, int vol_mv)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	switch (version) {
	case OPLUS_CHG_QC_2_0:
		if (vol_mv != 5000 && vol_mv != 9000) {
			chg_err("Unsupported qc voltage(=%d)\n", vol_mv);
			return -EINVAL;
		}
		rc = write_property_id(bcdev, pst, BATT_SET_QC, vol_mv);
		if (rc)
			chg_err("set QC to %d mV fail, rc=%d\n", vol_mv, rc);
		else
			chg_err("set QC to %d mV, rc=%d\n", vol_mv, rc);
		msleep(350);
		if (qpnp_get_prop_charger_voltage_now(bcdev) < GET_VBUS_8V) {
			chg_err("Non-standard QC-liked adapter detected,unabled to request 9V,falls back to 5V");
			rc = write_property_id(bcdev, pst, BATT_SET_QC, SET_VBUS_5V);
			if (rc)
				chg_err("Fall back to QC 5V fail, rc=%d\n", rc);
			else
				chg_err("Fall back to QC 5V OK\n");
		}
		break;
	case OPLUS_CHG_QC_3_0:
	default:
		chg_err("Unsupported qc version(=%d)\n", version);
		return -EINVAL;
	}

	return rc;
}

static int oplus_chg_8350_set_pd_config(struct oplus_chg_ic_dev *ic_dev, u32 pdo)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int vol_mv;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	switch (PD_SRC_PDO_TYPE(pdo)) {
	case PD_SRC_PDO_TYPE_FIXED:
		vol_mv = PD_SRC_PDO_FIXED_VOLTAGE(pdo) * 50;
		if (vol_mv != 5000 && vol_mv != 9000) {
			chg_err("Unsupported pd voltage(=%d)\n", vol_mv);
			return -EINVAL;
		}
		rc = write_property_id(bcdev, pst, BATT_SET_PDO, vol_mv);
		if (rc)
			chg_err("set PD to %d mV fail, rc=%d\n", vol_mv, rc);
		else
			chg_err("set PD to %d mV, rc=%d\n", vol_mv, rc);
		break;
	case PD_SRC_PDO_TYPE_BATTERY:
	case PD_SRC_PDO_TYPE_VARIABLE:
	case PD_SRC_PDO_TYPE_AUGMENTED:
	default:
		chg_err("Unsupported pdo type(=%d)\n", PD_SRC_PDO_TYPE(pdo));
		return -EINVAL;
	}

	return rc;
}

static int oplus_chg_8350_get_props_from_adsp_by_buffer(struct oplus_chg_ic_dev *ic_dev)
{
	oplus_get_props_from_adsp_by_buffer();
	return 0;
}

static int oplus_chg_8350_gauge_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct battery_chg_dev *bcdev;

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	*suspend = atomic_read(&bcdev->suspended);

	return 0;
}

static int oplus_chg_8350_voocphy_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	/* return oplus_voocphy_enable(en); */
	return 0;
}

static int oplus_chg_8350_voocphy_reset_again(struct oplus_chg_ic_dev *ic_dev)
{
	/* return oplus_adsp_voocphy_reset_again(); */
	return 0;
}

static int oplus_chg_8350_get_charger_cycle(struct oplus_chg_ic_dev *ic_dev, int *cycle)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY) {
		*cycle = bcdev->read_buffer_dump.data_buffer[5];
		return 0;
	}

	rc = read_property_id(bcdev, pst, BATT_CYCLE_COUNT);
	if (rc) {
		chg_err("get charger_cycle fail, rc=%d\n", rc);
		return rc;
	}

	*cycle = pst->prop[BATT_CYCLE_COUNT];

	return rc;
}

static int oplus_chg_8350_wls_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_WLS];

	rc = write_property_id(bcdev, pst, WLS_BOOST_EN, en ? 1 : 0);
	if (rc)
		chg_err("%s wls boost fail, rc=%d\n", en ? "enable" : "disable", rc);

	return rc;
}

static int oplus_chg_8350_set_wls_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	return 0;
}

static int oplus_chg_8350_set_wls_boost_curr_limit(struct oplus_chg_ic_dev *ic_dev, int curr_ma)
{
	return 0;
}

static int oplus_chg_8350_get_shutdown_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_RTC_SOC);
	if (rc < 0) {
		chg_err("read battery rtc soc fail, rc=%d\n", rc);
		return rc;
	}
	*soc = pst->prop[BATT_RTC_SOC];
	chg_info("read battery rtc soc success, rtc_soc=%d\n", pst->prop[BATT_RTC_SOC]);

	return rc;
}

static int oplus_chg_8350_backup_soc(struct oplus_chg_ic_dev *ic_dev, int soc)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_RTC_SOC, soc);
	if (rc) {
		chg_err("set battery rtc soc fail, rc=%d\n", rc);
		return 0;
	}
	chg_info("write battery rtc soc success, rtc_soc=%d\n", soc);

	return rc;
}

static int oplus_chg_8350_get_vbus_collapse_status(struct oplus_chg_ic_dev *ic_dev, bool *collapse)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_VBUS_COLLAPSE_STATUS);
	if (rc < 0) {
		chg_err("read usb vbus_collapse_status fail, rc=%d\n", rc);
		return rc;
	}
	*collapse = pst->prop[USB_VBUS_COLLAPSE_STATUS];
	chg_info("read usb vbus_collapse_status[%d]\n", *collapse);
	return rc;
}

static int oplus_chg_8350_get_typec_mode(struct oplus_chg_ic_dev *ic_dev,
					enum oplus_chg_typec_port_role_type *mode)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_TYPEC_MODE);
	if (rc < 0) {
		chg_err("read typec mode fail, rc=%d\n", rc);
		return rc;
	}
	if (pst->prop[USB_TYPEC_MODE] == 0)
		*mode = TYPEC_PORT_ROLE_SNK;
	else
		*mode = TYPEC_PORT_ROLE_SRC;

	return rc;
}

static int oplus_chg_8350_set_typec_mode(struct oplus_chg_ic_dev *ic_dev,
					enum oplus_chg_typec_port_role_type mode)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (mode >= ARRAY_SIZE(qcom_typec_port_role)) {
		chg_err("typec mode(=%d) error\n", mode);
		return -EINVAL;
	}
	rc = write_property_id(bcdev, pst, USB_TYPEC_MODE, qcom_typec_port_role[mode]);
	if (rc < 0)
		chg_err("set typec mode(=%d) error\n", mode);

	return rc;
}

static int oplus_chg_8350_set_otg_switch_status(struct oplus_chg_ic_dev *ic_dev,
						bool en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_OTG_SWITCH, en);
	if (rc < 0)
		chg_err("%s otg switch error, rc=%d\n",
			en ? "enable" : "disable", rc);
	return rc;
}

static int oplus_chg_8350_get_otg_switch_status(struct oplus_chg_ic_dev *ic_dev,
						bool *en)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = read_property_id(bcdev, pst, USB_OTG_SWITCH);
	if (rc < 0) {
		chg_err("get otg switch status error, rc=%d\n", rc);
		return rc;
	}
	*en = !!pst->prop[USB_OTG_SWITCH];

	return rc;
}

static int oplus_chg_8350_cc_detect_happened(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_CCDETECT_HAPPENED, 1);
	if (rc < 0)
		chg_err("write ccdetect plugout fail, rc=%d\n", rc);
	else
		chg_info("cc detect plugout, rc=%d\n", rc);

	return rc;
}

static int oplus_chg_8350_set_curr_level(struct oplus_chg_ic_dev *ic_dev, int cool_down)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_COOL_DOWN, cool_down);
	if (rc < 0)
		chg_err("write cool down fail, rc=%d\n", rc);
	else
		chg_info("set cool down to %d, rc=%d\n", cool_down, rc);

	return rc;
}

static int oplus_chg_8350_set_match_temp(struct oplus_chg_ic_dev *ic_dev, int match_temp)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_SET_MATCH_TEMP, match_temp);
	if (rc < 0)
		chg_err("write match temp fail, rc=%d\n", rc);
	else
		chg_debug("set matchtemp to %d, rc=%d\n", match_temp, rc);

	return rc;
}

static int oplus_chg_8350_get_otg_enbale(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*enable = bcdev->otg_online;

	return 0;
}

static int oplus_chg_set_input_current_with_no_aicl(struct battery_chg_dev *bcdev, int current_ma)
{
	int rc = 0;
	int prop_id = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (current_ma == 0)
		current_ma = usb_icl[0];

	prop_id = get_property_id(pst, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT);
	rc = write_property_id(bcdev, pst, prop_id, current_ma * 1000);
	if (rc)
		chg_err("set icl to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_info("set icl to %d mA\n", current_ma);

	return rc;
}

static int oplus_chg_8350_is_oplus_svid(struct oplus_chg_ic_dev *ic_dev, bool *oplus_svid)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*oplus_svid = bcdev->pd_svooc;

	return 0;
}

static int oplus_chg_8350_hardware_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *bcdev;
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int boot_mode = get_boot_mode();
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
		oplus_chg_8350_input_suspend(ic_dev, false);
	} else {
		oplus_chg_8350_input_suspend(ic_dev, true);
	}
	chg_info("boot_mode:%d\n", boot_mode);
#else
	oplus_chg_8350_input_suspend(ic_dev, false);
#endif
	oplus_chg_set_input_current_with_no_aicl(bcdev, 500);
	oplus_chg_8350_output_suspend(ic_dev, false);

	return 0;
}

static int oplus_chg_8350_get_usb_btb_temp(struct oplus_chg_ic_dev *ic_dev,
					   int *usb_btb_temp)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*usb_btb_temp = oplus_chg_get_usb_btb_temp_cal(bcdev);

	return 0;
}

static int oplus_chg_8350_get_batt_btb_temp(struct oplus_chg_ic_dev *ic_dev,
					    int *batt_btb_temp)
{
	struct battery_chg_dev *bcdev;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*batt_btb_temp = oplus_chg_get_battery_btb_temp_cal(bcdev);

	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC
static void *oplus_chg_8350_buck_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, oplus_chg_8350_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, oplus_chg_8350_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, oplus_chg_8350_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, oplus_chg_8350_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_PRESENT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT, oplus_chg_8350_input_present);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND, oplus_chg_8350_input_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND, oplus_chg_8350_input_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND, oplus_chg_8350_output_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND, oplus_chg_8350_output_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ICL, oplus_chg_8350_set_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_ICL, oplus_chg_8350_get_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FCC, oplus_chg_8350_set_fcc);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FV, oplus_chg_8350_set_fv);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ITERM, oplus_chg_8350_set_iterm);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL, oplus_chg_8350_set_rechg_vol);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR, oplus_chg_8350_get_input_curr);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL, oplus_chg_8350_get_input_vol);
		break;
	case OPLUS_IC_FUNC_OTG_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_OTG_BOOST_ENABLE, oplus_chg_8350_otg_boost_enable);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_VOL, oplus_chg_8350_set_otg_boost_vol);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT, oplus_chg_8350_set_otg_boost_curr_limit);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_ENABLE, oplus_chg_8350_aicl_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RERUN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RERUN, oplus_chg_8350_aicl_rerun);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RESET, oplus_chg_8350_aicl_reset);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_CC_ORIENTATION, oplus_chg_8350_get_cc_orientation);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_HW_DETECT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_HW_DETECT, oplus_chg_8350_get_hw_detect);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE, oplus_chg_8350_get_charger_type);
		break;
	case OPLUS_IC_FUNC_BUCK_RERUN_BC12:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_RERUN_BC12, oplus_chg_8350_rerun_bc12);
		break;
	case OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_QC_DETECT_ENABLE, oplus_chg_8350_qc_detect_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE, oplus_chg_8350_shipmode_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_QC_CONFIG, oplus_chg_8350_set_qc_config);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_PD_CONFIG, oplus_chg_8350_set_pd_config);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE, oplus_chg_8350_get_props_from_adsp_by_buffer);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_ENABLE, oplus_chg_8350_voocphy_enable);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_RESET_AGAIN, oplus_chg_8350_voocphy_reset_again);
		break;
	case OPLUS_IC_FUNC_GET_CHARGER_CYCLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_CHARGER_CYCLE, oplus_chg_8350_get_charger_cycle);
		break;
	case OPLUS_IC_FUNC_WLS_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_WLS_BOOST_ENABLE, oplus_chg_8350_wls_boost_enable);
		break;
	case OPLUS_IC_FUNC_SET_WLS_BOOST_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_WLS_BOOST_VOL, oplus_chg_8350_set_wls_boost_vol);
		break;
	case OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_WLS_BOOST_CURR_LIMIT, oplus_chg_8350_set_wls_boost_curr_limit);
		break;
	case OPLUS_IC_FUNC_GET_SHUTDOWN_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_SHUTDOWN_SOC, oplus_chg_8350_get_shutdown_soc);
		break;
	case OPLUS_IC_FUNC_BACKUP_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BACKUP_SOC, oplus_chg_8350_backup_soc);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_VBUS_COLLAPSE_STATUS, oplus_chg_8350_get_vbus_collapse_status);
		break;
	case OPLUS_IC_FUNC_GET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_TYPEC_MODE, oplus_chg_8350_get_typec_mode);
		break;
	case OPLUS_IC_FUNC_SET_TYPEC_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_TYPEC_MODE, oplus_chg_8350_set_typec_mode);
		break;
	case OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_SWITCH_STATUS, oplus_chg_8350_set_otg_switch_status);
		break;
	case OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_OTG_SWITCH_STATUS, oplus_chg_8350_get_otg_switch_status);
		break;
	case OPLUS_IC_FUNC_CC_DETECT_HAPPENED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CC_DETECT_HAPPENED, oplus_chg_8350_cc_detect_happened);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_CURR_LEVEL, oplus_chg_8350_set_curr_level);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_MATCH_TEMP, oplus_chg_8350_set_match_temp);
		break;
	case OPLUS_IC_FUNC_GET_OTG_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_OTG_ENABLE, oplus_chg_8350_get_otg_enbale);
		break;
	case OPLUS_IC_FUNC_IS_OPLUS_SVID:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_IS_OPLUS_SVID, oplus_chg_8350_is_oplus_svid);
		break;
	case OPLUS_IC_FUNC_BUCK_HARDWARE_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_HARDWARE_INIT, oplus_chg_8350_hardware_init);
		break;
	case OPLUS_IC_FUNC_VOOCPHY_SET_BCC_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOCPHY_SET_BCC_CURR, oplus_set_bcc_curr_to_voocphy);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_USB_BTB_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_USB_BTB_TEMP,
					       oplus_chg_8350_get_usb_btb_temp);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_BATT_BTB_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_BATT_BTB_TEMP,
					       oplus_chg_8350_get_batt_btb_temp);
		break;
	case OPLUS_IC_FUNC_GET_TYPEC_ROLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_TYPEC_ROLE, oplus_chg_8350_get_typec_mode);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq oplus_chg_8350_buck_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_CC_DETECT },
	{ .virq_id = OPLUS_IC_VIRQ_PLUGIN },
	{ .virq_id = OPLUS_IC_VIRQ_CC_CHANGED },
	{ .virq_id = OPLUS_IC_VIRQ_SUSPEND_CHECK },
	{ .virq_id = OPLUS_IC_VIRQ_CHG_TYPE_CHANGE },
	{ .virq_id = OPLUS_IC_VIRQ_OTG_ENABLE },
	{ .virq_id = OPLUS_IC_VIRQ_RESUME },
	{ .virq_id = OPLUS_IC_VIRQ_SVID },
};

static int oplus_sm8350_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;

	return 0;
}

static int oplus_sm8350_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int oplus_sm8350_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_sm8350_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_sm8350_get_batt_max(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol_mv = fg_sm8350_get_battery_mvolts_2cell_max();

	return 0;
}

static int oplus_sm8350_get_batt_min(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol_mv = fg_sm8350_get_battery_mvolts_2cell_min();

	return 0;
}

static int oplus_sm8350_get_batt_curr(struct oplus_chg_ic_dev *ic_dev,
				       int *curr_ma)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*curr_ma = fg_sm8350_get_average_current();

	return 0;
}

static int oplus_sm8350_get_batt_temp(struct oplus_chg_ic_dev *ic_dev,
				       int *temp)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*temp = fg_sm8350_get_battery_temperature();

	return 0;
}

static int oplus_sm8350_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*soc = fg_sm8350_get_battery_soc();

	return 0;
}

static int oplus_sm8350_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*fcc = fg_sm8350_get_battery_fcc();

	return 0;
}

static int oplus_sm8350_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*cc = fg_sm8350_get_battery_cc();

	return 0;
}

static int oplus_sm8350_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*rm = fg_sm8350_get_batt_remaining_capacity();

	return 0;
}

static int oplus_sm8350_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*soh = fg_sm8350_get_battery_soh();

	return 0;
}

static int oplus_sm8350_get_batt_auth(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	*pass = fg_sm8350_get_battery_authenticate();

	return 0;
}

static int oplus_sm8350_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev,
				       bool *pass)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*pass = fg_sm8350_get_battery_hmac(chip);

	return 0;
}

static int oplus_sm8350_set_batt_full(struct oplus_chg_ic_dev *ic_dev,
				       bool full)
{
	fg_sm8350_set_battery_full(full);

	return 0;
}

static int oplus_sm8350_update_dod0(struct oplus_chg_ic_dev *ic_dev)
{
	return fg_bq28z610_modify_dod0();
}

static int
oplus_sm8350_update_soc_smooth_parameter(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return fg_bq28z610_update_soc_smooth_parameter(chip);
}

static int oplus_sm8350_get_cb_status(struct oplus_chg_ic_dev *ic_dev,
				       int *status)
{
	*status = fg_bq28z610_get_battery_balancing_status();

	return 0;
}

static int oplus_sm8350_set_lock(struct oplus_chg_ic_dev *ic_dev, bool lock)
{
	return 0;
}

static int oplus_sm8350_is_locked(struct oplus_chg_ic_dev *ic_dev, bool *locked)
{
	*locked = false;
	return 0;
}

static int oplus_sm8350_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*num = chip->batt_num;

	return 0;
}

static int oplus_sm8350_get_device_type(struct oplus_chg_ic_dev *ic_dev,
					 int *type)
{
	*type = 0;

	return 0;
}

static int
oplus_sm8350_get_device_type_for_vooc(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	*type = 0;

	return 0;
}

static int
oplus_sm8350_get_battery_dod0(struct oplus_chg_ic_dev *ic_dev, int index,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 1:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[0];
		break;
	case 2:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[1];
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int
oplus_sm8350_get_battery_dod0_passed_q(struct oplus_chg_ic_dev *ic_dev,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*val = bcdev->bcc_read_buffer_dump.data_buffer[2];

	return 0;
}

static int
oplus_sm8350_get_battery_qmax(struct oplus_chg_ic_dev *ic_dev, int index,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 1:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[3];
		break;
	case 2:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[4];
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int
oplus_sm8350_get_battery_qmax_passed_q(struct oplus_chg_ic_dev *ic_dev,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*val = bcdev->bcc_read_buffer_dump.data_buffer[5];

	return 0;
}

static int
oplus_sm8350_get_batt_vol(struct oplus_chg_ic_dev *ic_dev, int index,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 1:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[6];
		break;
	case 2:
		*val = bcdev->bcc_read_buffer_dump.data_buffer[11];
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int
oplus_sm8350_get_battery_gauge_type_for_bcc(struct oplus_chg_ic_dev *ic_dev,
				       int *type)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (type == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	if (bcdev->bcc_read_buffer_dump.data_buffer[11] == SW_GAUGE) {
		*type = DEVICE_ZY0603;
	} else {
		*type = DEVICE_BQ27541;
	}

	return 0;
}

static int
oplus_sm8350_get_real_time_current(struct oplus_chg_ic_dev *ic_dev,
				       int *val)
{
	struct battery_chg_dev *bcdev;

	if ((ic_dev == NULL) || (val == NULL)) {
		chg_err("!!!ic_dev null\n");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	*val = bcdev->bcc_read_buffer_dump.data_buffer[8];

	return 0;
}

static int oplus_chg_8350_get_afi_update_done(struct oplus_chg_ic_dev *ic_dev,
					      bool *status)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*status = fg_zy0603_get_afi_update_done(chip);

	return 0;
}

static int oplus_chg_8350_soft_reset(struct oplus_chg_ic_dev *ic_dev,
				     bool *reset_done)
{
	struct battery_chg_dev *chip;
	int ret;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	ret = fg_zy0603_soft_reset(chip);
	if (ret == 0)
		*reset_done = true;
	else
		*reset_done = false;

	return 0;
}

static int oplus_chg_8350_detection_reset_condition(struct oplus_chg_ic_dev *ic_dev,
						    bool *need_reset)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*need_reset = fg_zy0603_check_rc_sfr(chip);

	return 0;
}

static int oplus_set_read_mode(struct oplus_chg_ic_dev *ic_dev, int value)
{
	struct battery_chg_dev *bcdev;

        if (ic_dev == NULL) {
                chg_err("oplus_chg_ic_dev is NULL");
                return -ENODEV;
        }

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	if (!bcdev)
		return -EINVAL;

	bcdev->read_by_reg = value;
	return 0;
}

static void *oplus_chg_8350_gauge_get_func(struct oplus_chg_ic_dev *ic_dev,
					   enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_sm8350_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_sm8350_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_sm8350_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       oplus_sm8350_smt_test);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_sm8350_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_sm8350_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_sm8350_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
			oplus_sm8350_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_REAL_TIME_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_REAL_TIME_CURR,
			oplus_sm8350_get_real_time_current);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
			oplus_sm8350_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_sm8350_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_sm8350_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_sm8350_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_sm8350_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_sm8350_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
			oplus_sm8350_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
			oplus_sm8350_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
			oplus_sm8350_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE_DOD0,
					       oplus_sm8350_update_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_UPDATE_SOC_SMOOTH,
			oplus_sm8350_update_soc_smooth_parameter);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_CB_STATUS,
			oplus_sm8350_get_cb_status);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LOCK,
					       oplus_sm8350_set_lock);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
					       oplus_sm8350_is_locked);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_sm8350_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			oplus_sm8350_get_device_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			oplus_sm8350_get_device_type_for_vooc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BCC_PARMS,
			oplus_get_bcc_parameters_from_adsp);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BCC_PARMS,
			oplus_set_bcc_debug_parameters);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0,
			oplus_sm8350_get_battery_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q,
			oplus_sm8350_get_battery_dod0_passed_q);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX,
			oplus_sm8350_get_battery_qmax);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX_PASSED_Q:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX_PASSED_Q,
			oplus_sm8350_get_battery_qmax_passed_q);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC,
			oplus_sm8350_get_battery_gauge_type_for_bcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_UPDATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_UPDATE,
			oplus_chg_8350_get_props_from_adsp_by_buffer);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
			oplus_chg_8350_gauge_is_suspend);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_AFI_UPDATE_DONE,
					       oplus_chg_8350_get_afi_update_done);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_RESET,
					       oplus_chg_8350_soft_reset);
		break;
	case OPLUS_IC_FUNC_GAUGE_CHECK_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_CHECK_RESET,
					       oplus_chg_8350_detection_reset_condition);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_SUBBOARD_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_SUBBOARD_TEMP,
					       oplus_get_subboard_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_READ_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_READ_MODE,
			oplus_set_read_mode);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq oplus_chg_8350_gauge_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static int oplus_chg_adsp_dpdm_switch_init(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	if (ic_dev->online)
		return 0;
	ic_dev->online = true;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_ONLINE);

	return 0;
}

static int oplus_chg_adsp_dpdm_switch_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;
	ic_dev->online = false;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_OFFLINE);

	return 0;
}

static int oplus_chg_adsp_dpdm_switch_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_chg_adsp_dpdm_switch_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_chg_adsp_dpdm_switch_set_switch_mode(struct oplus_chg_ic_dev *ic_dev,
	enum oplus_dpdm_switch_mode mode)
{
	struct battery_chg_dev *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (mode) {
	case DPDM_SWITCH_TO_AP:
		chg_info("dpdm switch to ap\n");
		rc = oplus_adsp_voocphy_enable(false);
		break;
	case DPDM_SWITCH_TO_VOOC:
		chg_info("dpdm switch to vooc\n");
		rc = oplus_adsp_voocphy_enable(true);
		break;
	case DPDM_SWITCH_TO_UFCS:
		chg_info("dpdm switch to ufcs\n");
		rc = oplus_adsp_voocphy_enable(false);
		break;
	default:
		chg_err("not supported mode, mode=%d\n", mode);
		return -EINVAL;
	}
	chip->dpdm_switch_mode = mode;

	return rc;
}

static int oplus_chg_adsp_dpdm_switch_get_switch_mode(struct oplus_chg_ic_dev *ic_dev,
	enum oplus_dpdm_switch_mode *mode)
{
	struct battery_chg_dev *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*mode = chip->dpdm_switch_mode;

	return 0;
}

static void *oplus_chg_adsp_dpdm_switch_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}
	if (!oplus_chg_ic_func_is_support(ic_dev, func_id)) {
		chg_info("%s: this func(=%d) is not supported\n",  ic_dev->name, func_id);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, oplus_chg_adsp_dpdm_switch_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, oplus_chg_adsp_dpdm_switch_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, oplus_chg_adsp_dpdm_switch_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, oplus_chg_adsp_dpdm_switch_smt_test);
		break;
	case OPLUS_IC_FUNC_SET_DPDM_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_DPDM_SWITCH_MODE,
			oplus_chg_adsp_dpdm_switch_set_switch_mode);
		break;
	case OPLUS_IC_FUNC_GET_DPDM_SWITCH_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GET_DPDM_SWITCH_MODE,
			oplus_chg_adsp_dpdm_switch_get_switch_mode);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq oplus_chg_adsp_dpdm_switch_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
};

static int oplus_chg_adsp_ufcs_init(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	if (ic_dev->online)
		return 0;
	ic_dev->online = true;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_ONLINE);

	return 0;
}

static int oplus_chg_adsp_ufcs_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;
	ic_dev->online = false;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_OFFLINE);

	return 0;
}

static int oplus_chg_adsp_ufcs_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	return 0;
}

static int oplus_chg_adsp_ufcs_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int oplus_chg_adsp_ufcs_handshake(struct oplus_chg_ic_dev *ic_dev)
{
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;
	int rc = -1, rc1 = -1;
	int start = 1;
	int retry_count = 12;
	int bc12_wait_count = 12;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	/*add for prevent bc1.2 and ufcs waveforms from overlap*/
	if (bcdev->charger_type == OPLUS_CHG_USB_TYPE_PD ||
	    bcdev->charger_type == OPLUS_CHG_USB_TYPE_PD_DRP ||
	    bcdev->charger_type == OPLUS_CHG_USB_TYPE_PD_PPS ||
	    bcdev->pd_svooc) {
		while (bc12_wait_count--) {
			chg_info("bcdev->bc12_completed = %d\n", bcdev->bc12_completed);
			if (bcdev->bc12_completed) {
				break;
			}
			msleep(20);
		}
	}

	rc1 = write_property_id(bcdev, pst, USB_SET_UFCS_START, start);
	if (rc1 < 0) {
		chg_err("start UFCS func error, rc1=%d\n", rc1);
		return rc1;
	}

	while (retry_count--) {
		chg_info("bcdev->ufcs_handshake_ok = %d\n", bcdev->ufcs_handshake_ok);
		if (bcdev->ufcs_handshake_ok) {
			rc = 0;
			bcdev->ufcs_handshake_ok = 0;
			break;
		}
		msleep(10);
	}

	return rc;
}

static int oplus_chg_adsp_ufcs_pdo_set(struct oplus_chg_ic_dev *ic_dev, int vbus_mv, int ibus_ma)
{
	int rc1 = 0, rc2 = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *bcdev;
	int retry_count = 28;
	bool pdo_set_success = false;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	while (retry_count--) {
		chg_info("bcdev->ufcs_power_ready = %d\n", bcdev->ufcs_power_ready);
		if (bcdev->ufcs_power_ready) {
			chg_info("success set vbus_mv = %d, ibus_ma = %d\n", vbus_mv, ibus_ma);
			rc1 = write_property_id(bcdev, pst, USB_SET_UFCS_VOLT, vbus_mv);
			rc2 = write_property_id(bcdev, pst, USB_SET_UFCS_CURRENT, ibus_ma);
			if (rc1 < 0 || rc2 < 0) {
				chg_err("set ufcs config fail, rc1,rc2 = %d, %d\n", rc1, rc2);
				return -1;
			}
			bcdev->ufcs_power_ready = 0;
			pdo_set_success = true;
			break;
		}
		msleep(20);
	}

	if (!pdo_set_success) {
		chg_err("set ufcs config failed because of wait ufcs_power_ready timeout.");
		return -1;
	}

	return 0;
}

static int oplus_chg_adsp_ufcs_get_dev_info(struct oplus_chg_ic_dev *ic_dev, u64 *dev_info)
{
	struct psy_state *pst = NULL;
	struct battery_chg_dev *bcdev;
	int rc1 = 0, rc2 = 0;
	u64 dev_info_l = 0;
	u64 dev_info_h = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc1 = read_property_id(bcdev, pst, USB_GET_DEV_INFO_L);
	rc2 = read_property_id(bcdev, pst, USB_GET_DEV_INFO_H);
	if (rc1 < 0 || rc2 < 0) {
		chg_err("get ufcs device info fail, rc1 = %d, rc2 = %d\n", rc1, rc2);
		return -1;
	}

	dev_info_l = pst->prop[USB_GET_DEV_INFO_L];
	dev_info_h = pst->prop[USB_GET_DEV_INFO_H];

	*dev_info = dev_info_l | (dev_info_h << 32);
	chg_err("dev_info_l = 0x%llx, dev_info_h = 0x%llx, *dev_info = 0x%llx\n", dev_info_l, dev_info_h, *dev_info);

	return 0;
}

#define UFCS_PDO_MAX 7
static int oplus_chg_adsp_ufcs_get_pdo_info_buffer(struct oplus_chg_ic_dev *ic_dev, u64 *pdo, int num)
{
	int pdo_index = 0;
	int pdo_max = num > UFCS_PDO_MAX ? UFCS_PDO_MAX : num;
	int pdo_num = 0;
	struct battery_chg_dev *bcdev;
	int retry_count = 12;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);

	while (retry_count--) {
		if (bcdev->ufcs_pdo_ready) {
			ufcs_read_buffer(bcdev);
			for (pdo_index = 0; pdo_index < pdo_max; pdo_index++) {
				pdo[pdo_index] = bcdev->ufcs_read_buffer_dump.data_buffer[pdo_index];
				if (pdo[pdo_index] == 0) {
					pdo_num = pdo_index;
					break;
				}
				chg_err("pdo[%d] = 0x%llu\n", pdo_index, pdo[pdo_index]);
			}
			bcdev->ufcs_pdo_ready = 0;
			break;
		}
		msleep(10);
	}
	return pdo_num;
}

static int oplus_chg_adsp_ufcs_get_src_info(struct oplus_chg_ic_dev *ic_dev, u64 *src_info)
{
	struct psy_state *pst = NULL;
	struct battery_chg_dev *bcdev;
	int rc1 = 0, rc2 = 0;
	u64 src_info_l = 0;
	u64 src_info_h = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc1 = read_property_id(bcdev, pst, USB_GET_SRC_INFO_L);
	rc2 = read_property_id(bcdev, pst, USB_GET_SRC_INFO_H);
	if (rc1 < 0 || rc2 < 0) {
		chg_err("get ufcs source info fail, rc1 = %d, rc2 = %d\n", rc1, rc2);
		return -1;
	}

	src_info_l = pst->prop[USB_GET_SRC_INFO_L];
	src_info_h = pst->prop[USB_GET_SRC_INFO_H];

	*src_info = src_info_l | (src_info_h << 32);
	chg_err("src_info_l = 0x%llx, src_info_h = 0x%llx, *src_info = 0x%llx\n", src_info_l, src_info_h, *src_info);

	return 0;
}

static int oplus_chg_adsp_ufcs_is_test_mode(struct oplus_chg_ic_dev *ic_dev, bool *en)
{
	struct battery_chg_dev *bcdev = g_bcdev;
	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	if (en == NULL) {
		chg_err("en is NULL");
		return -EINVAL;
	}

	*en = bcdev->ufcs_test_mode;
	chg_info("ufcs test mode is %d\n", bcdev->ufcs_test_mode);

	return 0;
}

static int oplus_chg_adsp_ufcs_is_vol_acc_test_mode(struct oplus_chg_ic_dev *ic_dev, bool *en)
{
	return 0;/*TODO*/
}

static int oplus_chg_adsp_ufcs_config_wd(struct oplus_chg_ic_dev *ic_dev, u16 time_ms)
{
	int rc = 0;
	/* TODO: */

	return rc;
}

static int oplus_chg_adsp_ufcs_running_state(struct oplus_chg_ic_dev *ic_dev, bool *state)
{
	int rc = 0;
	struct battery_chg_dev *bcdev;
	struct psy_state *pst = NULL;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	if (!bcdev) {
		chg_err("bcdev is NULL");
		return -ENODEV;
	}

	pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	rc = read_property_id(bcdev, pst, BATT_GET_UFCS_RUNNING_STATE);
	if (rc < 0) {
		chg_err("rc is %d read failed!", rc);
	} else {
		*state = pst->prop[BATT_GET_UFCS_RUNNING_STATE];
		rc = 0;
	}

	return rc;
}

#define OPLUS_UFCS_WAIT_EXIT_MAX_RETRY		30
static int oplus_chg_adsp_ufcs_exit_ufcs_mode(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;
	struct psy_state *pst = NULL;
	struct battery_chg_dev *bcdev;
	int exit = 1;
	bool state = true;
	int retry_count = 0; /* wait at most 600ms */

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	bcdev = oplus_chg_ic_get_drvdata(ic_dev);
	pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_SET_EXIT, exit);
	if (rc < 0) {
		chg_err("set ufcs config fail, rc= %d\n", rc);
		return -1;
	}

	if (bcdev->ufcs_run_check_support) {
		/* wait until the ufcs is realy exited to avoid DP/DM access conflict! */
		while (retry_count < OPLUS_UFCS_WAIT_EXIT_MAX_RETRY) {
			rc = oplus_chg_adsp_ufcs_running_state(ic_dev, &state);
			chg_info("retry_count = %d, state = %d, rc = %d", retry_count, state, rc);

			if ((rc < 0) || (state == false)) {
				chg_info("ufcs is exited now, not wait, retry_count %d\n", retry_count);
				break;
			}

			/* when the usb is not connected, no need to wait! */
			if (!bcdev->cid_status) {
				chg_info("usb unpluged, not retry.\n");
				break;
			}
			retry_count++;
			msleep(20);
		}
	}

	return rc;
}

static int  oplus_chg_adsp_ufcs_verify_adapter(struct oplus_chg_ic_dev *ic_dev, u8 key_index, u8 *auth_data, u8 data_len)
{
	return false;/*TODO*/
}

static int oplus_chg_adsp_ufcs_get_power_info_ext(struct oplus_chg_ic_dev *ic_dev, u64 *info, int num)
{
	return 0;/*TODO*/
}

static int oplus_chg_adsp_ufcs_get_emark_info(struct oplus_chg_ic_dev *ic_dev, u64 *info)
{
	return 0;/*TODO*/
}

static void *oplus_chg_adsp_ufcs_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}
	if (!oplus_chg_ic_func_is_support(ic_dev, func_id)) {
		chg_info("%s: this func(=%d) is not supported\n",  ic_dev->name, func_id);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
			oplus_chg_adsp_ufcs_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
			oplus_chg_adsp_ufcs_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
			oplus_chg_adsp_ufcs_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
			oplus_chg_adsp_ufcs_smt_test);
		break;
	case OPLUS_IC_FUNC_UFCS_HANDSHAKE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_HANDSHAKE,
			oplus_chg_adsp_ufcs_handshake);
		break;
	case OPLUS_IC_FUNC_UFCS_PDO_SET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_PDO_SET,
			oplus_chg_adsp_ufcs_pdo_set);
		break;
	case OPLUS_IC_FUNC_UFCS_GET_DEV_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_GET_DEV_INFO,
			oplus_chg_adsp_ufcs_get_dev_info);
		break;
	case OPLUS_IC_FUNC_UFCS_GET_PDO_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_GET_PDO_INFO,
			oplus_chg_adsp_ufcs_get_pdo_info_buffer);
		break;
	case OPLUS_IC_FUNC_UFCS_IS_TEST_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_IS_TEST_MODE,
			oplus_chg_adsp_ufcs_is_test_mode);
		break;
	case OPLUS_IC_FUNC_UFCS_IS_VOL_ACC_TEST_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_IS_VOL_ACC_TEST_MODE,
			oplus_chg_adsp_ufcs_is_vol_acc_test_mode);
		break;
	case OPLUS_IC_FUNC_UFCS_CONFIG_WD:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_CONFIG_WD,
			oplus_chg_adsp_ufcs_config_wd);
		break;
	case OPLUS_IC_FUNC_UFCS_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_EXIT,
			oplus_chg_adsp_ufcs_exit_ufcs_mode);
		break;
	case OPLUS_IC_FUNC_UFCS_VERIFY_ADAPTER:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_VERIFY_ADAPTER,
			oplus_chg_adsp_ufcs_verify_adapter);
		break;
	case OPLUS_IC_FUNC_UFCS_GET_POWER_INFO_EXT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_GET_POWER_INFO_EXT,
			oplus_chg_adsp_ufcs_get_power_info_ext);
		break;
	case OPLUS_IC_FUNC_UFCS_GET_EMARK_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_GET_EMARK_INFO,
			oplus_chg_adsp_ufcs_get_emark_info);
		break;
	case OPLUS_IC_FUNC_UFCS_GET_SRC_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_UFCS_GET_SRC_INFO,
			oplus_chg_adsp_ufcs_get_src_info);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq oplus_chg_adsp_ufcs_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
};

static int oplus_sm8350_ic_register(struct battery_chg_dev *bcdev)
{
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct device_node *child;
	struct oplus_chg_ic_dev *ic_dev = NULL;
	struct oplus_chg_ic_cfg ic_cfg;
	int rc;

	for_each_child_of_node(bcdev->dev->of_node, child) {
		rc = of_property_read_u32(child, "oplus,ic_type", &ic_type);
		if (rc < 0) {
			chg_err("can't get %s ic type, rc=%d\n", child->name, rc);
			continue;
		}
		rc = of_property_read_u32(child, "oplus,ic_index", &ic_index);
		if (rc < 0) {
			chg_err("can't get %s ic index, rc=%d\n", child->name, rc);
			continue;
		}
		ic_cfg.name = child->name;
		ic_cfg.index = ic_index;
		ic_cfg.type = ic_type;
		ic_cfg.of_node = child;
		switch (ic_type) {
		case OPLUS_CHG_IC_BUCK:
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "buck-sm8350");
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = oplus_chg_8350_buck_get_func;
			ic_cfg.virq_data = oplus_chg_8350_buck_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_8350_buck_virq_table);
			break;
		case OPLUS_CHG_IC_GAUGE:
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-adsp");
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = oplus_chg_8350_gauge_get_func;
			ic_cfg.virq_data = oplus_chg_8350_gauge_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_8350_gauge_virq_table);
			break;
		case OPLUS_CHG_IC_MISC:
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "misc-adsp_dpdm_switch");
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = oplus_chg_adsp_dpdm_switch_get_func;
			ic_cfg.virq_data = oplus_chg_adsp_dpdm_switch_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_adsp_dpdm_switch_virq_table);
			break;
		case OPLUS_CHG_IC_UFCS:
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "ufcs-adsp");
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = oplus_chg_adsp_ufcs_get_func;
			ic_cfg.virq_data = oplus_chg_adsp_ufcs_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(oplus_chg_adsp_ufcs_virq_table);
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_type);
			continue;
		}
		ic_dev = devm_oplus_chg_ic_register(bcdev->dev, &ic_cfg);
		if (!ic_dev) {
			rc = -ENODEV;
			chg_err("register %s error\n", child->name);
			continue;
		}
		chg_info("register %s\n", child->name);

		switch (ic_dev->type) {
		case OPLUS_CHG_IC_BUCK:
			bcdev->buck_ic = ic_dev;
			break;
		case OPLUS_CHG_IC_GAUGE:
			bcdev->gauge_ic = ic_dev;
			break;
		case OPLUS_CHG_IC_MISC:
			bcdev->misc_ic = ic_dev;
			oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_INIT);
			break;
		case OPLUS_CHG_IC_UFCS:
			bcdev->ufcs_ic = ic_dev;
			oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_INIT);
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_dev->type);
			continue;
		}

		of_platform_populate(child, NULL, NULL, bcdev->dev);
	}

	return 0;
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static int battery_chg_probe(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	const char *chg_name;
	int rc, i;

#ifdef OPLUS_FEATURE_CHG_BASIC
	chg_info("battery_chg_probe start...\n");
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	bcdev = devm_kzalloc(&pdev->dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

#ifdef OPLUS_FEATURE_CHG_BASIC
	g_bcdev = bcdev;
	bcdev->hvdcp_detect_time = 0;
	bcdev->hvdcp_detach_time = 0;
	bcdev->hvdcp_detect_ok = false;
	bcdev->hvdcp_disable = false;
	bcdev->voocphy_err_check = false;
	bcdev->usb_in_status = 0;
	bcdev->rerun_max = 3;
	bcdev->ufcs_test_mode = false;
	bcdev->ufcs_power_ready = false;
	bcdev->ufcs_handshake_ok = false;
	bcdev->bc12_completed = false;
	bcdev->read_by_reg = 0;
#endif

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;
	bcdev->psy_list[PSY_TYPE_WLS].map = wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(&pdev->dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model =
		devm_kzalloc(&pdev->dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	mutex_init(&bcdev->rw_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
	mutex_init(&bcdev->oplus_custom_gpio.pinctrl_mutex);
	mutex_init(&bcdev->read_buffer_lock);
	init_completion(&bcdev->oem_read_ack);
	mutex_init(&bcdev->bcc_read_buffer_lock);
	init_completion(&bcdev->bcc_read_ack);
	mutex_init(&bcdev->ufcs_read_buffer_lock);
	init_completion(&bcdev->ufcs_read_ack);
#endif
	init_completion(&bcdev->ack);
	init_completion(&bcdev->fw_buf_ack);
	init_completion(&bcdev->fw_update_ack);
	INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->adsp_voocphy_status_work, oplus_adsp_voocphy_status_func);
	INIT_DELAYED_WORK(&bcdev->unsuspend_usb_work, oplus_unsuspend_usb_work);
	INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func);
	INIT_DELAYED_WORK(&bcdev->cid_status_change_work, oplus_cid_status_change_work);
	INIT_DELAYED_WORK(&bcdev->adsp_crash_recover_work, oplus_adsp_crash_recover_func);
	INIT_DELAYED_WORK(&bcdev->voocphy_enable_check_work, oplus_voocphy_enable_check_func);
	INIT_DELAYED_WORK(&bcdev->otg_vbus_enable_work, otg_notification_handler);
	INIT_DELAYED_WORK(&bcdev->hvdcp_disable_work, oplus_hvdcp_disable_work);
	INIT_DELAYED_WORK(&bcdev->pd_only_check_work, oplus_pd_only_check_work);
	INIT_DELAYED_WORK(&bcdev->otg_status_check_work, oplus_otg_status_check_work);
	INIT_DELAYED_WORK(&bcdev->vbus_adc_enable_work, oplus_vbus_enable_adc_work);
	INIT_DELAYED_WORK(&bcdev->oem_lcm_en_check_work, oplus_oem_lcm_en_check_work);
	INIT_DELAYED_WORK(&bcdev->voocphy_err_work, oplus_voocphy_err_work);
	INIT_DELAYED_WORK(&bcdev->ctrl_lcm_frequency, oplus_chg_ctrl_lcm_work);
	INIT_DELAYED_WORK(&bcdev->plugin_irq_work, oplus_plugin_irq_work);
	INIT_DELAYED_WORK(&bcdev->recheck_input_current_work, oplus_recheck_input_current_work);
	INIT_DELAYED_WORK(&bcdev->qc_type_check_work, oplus_qc_type_check_work);
	INIT_DELAYED_WORK(&bcdev->vbus_collapse_rerun_icl_work, oplus_vbus_collapse_rerun_icl_work);
	INIT_DELAYED_WORK(&bcdev->ibus_collapse_rerun_aicl_work, oplus_ibus_collapse_rerun_aicl_work);
#endif
#ifdef OPLUS_FEATURE_CHG_BASIC
	INIT_DELAYED_WORK(&bcdev->vchg_trig_work, oplus_vchg_trig_work);
	INIT_DELAYED_WORK(&bcdev->mcu_en_init_work, oplus_chg_mcu_en_init_work);
#endif
	atomic_set(&bcdev->state, PMIC_GLINK_STATE_UP);
	bcdev->dev = dev;

	client_data.id = MSG_OWNER_BC;
	client_data.name = "battery_charger";
	client_data.msg_cb = battery_chg_callback;
	client_data.priv = bcdev;
	client_data.state_cb = battery_chg_state_cb;

	bcdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(bcdev->client)) {
		rc = PTR_ERR(bcdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n",
				rc);
		return rc;
	}

	bcdev->reboot_notifier.notifier_call = battery_chg_ship_mode;
	bcdev->reboot_notifier.priority = 255;
	register_reboot_notifier(&bcdev->reboot_notifier);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_ap_init_adsp_gague(bcdev);
#endif

	rc = battery_chg_parse_dt(bcdev);
	if (rc < 0)
		goto error;

	bcdev->restrict_fcc_ua = DEFAULT_RESTRICT_FCC_UA;
	platform_set_drvdata(pdev, bcdev);
	bcdev->fake_soc = -EINVAL;
#ifndef OPLUS_FEATURE_CHG_BASIC
	rc = battery_chg_init_psy(bcdev);
	if (rc < 0)
		goto error;
#endif

	bcdev->battery_class.name = "qcom-battery";
	bcdev->battery_class.class_groups = battery_class_groups;
	rc = class_register(&bcdev->battery_class);
	if (rc < 0) {
		chg_err("Failed to create battery_class rc=%d\n", rc);
		goto error;
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	bcdev->ssr_nb.notifier_call = oplus_chg_ssr_notifier_cb;
	bcdev->subsys_handle = qcom_register_ssr_notifier("lpass",
							  &bcdev->ssr_nb);
	if (IS_ERR(bcdev->subsys_handle)) {
		rc = PTR_ERR(bcdev->subsys_handle);
		pr_err("Failed in qcom_register_ssr_notifier rc=%d\n", rc);
	}

	oplus_usbtemp_iio_init(bcdev);
	oplus_subboard_temp_iio_init(bcdev);
	oplus_chg_parse_custom_dt(bcdev);
#ifdef OPLUS_CHG_UNDEF
	main_psy = power_supply_get_by_name("main");
	if (main_psy) {
		pval.intval = 1000 * oplus_chg_get_fv(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_VOLTAGE_MAX,
				&pval);
		pval.intval = 1000 * oplus_chg_get_charging_current(oplus_chip);
		power_supply_set_property(main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
				&pval);
	}
#endif
	/* oplus_chg_wake_update_work(); */

	if (oplus_vchg_trig_is_support() == true) {
		schedule_delayed_work(&bcdev->vchg_trig_work, msecs_to_jiffies(3000));
		oplus_vchg_trig_irq_register(bcdev);
	}
#endif /*OPLUS_FEATURE_CHG_BASIC*/

	battery_chg_add_debugfs(bcdev);
	battery_chg_notify_enable(bcdev);
	device_init_wakeup(bcdev->dev, true);
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_set_match_temp_to_voocphy();
	oplus_dwc3_config_usbphy_pfunc(&oplus_is_pd_svooc);
	oplus_voocphy_enable(bcdev, true);
	schedule_delayed_work(&bcdev->otg_init_work, 0);
	schedule_delayed_work(&bcdev->voocphy_enable_check_work,
			      round_jiffies_relative(msecs_to_jiffies(5000)));
	init_debug_reg_proc(bcdev);

	rc = of_property_read_u32(dev->of_node, "oplus,batt_num", &bcdev->batt_num);
	if (rc < 0) {
		chg_err("can't get oplus,batt_num, rc=%d\n", rc);
		bcdev->batt_num = 1;
	}

	bcdev->voocphy_bidirect_cp_support = of_property_read_bool(dev->of_node,
					"oplus,voocphy_bidirect_cp_support");
	chg_info("%s support voocphy bidirect cp\n",
		 bcdev->voocphy_bidirect_cp_support ? "" : "no");

	rc = of_property_read_string(dev->of_node, "oplus,chg_ops",  &chg_name);
	if (rc >= 0) {
		if (strncmp(chg_name, "plat-pmic", 64) == 0)
			bcdev->is_external_chg = false;
		else
			bcdev->is_external_chg = true;
	} else {
		chg_info("can't get oplus,chg_ops, rc=%d\n", rc);
		bcdev->is_external_chg = false;
	}

	rc = oplus_sm8350_ic_register(bcdev);
	if (rc < 0)
		goto error;
	schedule_delayed_work(&bcdev->mcu_en_init_work, 0);
	/* Adapter has no insertion interrupt circumvention scheme */
	schedule_delayed_work(&bcdev->plugin_irq_work, 0);

	chg_info("battery_chg_probe end...\n");
#endif
	return 0;
error:
	pmic_glink_unregister_client(bcdev->client);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	return rc;
}

static int battery_chg_remove(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = platform_get_drvdata(pdev);
	int rc;

	device_init_wakeup(bcdev->dev, false);
	debugfs_remove_recursive(bcdev->debugfs_dir);
	class_unregister(&bcdev->battery_class);
	unregister_reboot_notifier(&bcdev->reboot_notifier);
	rc = pmic_glink_unregister_client(bcdev->client);
	if (rc < 0) {
		chg_err("Error unregistering from pmic_glink, rc=%d\n", rc);
		return rc;
	}
	return 0;
}

#ifdef OPLUS_FEATURE_CHG_BASIC

static void battery_chg_shutdown(struct platform_device *pdev)
{
	struct battery_chg_dev *bcdev = g_bcdev;

	if (bcdev) {
		chg_err("disable voocphy");
		cancel_delayed_work_sync(&bcdev->voocphy_enable_check_work);
		cancel_delayed_work_sync(&bcdev->qc_type_check_work);
		oplus_typec_disable();
		oplus_voocphy_enable(bcdev, false);
	}

#ifdef OPLUS_CHG_UNDEF /* TODO */
	if (g_oplus_chip
		&& g_oplus_chip->chg_ops->charger_suspend
		&& g_oplus_chip->chg_ops->charger_unsuspend) {
		g_oplus_chip->chg_ops->charger_suspend();
		msleep(1000);
		g_oplus_chip->chg_ops->charger_unsuspend();
	}

	if (g_oplus_chip && g_oplus_chip->enable_shipmode) {
		smbchg_enter_shipmode(g_oplus_chip);
		msleep(1000);
	}
	if (!is_ext_chg_ops()) {
		bcdev->oem_misc_ctl_data = 0;
		bcdev->oem_misc_ctl_data |= OEM_MISC_CTL_DATA_PAIR(OEM_MISC_CTL_CMD_LCM_25K, false);
		oplus_oem_misc_ctl();
	}
#endif
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

static const struct of_device_id battery_chg_match_table[] = {
	{ .compatible = "oplus,hal_sm8450" },
	{},
};

static struct platform_driver battery_chg_driver = {
	.driver = {
		.name = "qti_battery_charger",
		.of_match_table = battery_chg_match_table,
#ifdef OPLUS_FEATURE_CHG_BASIC
		.pm	= &battery_chg_pm_ops,
#endif
	},
	.probe = battery_chg_probe,
	.remove = battery_chg_remove,
#ifdef OPLUS_FEATURE_CHG_BASIC
	.shutdown = battery_chg_shutdown,
#endif
};

#ifdef OPLUS_FEATURE_CHG_BASIC
static int __init sm8350_chg_init(void)
{
	int ret;

	ret = platform_driver_register(&battery_chg_driver);
	return ret;
}

static void __exit sm8350_chg_exit(void)
{
	platform_driver_unregister(&battery_chg_driver);
}

oplus_chg_module_register(sm8350_chg);
#else
module_platform_driver(battery_chg_driver);
#endif

MODULE_DESCRIPTION("QTI Glink battery charger driver");
MODULE_LICENSE("GPL v2");
