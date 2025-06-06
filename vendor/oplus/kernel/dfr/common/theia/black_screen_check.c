// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include "powerkey_monitor.h"
#include "theia_kevent_kernel.h"

#define BLACK_MAX_WRITE_NUMBER            50
#define BLACK_SLOW_STATUS_TIMEOUT_MS    20000
#define BLACK_ERROR_RECOVERY_MS        200000
#define PROC_BLACK_SWITCH "blackSwitch"
#include "theia_trace.h"

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
static struct delayed_work g_check_dt_work;
static int g_check_dt_retry_count;
#define CHECK_DT_DELAY_MS 5000
#endif

#define BLACK_DEBUG_PRINTK(a, arg...)\
	do {\
		printk("[black_screen_check]: " a, ##arg);\
	} while (0)

struct pwrkey_monitor_data g_black_data = {
	.is_panic = 0,
	.status = BLACK_STATUS_INIT,
	.blank = THEIA_PANEL_BLANK_VALUE,
	.timeout_ms = BLACK_SLOW_STATUS_TIMEOUT_MS,
	.get_log = 1,
	.error_count = 0,
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	.is_fold_dev = false,
	.active_panel = NULL,
	.active_panel_second = NULL,
	.cookie = NULL,
	.cookie_second = NULL,
#endif
};

/* if last stage in this array, skip */
static char black_last_skip_block_stages[][64] = {
	{ "LIGHT_setScreenState_" }, /* quick press powerkey, power decide wakeup when black check, skip */
	{ "POWERKEY_interceptKeyBeforeQueueing" }, /* don't wakeup in case heycast/powerlight */
};

/* if contain stage in this array, skip */
static char black_skip_stages[][64] = {
	{ "CANCELED_" }, /* if CANCELED_ event write in black check stage, skip */
};

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER) || IS_ENABLED(CONFIG_OPLUS_MTK_DRM_SUB_NOTIFY)
/* if contain stage in this array, skip */
static char black_skip_stages_fold[][64] = {
	{ "POWER_wakeUpInternal" }, /* don't set screenstate in case aod alm: 7925370*/
};
#endif

static int bl_start_check_systemid = -1;

extern bool error_flag;
int black_screen_timer_restart(void)
{
	BLACK_DEBUG_PRINTK("%s enter: blank = %d, status = %d\n", __func__, g_black_data.blank, g_black_data.status);

	if (g_black_data.status != BLACK_STATUS_CHECK_ENABLE && g_black_data.status != BLACK_STATUS_CHECK_DEBUG) {
		BLACK_DEBUG_PRINTK("%s unsupported status, return, status = %d\n", __func__, g_black_data.status);
		return g_black_data.status;
	}

	/* Remove for MTK functioning */
	if (!is_system_boot_completed()) {
		BLACK_DEBUG_PRINTK("boot not complete, %s just return\n", __func__);
		return -1;
	}

	if ((g_black_data.blank == THEIA_PANEL_BLANK_VALUE) && !error_flag) {
		bl_start_check_systemid = get_systemserver_pid();
		mod_timer(&g_black_data.timer, jiffies + msecs_to_jiffies(g_black_data.timeout_ms));
		BLACK_DEBUG_PRINTK("%s: BL check start, timeout = %u\n", __func__, g_black_data.timeout_ms);
		theia_pwk_stage_start("POWERKEY_START_BL");
		return 0;
	}
	return g_black_data.blank;
}
EXPORT_SYMBOL_GPL(black_screen_timer_restart);

/*
logmap format:
logmap{key1:value1;key2:value2;key3:value3 ...}
*/
static void get_blackscreen_check_dcs_logmap(char *logmap)
{
	char stages[512] = {0};
	int stages_len;

	stages_len = get_pwkey_stages(stages);
	snprintf(logmap, 512, "logmap{logType:%s;error_id:%s;error_count:%u;systemserver_pid:%d;stages:%s}",
		PWKKEY_BLACK_SCREEN_DCS_LOGTYPE, g_black_data.error_id, g_black_data.error_count,
		get_systemserver_pid(), stages);
}

extern u64 mLastPwkTime;
extern u64 FrequencyInterval;
void send_black_screen_dcs_msg(void)
{
	char logmap[512] = {0};
	char stages[512] = {0};

	u64 ts = ktime_to_ms(ktime_get());
	if (mLastPwkTime > 0 && (ts - mLastPwkTime) < FrequencyInterval) {
		BLACK_DEBUG_PRINTK("send_pwk_event too frequency, must wait %lld ms\n", (FrequencyInterval + mLastPwkTime - ts));
		return;
	}
	mLastPwkTime = ts;
	BLACK_DEBUG_PRINTK("send_black_screen_dcs_msg mLastPwkTime is %lld ms\n", mLastPwkTime);
	get_blackscreen_check_dcs_logmap(logmap);
	/* remove theia event in os15
	theia_send_event(THEIA_EVENT_PWK_LIGHT_UP_MONITOR, THEIA_LOGINFO_SYSTEM_SERVER_TRACES
		 | THEIA_LOGINFO_EVENTS_LOG | THEIA_LOGINFO_KERNEL_LOG | THEIA_LOGINFO_ANDROID_LOG
		 | THEIA_LOGINFO_DUMPSYS_SF | THEIA_LOGINFO_DUMPSYS_POWER,
		get_systemserver_pid(), logmap);
	*/
	get_pwkey_stages(stages);
	trace_black_screen_monitor(get_timestamp_ms(), SYSTEM_ID, PWKKEY_DCS_TAG, PWKKEY_DCS_EVENTID, PWKKEY_BLACK_SCREEN_DCS_LOGTYPE, g_black_data.error_id,
			g_black_data.error_count, get_systemserver_pid(), stages);
}

static void delete_timer_black(char *reason, bool cancel)
{
	del_timer(&g_bright_data.timer);
	del_timer(&g_black_data.timer);

	if (cancel && g_black_data.error_count != 0) {
		g_black_data.error_count = 0;
		sprintf(g_black_data.error_id, "%s", "null");
	}

	theia_pwk_stage_end(reason);
}

static int black_screen_cancel_proc_show(struct seq_file *seq_file, void *data)
{
	seq_printf(seq_file, "%s called\n", __func__);
	return 0;
}

static int black_screen_cancel_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, black_screen_cancel_proc_show, NULL);
}

static ssize_t black_screen_cancel_proc_write(struct file *file, const char __user *buf,
	size_t count, loff_t *off)
{
	char buffer[40] = {0};
	char cancel_str[64] = {0};

	if (g_black_data.status == BLACK_STATUS_INIT || g_black_data.status == BLACK_STATUS_INIT_FAIL) {
		BLACK_DEBUG_PRINTK("%s init not finish: status = %d\n", __func__, g_black_data.status);
		return count;
	}

	if (count >= 40)
		count = 39;

	if (copy_from_user(buffer, buf, count)) {
		BLACK_DEBUG_PRINTK("%s: read proc input error.\n", __func__);
		return count;
	}

	snprintf(cancel_str, sizeof(cancel_str), "CANCELED_BL_%s", buffer);
	delete_timer_black(cancel_str, true);

	return count;
}

static ssize_t black_screen_cancel_proc_read(struct file *file, char __user *buf,
	size_t count, loff_t *off)
{
	return 0;
}

static const struct proc_ops black_screen_cancel_proc_fops = {
	.proc_open = black_screen_cancel_proc_open,
	.proc_read = black_screen_cancel_proc_read,
	.proc_write = black_screen_cancel_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void dump_freeze_log(void)
{
	send_black_screen_dcs_msg();
}

static bool is_black_last_stage_skip(void)
{
	int i = 0, nLen;
	char stage[64] = {0};;
	get_last_pwkey_stage(stage);

	nLen = ARRAY_SIZE(black_last_skip_block_stages);

	for (i = 0; i < nLen; i++) {
		if (strstr(stage, black_last_skip_block_stages[i]) != NULL) {
			BLACK_DEBUG_PRINTK("is_black_last_stage_skip return true, stage:%s", stage);
			return true;
		}
	}

	return false;
}

static bool is_black_contain_skip_stage(void)
{
	char stages[512] = {0};
	int i = 0, nArrayLen;
	get_pwkey_stages(stages);

	nArrayLen = ARRAY_SIZE(black_skip_stages);
	for (i = 0; i < nArrayLen; i++) {
		if (strstr(stages, black_skip_stages[i]) != NULL) {
			BLACK_DEBUG_PRINTK("is_black_contain_skip_stage return true, stages:%s", stages);
			return true;
		}
	}

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (g_black_data.is_fold_dev) {
		for (i = 0; i < nArrayLen; i++) {
			if (strstr(stages, black_skip_stages_fold[i]) != NULL) {
				BLACK_DEBUG_PRINTK("is_black_contain_skip_stage return true case folding-device in qcom, stages:%s", stages);
				return true;
			}
		}
	}
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_SUB_NOTIFY)
	for (i = 0; i < nArrayLen; i++) {
		if (strstr(stages, black_skip_stages_fold[i]) != NULL) {
			BLACK_DEBUG_PRINTK("is_black_contain_skip_stage return true case folding-device in mtk, stages:%s", stages);
			return true;
		}
	}
#endif

	return false;
}

static bool is_need_skip(void)
{
	if (is_black_last_stage_skip())
		return true;

	if (is_black_contain_skip_stage())
		return true;
	if (is_slowkernel_skip())
		return true;

	return false;
}

static void black_error_happen_work(struct work_struct *work)
{
	struct pwrkey_monitor_data *bla_data = container_of(work, struct pwrkey_monitor_data, error_happen_work);
	struct timespec64 ts;

	/* for black screen check, check if need skip, we direct return */
	if (is_need_skip()) {
		error_flag = false;
		return;
	}

	if (bla_data->error_count == 0) {
		ktime_get_real_ts64(&ts);
		sprintf(g_black_data.error_id, "%d.%lld.%ld", get_systemserver_pid(), ts.tv_sec, ts.tv_nsec);
	}

	if (bla_data->error_count < BLACK_MAX_WRITE_NUMBER) {
		bla_data->error_count++;
		dump_freeze_log();
	}

	if(bla_data->is_panic) {
		mod_timer(&g_recovery_data.timer, jiffies + msecs_to_jiffies(BLACK_ERROR_RECOVERY_MS));
		BLACK_DEBUG_PRINTK("black_error_happen_work: mod_timer g_recovery_data start!\n");
	}

	BLACK_DEBUG_PRINTK("black_error_happen_work error_id = %s, error_count = %d\n",
		bla_data->error_id, bla_data->error_count);

	set_timer_started(true);
	error_flag = false;
	delete_timer_black("BL_SCREEN_ERROR_HAPPEN", false);
}

static void black_timer_func(struct timer_list *t)
{
	struct pwrkey_monitor_data *p = from_timer(p, t, timer);

	BLACK_DEBUG_PRINTK("black_timer_func is called\n");

	/* stop recored stage when happen work for alm:6864732 */
	set_timer_started(false);
	error_flag = true;
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (g_black_data.active_panel == NULL || g_black_data.cookie == NULL) {
		BLACK_DEBUG_PRINTK("bl check register panel not ready\n");
		error_flag = false;
		return;
	}
#endif

	if (bl_start_check_systemid == get_systemserver_pid())
		schedule_work(&p->error_happen_work);
	else {
		error_flag = false;
		BLACK_DEBUG_PRINTK("black_timer_func, not valid for check, skip\n");
	}
}

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
static void black_fb_notifier_callback(enum panel_event_notifier_tag tag,
	struct panel_event_notification *notification, void *client_data)
{
	if (!notification) {
		BLACK_DEBUG_PRINTK("black_fb_notifier_callback, invalid notify\n");
		return;
	}

	switch (notification->notif_type) {
	case THEIA_PANEL_BLANK_EVENT:
		g_black_data.blank = THEIA_PANEL_BLANK_VALUE;
		break;
	case THEIA_PANEL_UNBLANK_EVENT:
		g_black_data.blank = THEIA_PANEL_UNBLANK_VALUE;
		if (g_black_data.status != BLACK_STATUS_CHECK_DEBUG) {
			delete_timer_black("FINISH_FB", true);
			del_timer(&g_recovery_data.timer);
			BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del_timer g_recovery_data del in qcom\n");
			BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer, status:%d, blank:%d\n",
				g_black_data.status, g_black_data.blank);
		} else {
			BLACK_DEBUG_PRINTK("black_fb_notifier_callback debug: status:%d, blank:%d\n",
				g_black_data.status, g_black_data.blank);
		}
		break;
	default:
		break;
	}
}
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
static int black_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	switch (event) {
	case THEIA_PANEL_BLANK_EVENT:
		g_black_data.blank = *(int *)data;
		if (g_black_data.status != BLACK_STATUS_CHECK_DEBUG) {
			if (g_black_data.blank == THEIA_PANEL_UNBLANK_VALUE) {
				delete_timer_black("FINISH_FB", true);
				del_timer(&g_recovery_data.timer);
				BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del_timer g_recovery_data del in mtk\n");
				BLACK_DEBUG_PRINTK("black_fb_notifier_callback: del timer, status:%d, blank:%d\n",
					g_black_data.status, g_black_data.blank);
			}
		} else {
			BLACK_DEBUG_PRINTK("black_fb_notifier_callback debug: status:%d, blank:%d\n",
				g_black_data.status, g_black_data.blank);
		}
		break;
	default:
		break;
	}

	return 0;
}
#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_SUB_NOTIFY)
static int black_fb_notifier_sub_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	switch (event) {
	case THEIA_PANEL_BLANK_EVENT:
		g_black_data.blank = *(int *)data;
		if (g_black_data.status != BLACK_STATUS_CHECK_DEBUG) {
			if (g_black_data.blank == THEIA_PANEL_UNBLANK_VALUE) {
				delete_timer_black("FINISH_FB", true);
				del_timer(&g_recovery_data.timer);
				BLACK_DEBUG_PRINTK("black_fb_notifier_sub_callback: del_timer g_recovery_data del in mtk\n");
				BLACK_DEBUG_PRINTK("black_fb_notifier_sub_callback: del timer, status:%d, blank:%d\n",
					g_black_data.status, g_black_data.blank);
			}
		} else {
			BLACK_DEBUG_PRINTK("black_fb_notifier_sub_callback debug: status:%d, blank:%d\n",
				g_black_data.status, g_black_data.blank);
		}
		break;
	default:
		break;
	}

	return 0;
}
#endif
#endif

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
static int bl_register_panel_event_notify(void)
{
	void *data = NULL;
	void *cookie = NULL;

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
				PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_THEIA_BLACK,
				g_black_data.active_panel, black_fb_notifier_callback, data);

	if (!cookie) {
		BLACK_DEBUG_PRINTK("bl_register_panel_event_notify failed\n");
		return -1;
	}
	g_black_data.cookie = cookie;
	return 0;
}

static int bl_register_panel_second_event_notify(void)
{
	void *data = NULL;
	void *cookie = NULL;

	cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_SECONDARY,
				PANEL_EVENT_NOTIFIER_CLIENT_SECONDARY_THEIA_BLACK,
				g_black_data.active_panel_second, black_fb_notifier_callback, data);

	if (!cookie) {
		BLACK_DEBUG_PRINTK("bl_register_panel_event_notify failed\n");
		return -1;
	}
	g_black_data.cookie_second = cookie;
	return 0;
}

static struct drm_panel *theia_check_panel_dt(void)
{
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	struct device_node *np = NULL;

	np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
	if (!np) {
		BLACK_DEBUG_PRINTK("Device tree info missing.\n");
		goto fail;
	} else {
		BLACK_DEBUG_PRINTK("Device tree info found.\n");
	}

	/* for furture mliti-panel extend, need to add code for other panel parse and register */
	count = of_count_phandle_with_args(np, "oplus,dsi-panel-primary", NULL);
	BLACK_DEBUG_PRINTK("Device tree oplus,dsi-panel-primary count = %d.\n", count);
	if (count <= 0)
		goto fail;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,dsi-panel-primary", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			BLACK_DEBUG_PRINTK("Found active panel.\n");
			break;
		}
	}

	if (IS_ERR(panel))
		panel = NULL;

fail:
	return panel;
}

static struct drm_panel *theia_check_panel_second_dt(void)
{
	int i;
	int count;
	struct device_node *node = NULL;
	struct drm_panel *panel = NULL;
	struct device_node *np = NULL;

	np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
	if (!np) {
		BLACK_DEBUG_PRINTK("Device tree info missing.\n");
		goto fail;
	} else {
		BLACK_DEBUG_PRINTK("Device tree info found.\n");
	}

	/* for furture mliti-panel extend, need to add code for other panel parse and register */
	count = of_count_phandle_with_args(np, "oplus,dsi-panel-secondary", NULL);
	BLACK_DEBUG_PRINTK("Device tree oplus,dsi-panel-secondary count = %d.\n", count);
	if (count <= 0)
		goto fail;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "oplus,dsi-panel-secondary", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			BLACK_DEBUG_PRINTK("Found active secondary panel.\n");
			break;
		}
	}

	if (IS_ERR(panel))
		panel = NULL;

fail:
	return panel;
}

static struct drm_panel *theia_get_active_panel(void)
{
	return theia_check_panel_dt();
}

static int register_panel_event(void)
{
	int ret = -1;
	struct drm_panel *panel = NULL;
	struct drm_panel *panel_second = NULL;

	panel = theia_get_active_panel();
	if (panel) {
		g_black_data.active_panel = panel;
		g_bright_data.active_panel = panel;
		ret = bl_register_panel_event_notify();
		ret |= br_register_panel_event_notify();
	} else {
		BLACK_DEBUG_PRINTK("theia_check_panel_dt failed, get no active panel\n");
	}

	if (g_black_data.is_fold_dev) {
		panel_second = theia_check_panel_second_dt();
		if (panel_second) {
			g_black_data.active_panel_second = panel_second;
			g_bright_data.active_panel_second = panel_second;
			ret = bl_register_panel_second_event_notify();
			ret |= br_register_panel_second_event_notify();
		} else {
			BLACK_DEBUG_PRINTK("theia_check_panel_dt failed, get no active panel_second\n");
		}
	}

	return ret;
}

static void check_dt_work_func(struct work_struct *work)
{
	if (register_panel_event()) {
		BLACK_DEBUG_PRINTK("register_panel_event failed, retry, retry_count = %d\n", g_check_dt_retry_count);
		if (g_check_dt_retry_count) {
			schedule_delayed_work(&g_check_dt_work, msecs_to_jiffies(CHECK_DT_DELAY_MS));
			g_check_dt_retry_count--;
		} else {
			g_black_data.status = BLACK_STATUS_INIT_FAIL;
			g_bright_data.status = BRIGHT_STATUS_INIT_FAIL;
			BLACK_DEBUG_PRINTK("register_panel_event failed, the pwrkey monitor function disabled\n");
		}
	}
}
#endif

void black_screen_check_init(void)
{
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
        struct device_node *np = NULL;
#endif
        g_black_data.status = BLACK_STATUS_INIT;

#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	np = of_find_node_by_name(NULL, "ssc_interactive");
	if (!np) {
		pr_err("ssc_interactive dts info missing.\n");
	} else {
		if (of_property_read_bool(np, "is-folding-device")) {
			g_black_data.is_fold_dev = true;
			BLACK_DEBUG_PRINTK("supported fold device");
		} else {
			g_black_data.is_fold_dev = false;
			BLACK_DEBUG_PRINTK("unsupported fold device");
		}
	}

	g_black_data.active_panel = NULL;
	g_black_data.cookie = NULL;
	g_check_dt_retry_count = 2;
	INIT_DELAYED_WORK(&g_check_dt_work, check_dt_work_func);
	schedule_delayed_work(&g_check_dt_work, msecs_to_jiffies(CHECK_DT_DELAY_MS));
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	g_black_data.fb_notif.notifier_call = black_fb_notifier_callback;
	if (mtk_disp_notifier_register("oplus_theia", &g_black_data.fb_notif)) {
		g_black_data.status = BLACK_STATUS_INIT_FAIL;
		BLACK_DEBUG_PRINTK("black_screen_check_init, register fb notifier fail\n");
		return;
	}
#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_SUB_NOTIFY)
	g_black_data.fb_notif_sub.notifier_call = black_fb_notifier_sub_callback;
	if (mtk_disp_sub_notifier_register("oplus_theia_sub", &g_black_data.fb_notif_sub)) {
		g_black_data.status = BLACK_STATUS_INIT_FAIL;
		BLACK_DEBUG_PRINTK("black_screen_check_init, register sub fb notifier fail\n");
		return;
	}
#endif
#endif
	sprintf(g_black_data.error_id, "%s", "null");

	/* the node for cancel black screen check */
	if (proc_create(PROC_BLACK_SWITCH, S_IRWXUGO, NULL, &black_screen_cancel_proc_fops) == NULL)
		BLACK_DEBUG_PRINTK("brightSwitch proc node create failed\n");

	INIT_WORK(&g_black_data.error_happen_work, black_error_happen_work);
	timer_setup((&g_black_data.timer), (black_timer_func), TIMER_DEFERRABLE);
	g_black_data.status = BLACK_STATUS_CHECK_ENABLE;
}

void black_screen_exit(void)
{
	BLACK_DEBUG_PRINTK("%s called\n", __func__);

	remove_proc_entry(PROC_BLACK_SWITCH, NULL);
	delete_timer_black("FINISH_DRIVER_EXIT", true);
	cancel_work_sync(&g_black_data.error_happen_work);
#if IS_ENABLED(CONFIG_DRM_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	cancel_delayed_work_sync(&g_check_dt_work);
	if (g_black_data.active_panel && g_black_data.cookie)
		panel_event_notifier_unregister(g_black_data.cookie);
	if (g_black_data.active_panel_second && g_black_data.cookie_second)
		panel_event_notifier_unregister(g_black_data.cookie_second);
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	mtk_disp_notifier_unregister(&g_black_data.fb_notif);
#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_SUB_NOTIFY)
	mtk_disp_sub_notifier_unregister(&g_black_data.fb_notif_sub);
#endif
#endif
}
