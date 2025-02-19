/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef __AM_MESON_VPU_H
#define __AM_MESON_VPU_H

#include <linux/amlogic/media/vout/vout_notify.h>

struct am_vout_mode {
	char name[DRM_DISPLAY_MODE_LEN];
	enum vmode_e mode;
	int width, height, vrefresh;
	unsigned int flags;
};

struct meson_vpu_pipeline;

struct meson_vpu_crtc_func {
	struct rdma_reg_ops *reg_ops;
	void (*pre_init)(struct meson_vpu_pipeline *pipeline, struct device *dev);
	void (*init_default_reg)(void);
};

struct meson_vpu_data {
	struct meson_vpu_crtc_func crtc_func;
	struct meson_vpu_pipeline_ops *pipe_ops;
	struct meson_vpu_block_ops *osd_ops;
	struct meson_vpu_block_ops *afbc_ops;
	struct meson_vpu_block_ops *scaler_ops;
	struct meson_vpu_block_ops *osdblend_ops;
	struct meson_vpu_block_ops *hdr_ops;
	struct meson_vpu_block_ops *dv_ops;
	struct meson_vpu_block_ops *postblend_ops;
	struct meson_vpu_block_ops *video_ops;
	struct meson_vpu_block_ops *slice2ppc_ops;
	struct meson_vpu_block_ops *gfcd_ops;
	struct meson_plane_supported_formats *osd_formats;
	struct meson_plane_supported_formats *video_formats;
	int enc_method;
	int slice_mode;
	unsigned int max_osdblend_width, max_osdblend_height;
	const enum meson_policy_id *policy;
};

enum meson_vout_event {
	EVENT_MODE_SET_START = 0,
	EVENT_MODE_SET_FINISH = 1,
};

void meson_vout_notify_mode_change(int idx,
	enum vmode_e mode, enum meson_vout_event event);
void meson_vout_update_mode_name(int idx, char *modename, char *ctx);

/*api in vout_server, for android-compatible.*/
int vout_set_uevent(unsigned int vout_event, int val);
int vout2_set_uevent(unsigned int vout_event, int val);
int vout3_set_uevent(unsigned int vout_event, int val);
int set_vout_mode_name(char *name);
int set_vout2_mode_name(char *name);
int set_vout3_mode_name(char *name);

#endif /* __AM_MESON_VPU_H */
