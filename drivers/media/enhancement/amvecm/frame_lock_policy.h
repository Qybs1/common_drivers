/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef CONFIG_AMLOGIC_ZAPPER_CUT
#ifndef __AML_FRAME_LOCK_POLICY_H__
#define __AML_FRAME_LOCK_POLICY_H__
#include <linux/amlogic/media/vrr/vrr.h>

#define VRRLOCK_SUPPORT_HDMI BIT(0)
#define VRRLOCK_SUPPORT_CVBS BIT(1)

#define FRAME_LOCK_POLICY_VERSION  "20231218 fix vrr line delay issue"

#define FRAMELOCK_INVALID    0
#define FRAMELOCK_VLOCK      1
#define FRAMELOCK_VRRLOCK    2

struct vrr_sig_sts {
	bool vrr_support;
	bool vrr_lfc_mode;
	bool vrr_cinema_flg;
	bool vrr_m_const_flg;
	u16 vrr_pre_en;
	u16 vrr_en;
	u16 vrr_switch_off;
	u16 vrr_policy;
	u16 vrr_policy_pre;
	u16 vrr_frame_sts;
	u16 vrr_frame_pre_sts;
	u16 vrr_frame_lock_type;
	u32 vrr_frame_cur;
	u32 vrr_frame_in_frame_cnt;
	u32 vrr_frame_outof_range_cnt;
	u32 vrr_frame_in_fps_min;
	u32 vrr_frame_in_fps_max;
	u32 vrr_frame_out_fps_min;
	u32 vrr_frame_out_fps_max;
};

struct freesync_vsif_s {
	u8 pkttype;
	u8 vsif_version;
	u8 length;
	u8 checksum;
	u8 amd_ieee_oui[3];
	u8 reserved_pb4_pb5[2];
	u8 freesync_ctr1;
	u8 freesync_min_fps;
	u8 freesync_max_fps;
	u8 freesync_ctr2;
	u8 reserved_pb10_pb27[18];
};

struct freesync_vtem_s {
	u8 vrr_en;
	u8 m_const;
	u8 fva_factor_m1;
	u8 base_vfront;
	u8 rb;
	u16 base_framerate;
};

extern unsigned int probe_ok;
extern unsigned int vecm_latch_flag;
extern unsigned int vlock_en;

u32 vlock_get_vlock_sts(void);

ssize_t frame_lock_debug_store(struct class *cla,
			  struct class_attribute *attr,
		const char *buf, size_t count);
ssize_t frame_lock_debug_show(struct class *cla,
			 struct class_attribute *attr, char *buf);
int flock_vrr_nfy_callback(struct notifier_block *block, unsigned long cmd,
			  void *para);
void frame_lock_vrr_off_done_init(void);
void frame_lock_mode_chg(unsigned int cmd);
u16 frame_lock_get_vrr_status(void);
void frame_lock_set_vrr_support_flag(bool support_flag);
void frame_lock_param_config(struct device_node *node);

#endif
#endif
