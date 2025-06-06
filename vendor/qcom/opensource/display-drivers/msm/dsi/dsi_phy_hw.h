/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DSI_PHY_HW_H_
#define _DSI_PHY_HW_H_

#include "dsi_defs.h"
#include "dsi_hw.h"

#define DSI_MAX_SETTINGS 8
#define DSI_PHY_TIMING_V3_SIZE 12
#define DSI_PHY_TIMING_V4_SIZE 14

#define DSI_PHY_DBG(p, fmt, ...)	DRM_DEV_DEBUG(NULL, "[msm-dsi-debug]: DSI_%d: "\
		fmt, p ? p->index : -1, ##__VA_ARGS__)
#define DSI_PHY_ERR(p, fmt, ...)	DRM_DEV_ERROR(NULL, "[msm-dsi-error]: DSI_%d: "\
		fmt, p ? p->index : -1, ##__VA_ARGS__)
#define DSI_PHY_INFO(p, fmt, ...)	DRM_DEV_INFO(NULL, "[msm-dsi-info]: DSI_%d: "\
		fmt, p ? p->index : -1, ##__VA_ARGS__)
#define DSI_PHY_WARN(p, fmt, ...)	DRM_WARN("[msm-dsi-warn]: DSI_%d: " fmt,\
		p ? p->index : -1, ##__VA_ARGS__)

#define DSI_MISC_R32(dsi_phy_hw, off) DSI_GEN_R32((dsi_phy_hw)->phy_clamp_base, off)
#define DSI_MISC_W32(dsi_phy_hw, off, val) \
	DSI_GEN_W32_DEBUG((dsi_phy_hw)->phy_clamp_base, (dsi_phy_hw)->index, off, val)

#ifdef OPLUS_FEATURE_DISPLAY
#undef DSI_PHY_ERR
#include <soc/oplus/system/oplus_mm_kevent_fb.h>
#define DSI_PHY_ERR(p, fmt, ...) \
	do { \
		DRM_DEV_ERROR(NULL, "[msm-dsi-error]: DSI_%d: "\
				fmt, p ? p->index : -1, ##__VA_ARGS__); \
		mm_fb_display_kevent_named(MM_FB_KEY_RATELIMIT_1H, fmt, ##__VA_ARGS__); \
	} while(0)
#endif /* OPLUS_FEATURE_DISPLAY */

/**
 * enum dsi_phy_version - DSI PHY version enumeration
 * @DSI_PHY_VERSION_UNKNOWN:    Unknown version.
 * @DSI_PHY_VERSION_3_0:        10nm
 * @DSI_PHY_VERSION_4_0:        7nm
 * @DSI_PHY_VERSION_4_1:	7nm
 * @DSI_PHY_VERSION_4_2:        5nm
 * @DSI_PHY_VERSION_4_3:        5nm
 * @DSI_PHY_VERSION_4_3_2:	4nm (v4.3 specific to SM8475)
 * @DSI_PHY_VERSION_5_2:        4nm
 * @DSI_PHY_VERSION_MAX:
 */
enum dsi_phy_version {
	DSI_PHY_VERSION_UNKNOWN,
	DSI_PHY_VERSION_3_0, /* 10nm */
	DSI_PHY_VERSION_4_0, /* 7nm  */
	DSI_PHY_VERSION_4_1, /* 7nm */
	DSI_PHY_VERSION_4_2, /* 5nm */
	DSI_PHY_VERSION_4_3, /* 5nm */
	DSI_PHY_VERSION_4_3_2, /* 4nm */
	DSI_PHY_VERSION_5_2, /* 4nm */
	DSI_PHY_VERSION_MAX
};

/**
 * enum dsi_pll_version - DSI PHY PLL version enumeration
 * @DSI_PLL_VERSION_4NM:        4nm PLL
 * @DSI_PLL_VERSION_5NM:        5nm PLL
 * @DSI_PLL_VERSION_10NM:	10nm PLL
 * @DSI_PLL_VERSION_UNKNOWN:	Unknown PLL version
 */
enum dsi_pll_version {
	DSI_PLL_VERSION_4NM,
	DSI_PLL_VERSION_5NM,
	DSI_PLL_VERSION_10NM,
	DSI_PLL_VERSION_UNKNOWN
};

/**
 * enum dsi_phy_hw_features - features supported by DSI PHY hardware
 * @DSI_PHY_DPHY:        Supports DPHY
 * @DSI_PHY_CPHY:        Supports CPHY
 * @DSI_PHY_SPLIT_LINK:  Supports Split Link
 * @DSI_PHY_MAX_FEATURES:
 */
enum dsi_phy_hw_features {
	DSI_PHY_DPHY,
	DSI_PHY_CPHY,
	DSI_PHY_SPLIT_LINK,
	DSI_PHY_MAX_FEATURES
};

/**
 * enum dsi_phy_pll_source - pll clock source for PHY.
 * @DSI_PLL_SOURCE_STANDALONE:    Clock is sourced from native PLL and is not
 *				  shared by other PHYs.
 * @DSI_PLL_SOURCE_NATIVE:        Clock is sourced from native PLL and is
 *				  shared by other PHYs.
 * @DSI_PLL_SOURCE_NON_NATIVE:    Clock is sourced from other PHYs.
 * @DSI_PLL_SOURCE_MAX:
 */
enum dsi_phy_pll_source {
	DSI_PLL_SOURCE_STANDALONE = 0,
	DSI_PLL_SOURCE_NATIVE,
	DSI_PLL_SOURCE_NON_NATIVE,
	DSI_PLL_SOURCE_MAX
};

/**
 * struct dsi_phy_per_lane_cfgs - Holds register values for PHY parameters
 * @lane:           A set of maximum 8 values for each lane.
 * @lane_v3:        A set of maximum 12 values for each lane.
 * @count_per_lane: Number of values per each lane.
 */
struct dsi_phy_per_lane_cfgs {
	u8 lane[DSI_LANE_MAX][DSI_MAX_SETTINGS];
	u8 lane_v3[DSI_PHY_TIMING_V3_SIZE];
	u8 lane_v4[DSI_PHY_TIMING_V4_SIZE];
	u32 count_per_lane;
};

/**
 * struct dsi_phy_cfg - DSI PHY configuration
 * @lanecfg:          Lane configuration settings.
 * @strength:         Strength settings for lanes.
 * @timing:           Timing parameters for lanes.
 * @is_phy_timing_present:	Boolean whether phy timings are defined.
 * @regulators:       Regulator settings for lanes.
 * @pll_source:       PLL source.
 * @data_lanes:       Bitmask of enum dsi_data_lanes.
 * @lane_map:         DSI logical to PHY lane mapping.
 * @force_clk_lane_hs:Boolean whether to force clock lane in HS mode.
 * @phy_type:         Phy-type (Dphy/Cphy).
 * @bit_clk_rate_hz: DSI bit clk rate in HZ.
 * @split_link:       DSI split link config data.
 */
struct dsi_phy_cfg {
	struct dsi_phy_per_lane_cfgs lanecfg;
	struct dsi_phy_per_lane_cfgs strength;
	struct dsi_phy_per_lane_cfgs timing;
	bool is_phy_timing_present;
	struct dsi_phy_per_lane_cfgs regulators;
	enum dsi_phy_pll_source pll_source;
	struct dsi_lane_map lane_map;
	bool force_clk_lane_hs;
	enum dsi_phy_type phy_type;
	unsigned long bit_clk_rate_hz;
	struct dsi_split_link_config split_link;
	u32 data_lanes;
};

struct dsi_phy_hw;

struct phy_ulps_config_ops {
	/**
	 * wait_for_lane_idle() - wait for DSI lanes to go to idle state
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @lanes:         ORed list of lanes (enum dsi_data_lanes) which need
	 *                 to be checked to be in idle state.
	 */
	int (*wait_for_lane_idle)(struct dsi_phy_hw *phy, u32 lanes);

	/**
	 * ulps_request() - request ulps entry for specified lanes
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @cfg:           Per lane configurations for timing, strength and lane
	 *	           configurations.
	 * @lanes:         ORed list of lanes (enum dsi_data_lanes) which need
	 *                 to enter ULPS.
	 *
	 * Caller should check if lanes are in ULPS mode by calling
	 * get_lanes_in_ulps() operation.
	 */
	void (*ulps_request)(struct dsi_phy_hw *phy,
			struct dsi_phy_cfg *cfg, u32 lanes);

	/**
	 * ulps_exit() - exit ULPS on specified lanes
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @cfg:           Per lane configurations for timing, strength and lane
	 *                 configurations.
	 * @lanes:         ORed list of lanes (enum dsi_data_lanes) which need
	 *                 to exit ULPS.
	 *
	 * Caller should check if lanes are in active mode by calling
	 * get_lanes_in_ulps() operation.
	 */
	void (*ulps_exit)(struct dsi_phy_hw *phy,
			struct dsi_phy_cfg *cfg, u32 lanes);

	/**
	 * get_lanes_in_ulps() - returns the list of lanes in ULPS mode
	 * @phy:           Pointer to DSI PHY hardware instance.
	 *
	 * Returns an ORed list of lanes (enum dsi_data_lanes) that are in ULPS
	 * state.
	 *
	 * Return: List of lanes in ULPS state.
	 */
	u32 (*get_lanes_in_ulps)(struct dsi_phy_hw *phy);

	/**
	 * is_lanes_in_ulps() - checks if the given lanes are in ulps
	 * @lanes:           lanes to be checked.
	 * @ulps_lanes:	   lanes in ulps currenly.
	 *
	 * Return: true if all the given lanes are in ulps; false otherwise.
	 */
	bool (*is_lanes_in_ulps)(u32 ulps, u32 ulps_lanes);
};

struct phy_dyn_refresh_ops {
	/**
	 * dyn_refresh_helper - helper function to config particular registers
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @offset:         register offset to program.
	 */
	void (*dyn_refresh_helper)(struct dsi_phy_hw *phy, u32 offset);

	/**
	 * dyn_refresh_trigger_sel - configure trigger_sel to frame flush
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @is_master:      Boolean to indicate whether master or slave.
	 */
	void (*dyn_refresh_trigger_sel)(struct dsi_phy_hw *phy,
			bool is_master);

	/**
	 * dyn_refresh_config - configure dynamic refresh ctrl registers
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @cfg:	   Pointer to DSI PHY timings.
	 * @is_master:	   Boolean to indicate whether for master or slave.
	 */
	void (*dyn_refresh_config)(struct dsi_phy_hw *phy,
				   struct dsi_phy_cfg *cfg, bool is_master);

	/**
	 * dyn_refresh_pipe_delay - configure pipe delay registers for dynamic
	 *				refresh.
	 * @phy:           Pointer to DSI PHY hardware instance.
	 * @delay:	   structure containing all the delays to be programed.
	 */
	void (*dyn_refresh_pipe_delay)(struct dsi_phy_hw *phy,
				      struct dsi_dyn_clk_delay *delay);

	/**
	 * cache_phy_timings - cache the phy timings calculated as part of
	 *				dynamic refresh.
	 * @timings:       Pointer to calculated phy timing parameters.
	 * @dst:	   Pointer to cache location.
	 * @size:	   Number of phy lane settings.
	 */
	int (*cache_phy_timings)(struct dsi_phy_per_lane_cfgs *timings,
				  u32 *dst, u32 size);
};

/**
 * struct dsi_phy_hw_ops - Operations for DSI PHY hardware.
 * @regulator_enable:          Enable PHY regulators.
 * @regulator_disable:         Disable PHY regulators.
 * @enable:                    Enable PHY.
 * @disable:                   Disable PHY.
 * @calculate_timing_params:   Calculate PHY timing params from mode information
 */
struct dsi_phy_hw_ops {
	/**
	 * regulator_enable() - enable regulators for DSI PHY
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @reg_cfg:  Regulator configuration for all DSI lanes.
	 */
	void (*regulator_enable)(struct dsi_phy_hw *phy,
				 struct dsi_phy_per_lane_cfgs *reg_cfg);

	/**
	 * regulator_disable() - disable regulators
	 * @phy:      Pointer to DSI PHY hardware object.
	 */
	void (*regulator_disable)(struct dsi_phy_hw *phy);

	/**
	 * enable() - Enable PHY hardware
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @cfg:      Per lane configurations for timing, strength and lane
	 *	      configurations.
	 */
	void (*enable)(struct dsi_phy_hw *phy, struct dsi_phy_cfg *cfg);

	/**
	 * disable() - Disable PHY hardware
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @cfg:      Per lane configurations for timing, strength and lane
	 *	      configurations.
	 */
	void (*disable)(struct dsi_phy_hw *phy, struct dsi_phy_cfg *cfg);

	/**
	 * phy_idle_on() - Enable PHY hardware when entering idle screen
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @cfg:      Per lane configurations for timing, strength and lane
	 *	      configurations.
	 */
	void (*phy_idle_on)(struct dsi_phy_hw *phy, struct dsi_phy_cfg *cfg);

	/**
	 * phy_idle_off() - Disable PHY hardware when exiting idle screen
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @cfg:      Per lane configurations for timing, strength and lane
	 *	      configurations.
	 */
	void (*phy_idle_off)(struct dsi_phy_hw *phy, struct dsi_phy_cfg *cfg);

	/**
	 * calculate_timing_params() - calculates timing parameters.
	 * @phy:      Pointer to DSI PHY hardware object.
	 * @mode:     Mode information for which timing has to be calculated.
	 * @config:   DSI host configuration for this mode.
	 * @timing:   Timing parameters for each lane which will be returned.
	 * @use_mode_bit_clk: Boolean to indicate whether reacalculate dsi
	 *		bitclk or use the existing bitclk(for dynamic clk case).
	 */
	int (*calculate_timing_params)(struct dsi_phy_hw *phy,
				       struct dsi_mode_info *mode,
				       struct dsi_host_common_cfg *config,
				       struct dsi_phy_per_lane_cfgs *timing,
				       bool use_mode_bit_clk);

	/**
	 * phy_timing_val() - Gets PHY timing values.
	 * @timing_val: Timing parameters for each lane which will be returned.
	 * @timing: Array containing PHY timing values
	 * @size: Size of the array
	 */
	int (*phy_timing_val)(struct dsi_phy_per_lane_cfgs *timing_val,
				u32 *timing, u32 size);

	/**
	 * clamp_ctrl() - configure clamps for DSI lanes
	 * @phy:        DSI PHY handle.
	 * @enable:     boolean to specify clamp enable/disable.
	 * Return:    error code.
	 */
	void (*clamp_ctrl)(struct dsi_phy_hw *phy, bool enable);

	/**
	 * phy_lane_reset() - Reset dsi phy lanes in case of error.
	 * @phy:      Pointer to DSI PHY hardware object.
	 * Return:    error code.
	 */
	int (*phy_lane_reset)(struct dsi_phy_hw *phy);

	/**
	 * toggle_resync_fifo() - toggle resync retime FIFO to sync data paths
	 * @phy:      Pointer to DSI PHY hardware object.
	 * Return:    error code.
	 */
	void (*toggle_resync_fifo)(struct dsi_phy_hw *phy);

	/**
	 * reset_clk_en_sel() - reset clk_en_sel on phy cmn_clk_cfg1 register
	 * @phy:      Pointer to DSI PHY hardware object.
	 */
	void (*reset_clk_en_sel)(struct dsi_phy_hw *phy);

	/**
	 * set_continuous_clk() - Set continuous clock
	 * @phy:	Pointer to DSI PHY hardware object
	 * @enable:	Bool to control continuous clock request.
	 */
	void (*set_continuous_clk)(struct dsi_phy_hw *phy, bool enable);

	/**
	 * commit_phy_timing() - Commit PHY timing
	 * @phy:	Pointer to DSI PHY hardware object.
	 * @timing: Pointer to PHY timing array
	 */
	void (*commit_phy_timing)(struct dsi_phy_hw *phy,
			struct dsi_phy_per_lane_cfgs *timing);

	void *timing_ops;
	struct phy_ulps_config_ops ulps_ops;
	struct phy_dyn_refresh_ops dyn_refresh_ops;

	/**
	 * configure() - Configure the DSI PHY PLL
	 * @pll:	 Pointer to DSI PLL.
	 * @commit:      boolean to specify if calculated PHY configuration
			 needs to be committed. Set to false in case of
			 dynamic clock switch.
	 */
	int (*configure)(void *pll, bool commit);

	/**
	 * pll_toggle() - Toggle the DSI PHY PLL
	 * @pll:	  Pointer to DSI PLL.
	 * @prepare:	  specify if PLL needs to be turned on or off.
	 */
	int (*pll_toggle)(void *pll, bool prepare);

};

/**
 * struct dsi_phy_hw - DSI phy hardware object specific to an instance
 * @base:                  VA for the DSI PHY base address.
 * @length:                Length of the DSI PHY register base map.
 * @dyn_pll_base:      VA for the DSI dynamic refresh base address.
 * @length:                Length of the DSI dynamic refresh register base map.
 * @index:                 Instance ID of the controller.
 * @version:               DSI PHY version.
 * @phy_clamp_base:        Base address of phy clamp register map.
 * @feature_map:           Features supported by DSI PHY.
 * @ops:                   Function pointer to PHY operations.
 */
struct dsi_phy_hw {
	void __iomem *base;
	u32 length;
	void __iomem *dyn_pll_base;
	u32 dyn_refresh_len;
	u32 index;

	enum dsi_phy_version version;
	void __iomem *phy_clamp_base;

	DECLARE_BITMAP(feature_map, DSI_PHY_MAX_FEATURES);
	struct dsi_phy_hw_ops ops;
};

/**
 * dsi_phy_conv_phy_to_logical_lane() - Convert physical to logical lane
 * @lane_map:     logical lane
 * @phy_lane:     physical lane
 *
 * Return: Error code on failure. Lane number on success.
 */
int dsi_phy_conv_phy_to_logical_lane(
	struct dsi_lane_map *lane_map, enum dsi_phy_data_lanes phy_lane);

/**
 * dsi_phy_conv_logical_to_phy_lane() - Convert logical to physical lane
 * @lane_map:     physical lane
 * @lane:         logical lane
 *
 * Return: Error code on failure. Lane number on success.
 */
int dsi_phy_conv_logical_to_phy_lane(
	struct dsi_lane_map *lane_map, enum dsi_logical_lane lane);

#endif /* _DSI_PHY_HW_H_ */
