// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <cam_sensor_cmn_header.h>
#include <cam_sensor_util.h>
#include <cam_sensor_io.h>
#include <cam_req_mgr_util.h>
#include "cam_actuator_soc.h"
#include "cam_soc_util.h"

int32_t cam_actuator_parse_dt(struct cam_actuator_ctrl_t *a_ctrl,
	struct device *dev)
{
	int32_t                         i, rc = 0;
	struct cam_hw_soc_info          *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private *soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t  *power_info = &soc_private->power_info;
	struct device_node              *of_node = NULL;
	struct device_node              *of_parent = NULL;

#ifdef OPLUS_FEATURE_CAMERA_COMMON
	uint32_t                        reactive_setting_data[6];
	uint32_t                        reactive_setting_size;
	const char                      *p = NULL;
	int32_t                         name_length = 0;
#endif

	/* Initialize mutex */
	mutex_init(&(a_ctrl->actuator_mutex));
#ifdef OPLUS_FEATURE_CAMERA_COMMON
	mutex_init(&(a_ctrl->actuator_ioctl_mutex));
	sema_init(&a_ctrl->actuator_sem, 1);
#endif

	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "parsing common soc dt(rc %d)", rc);
		return rc;
	}

	of_node = soc_info->dev->of_node;

	rc = of_property_read_bool(of_node, "i3c-target");
	if (rc) {
		a_ctrl->is_i3c_device = true;
		a_ctrl->io_master_info.master_type = I3C_MASTER;
	}

	CAM_DBG(CAM_SENSOR, "I3C Target: %s", CAM_BOOL_TO_YESNO(a_ctrl->is_i3c_device));

#ifdef OPLUS_FEATURE_CAMERA_COMMON
	rc = of_property_read_bool(of_node, "is_update_pid");
	if (rc) {
		a_ctrl->is_update_pid = true;
		CAM_INFO(CAM_ACTUATOR, "read is_update_pid success, value:%d", a_ctrl->is_update_pid);
	} else {
		a_ctrl->is_update_pid = false;
		CAM_INFO(CAM_ACTUATOR, "get is_update_pid failed rc:%d, default %d", rc, a_ctrl->is_update_pid);
	}
	rc = of_property_read_string_index(of_node, "actuator,name", 0, (const char **)&p);
	if (rc) {
		CAM_ERR(CAM_OIS, "get actuator,name failed rc:%d", rc);
	} else {
		memcpy(a_ctrl->actuator_name, p, sizeof(a_ctrl->actuator_name));
		CAM_ERR(CAM_OIS, "read actuator,name success, value:%s", a_ctrl->actuator_name);
	}
#endif

	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		rc = of_property_read_u32(of_node, "cci-master",
			&(a_ctrl->cci_i2c_master));
		CAM_DBG(CAM_ACTUATOR, "cci-master %d, rc %d",
			a_ctrl->cci_i2c_master, rc);
		if ((rc < 0) || (a_ctrl->cci_i2c_master >= MASTER_MAX)) {
			CAM_ERR(CAM_ACTUATOR,
				"Wrong info: rc: %d, dt CCI master:%d",
				rc, a_ctrl->cci_i2c_master);
			rc = -EFAULT;
			return rc;
		}

		of_parent = of_get_parent(of_node);
		if (of_property_read_u32(of_parent, "cell-index",
				&a_ctrl->cci_num) < 0)
			/* Set default master 0 */
			a_ctrl->cci_num = CCI_DEVICE_0;
		a_ctrl->io_master_info.cci_client->cci_device = a_ctrl->cci_num;
		CAM_DBG(CAM_ACTUATOR, "cci-device %d", a_ctrl->cci_num);
	}

#ifdef OPLUS_FEATURE_CAMERA_COMMON
	rc = of_property_read_u32(of_node, "is_af_parklens", &a_ctrl->is_af_parklens);
	if (rc)
	{
		a_ctrl->is_af_parklens = 0;
		CAM_INFO(CAM_ACTUATOR, "get failed for is_af_parklens = %d",a_ctrl->is_af_parklens);
	}
	else
	{
		CAM_INFO(CAM_ACTUATOR, "read is_af_parklens success, value:%d", a_ctrl->is_af_parklens);
	}

	if (!of_property_read_bool(of_node, "reactive-ctrl-support")) {
		a_ctrl->reactive_ctrl_support = false;
		CAM_DBG(CAM_ACTUATOR, "No reactive control parameter defined");
	} else {
		reactive_setting_size = of_property_count_u32_elems(of_node, "reactive-reg-setting");

		if (reactive_setting_size != 6) {
			a_ctrl->reactive_ctrl_support = false;
			CAM_ERR(CAM_ACTUATOR, "reactive control parameter config err!");
		} else {
			rc = of_property_read_u32_array(of_node, "reactive-reg-setting",
					reactive_setting_data, reactive_setting_size);

			a_ctrl->reactive_reg_array.reg_addr  = reactive_setting_data[0];
			a_ctrl->reactive_setting.addr_type   = reactive_setting_data[1];
			a_ctrl->reactive_reg_array.reg_data  = reactive_setting_data[2];
			a_ctrl->reactive_setting.data_type   = reactive_setting_data[3];
			a_ctrl->reactive_reg_array.delay     = reactive_setting_data[4];
			a_ctrl->reactive_reg_array.data_mask = reactive_setting_data[5];
			a_ctrl->reactive_setting.reg_setting = &(a_ctrl->reactive_reg_array);
			a_ctrl->reactive_setting.size        = 1;
			a_ctrl->reactive_setting.delay       = 0;
			a_ctrl->reactive_ctrl_support        = true;

			CAM_INFO(CAM_ACTUATOR,
				"reactive control support %d, reactive [0x%x %d 0x%x %d %d 0x%x]",
				a_ctrl->reactive_ctrl_support,
				a_ctrl->reactive_setting.reg_setting->reg_addr,
				a_ctrl->reactive_setting.addr_type,
				a_ctrl->reactive_setting.reg_setting->reg_data,
				a_ctrl->reactive_setting.data_type,
				a_ctrl->reactive_setting.reg_setting->delay,
				a_ctrl->reactive_setting.reg_setting->data_mask);
		}
	}

	rc = of_property_read_string_index(of_node, "actuator,name", 0, (const char **)&p);
	if (rc) {
		CAM_ERR(CAM_OIS, "get actuator,name failed rc:%d", rc);
	} else {
		name_length = (strlen(p) > sizeof(a_ctrl->actuator_name)-1) ? sizeof(a_ctrl->actuator_name)-1 : strlen(p);
		memcpy(a_ctrl->actuator_name, p, name_length);
		CAM_INFO(CAM_ACTUATOR, "read actuator,name success, value:%s, name_length: %d", a_ctrl->actuator_name, name_length);
	}
#endif
	/* Initialize regulators to default parameters */
	for (i = 0; i < soc_info->num_rgltr; i++) {
		soc_info->rgltr[i] = devm_regulator_get(soc_info->dev,
					soc_info->rgltr_name[i]);
		if (IS_ERR_OR_NULL(soc_info->rgltr[i])) {
			rc = PTR_ERR(soc_info->rgltr[i]);
			rc = rc ? rc : -EINVAL;
			CAM_ERR(CAM_ACTUATOR, "get failed for regulator %s %d",
				 soc_info->rgltr_name[i], rc);
			return rc;
		}
		CAM_DBG(CAM_ACTUATOR, "get for regulator %s",
			soc_info->rgltr_name[i]);
	}
	if (!soc_info->gpio_data) {
		CAM_DBG(CAM_ACTUATOR, "No GPIO found");
		rc = 0;
		return rc;
	}

	if (!soc_info->gpio_data->cam_gpio_common_tbl_size) {
		CAM_DBG(CAM_ACTUATOR, "No GPIO found");
		return -EINVAL;
	}

	rc = cam_sensor_util_init_gpio_pin_tbl(soc_info,
		&power_info->gpio_num_info);
	if ((rc < 0) || (!power_info->gpio_num_info)) {
		CAM_ERR(CAM_ACTUATOR, "No/Error Actuator GPIOs");
		return -EINVAL;
	}
	return rc;
}
