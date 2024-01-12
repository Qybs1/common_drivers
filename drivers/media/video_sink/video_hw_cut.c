// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/amlogic/major.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/media/vfm/vframe.h>
#include <linux/amlogic/media/vfm/vframe_provider.h>
#include <linux/amlogic/media/vfm/vframe_receiver.h>
#include <linux/amlogic/media/utils/amstream.h>
#ifdef CONFIG_AMLOGIC_VOUT
#include <linux/amlogic/media/vout/vout_notify.h>
#endif
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/clk.h>
#include <linux/arm-smccc.h>
#include <linux/debugfs.h>
#include <linux/amlogic/media/canvas/canvas.h>
#include <linux/amlogic/media/canvas/canvas_mgr.h>
#include <linux/sched.h>
#include <linux/amlogic/media/video_sink/video_keeper.h>
#include "video_priv.h"
#include "video_reg.h"
#include "video_common.h"
#include "video_hw.h"
#include "video_hw_s5.h"
#include "vpp_post_s5.h"

#if defined(CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_VECM)
#include <linux/amlogic/media/amvecm/amvecm.h>
#endif
#include <linux/amlogic/media/utils/vdec_reg.h>

#include <linux/amlogic/media/registers/register.h>
#include <linux/uaccess.h>
#include <linux/amlogic/media/utils/amports_config.h>
#ifdef CONFIG_AMLOGIC_VPU
#include <linux/amlogic/media/vpu/vpu.h>
#endif
#include "videolog.h"

#include <linux/amlogic/media/video_sink/vpp.h>
#ifdef CONFIG_AMLOGIC_MEDIA_TVIN
#include "linux/amlogic/media/frame_provider/tvin/tvin_v4l2.h"
#endif
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
#include "../common/rdma/rdma.h"
#endif
#include <linux/amlogic/media/video_sink/video.h>
#include "../common/vfm/vfm.h"
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
#include <linux/amlogic/media/amdolbyvision/dolby_vision.h>
#endif
#include "video_receiver.h"

#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
#include <linux/amlogic/media/vpu_secure/vpu_secure.h>
#endif
#include <linux/amlogic/media/video_sink/video_signal_notify.h>
#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
#include <linux/amlogic/media/di/di_interface.h>
#endif

struct video_layer_s vd_layer[MAX_VD_LAYER];
struct disp_info_s glayer_info[MAX_VD_LAYER];

struct video_dev_s video_dev;
struct video_dev_s *cur_dev = &video_dev;
bool legacy_vpp = true;

static bool bypass_cm;
bool hscaler_8tap_enable[MAX_VD_LAYER];
struct pre_scaler_info pre_scaler[MAX_VD_LAYER];
static bool video_mute_array[MAX_VIDEO_MUTE_OWNER];

static DEFINE_SPINLOCK(video_onoff_lock);
static DEFINE_SPINLOCK(video2_onoff_lock);
static DEFINE_MUTEX(video_mute_mutex);

#ifdef CONFIG_AMLOGIC_VPU
/* VPU delay work */
#define VPU_DELAYWORK_VPU_VD1_CLK		BIT(0)
#define VPU_DELAYWORK_VPU_VD2_CLK		BIT(1)
#define VPU_DELAYWORK_VPU_VD3_CLK		BIT(2)

#define VPU_DELAYWORK_MEM_POWER_OFF_VD1		BIT(3)
#define VPU_DELAYWORK_MEM_POWER_OFF_VD2		BIT(4)
#define VPU_DELAYWORK_MEM_POWER_OFF_VD3		BIT(5)

#define VPU_VIDEO_LAYER1_CHANGED		BIT(6)
#define VPU_VIDEO_LAYER2_CHANGED		BIT(7)
#define VPU_VIDEO_LAYER3_CHANGED		BIT(8)

#define VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0		BIT(9)
#define VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A		BIT(10)
#define VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B		BIT(11)
#define VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2		BIT(12)
#define VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3		BIT(13)
#define VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY		BIT(14)

#define VPU_MEM_POWEROFF_DELAY	100
#define DV_MEM_POWEROFF_DELAY	2

static struct work_struct vpu_delay_work;
static int vpu_vd1_clk_level;
static int vpu_vd2_clk_level;
static DEFINE_SPINLOCK(delay_work_lock);
static int vpu_delay_work_flag;
static int vpu_mem_power_off_count;
static int dv_mem_power_off_count;
static struct vpu_dev_s *vd1_vpu_dev;
static struct vpu_dev_s *afbc_vpu_dev;
#ifdef DI_POST_PWR
static struct vpu_dev_s *di_post_vpu_dev;
#endif
static struct vpu_dev_s *vd1_scale_vpu_dev;
static struct vpu_dev_s *vpu_fgrain0;
static struct vpu_dev_s *vd2_vpu_dev;
static struct vpu_dev_s *afbc2_vpu_dev;
static struct vpu_dev_s *vd2_scale_vpu_dev;

static struct vpu_dev_s *vpu_dolby0;
static struct vpu_dev_s *vpu_dolby1a;
static struct vpu_dev_s *vpu_dolby1b;
static struct vpu_dev_s *vpu_dolby2;
static struct vpu_dev_s *vpu_dolby_core3;
static struct vpu_dev_s *vpu_prime_dolby_ram;

#define VPU_VD1_CLK_SWITCH(level) \
	do { \
		unsigned long flags; \
		int next_level = (level); \
		if (vpu_vd1_clk_level != next_level) { \
			vpu_vd1_clk_level = next_level; \
			spin_lock_irqsave(&delay_work_lock, flags); \
			vpu_delay_work_flag |= \
				VPU_DELAYWORK_VPU_VD1_CLK; \
			spin_unlock_irqrestore(&delay_work_lock, flags); \
		} \
	} while (0)

#define VPU_VD2_CLK_SWITCH(level) \
	do { \
		unsigned long flags; \
		int next_level = (level); \
		if (vpu_vd2_clk_level != next_level) { \
			vpu_vd2_clk_level = next_level; \
			spin_lock_irqsave(&delay_work_lock, flags); \
			vpu_delay_work_flag |= \
				VPU_DELAYWORK_VPU_VD2_CLK; \
			spin_unlock_irqrestore(&delay_work_lock, flags); \
		} \
	} while (0)

#ifdef DI_POST_PWR
#define VD1_MEM_POWER_ON() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&delay_work_lock, flags); \
		vpu_delay_work_flag &= ~VPU_DELAYWORK_MEM_POWER_OFF_VD1; \
		spin_unlock_irqrestore(&delay_work_lock, flags); \
		vpu_dev_mem_power_on(vd1_vpu_dev); \
		vpu_dev_mem_power_on(afbc_vpu_dev); \
		vpu_dev_mem_power_on(di_post_vpu_dev); \
		if (!legacy_vpp) \
			vpu_dev_mem_power_on(vd1_scale_vpu_dev); \
		if (glayer_info[0].fgrain_support) \
			vpu_dev_mem_power_on(vpu_fgrain0); \
	} while (0)
#else
#define VD1_MEM_POWER_ON() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&delay_work_lock, flags); \
		vpu_delay_work_flag &= ~VPU_DELAYWORK_MEM_POWER_OFF_VD1; \
		spin_unlock_irqrestore(&delay_work_lock, flags); \
		vpu_dev_mem_power_on(vd1_vpu_dev); \
		vpu_dev_mem_power_on(afbc_vpu_dev); \
		if (!legacy_vpp) \
			vpu_dev_mem_power_on(vd1_scale_vpu_dev); \
	} while (0)
#endif
#define VD2_MEM_POWER_ON() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&delay_work_lock, flags); \
		vpu_delay_work_flag &= ~VPU_DELAYWORK_MEM_POWER_OFF_VD2; \
		spin_unlock_irqrestore(&delay_work_lock, flags); \
		vpu_dev_mem_power_on(vd2_vpu_dev); \
		vpu_dev_mem_power_on(afbc2_vpu_dev); \
		if (!legacy_vpp) \
			vpu_dev_mem_power_on(vd2_scale_vpu_dev); \
	} while (0)

#define VD1_MEM_POWER_OFF() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&delay_work_lock, flags); \
		vpu_delay_work_flag |= VPU_DELAYWORK_MEM_POWER_OFF_VD1; \
		vpu_mem_power_off_count = VPU_MEM_POWEROFF_DELAY; \
		spin_unlock_irqrestore(&delay_work_lock, flags); \
	} while (0)
#define VD2_MEM_POWER_OFF() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&delay_work_lock, flags); \
		vpu_delay_work_flag |= VPU_DELAYWORK_MEM_POWER_OFF_VD2; \
		vpu_mem_power_off_count = VPU_MEM_POWEROFF_DELAY; \
		spin_unlock_irqrestore(&delay_work_lock, flags); \
	} while (0)
#endif

#define VIDEO_LAYER_ON() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&video_onoff_lock, flags); \
		if (vd_layer[0].onoff_state != VIDEO_ENABLE_STATE_ON_PENDING) \
			vd_layer[0].onoff_state = \
			VIDEO_ENABLE_STATE_ON_PENDING; \
		vd_layer[0].enabled = 1; \
		vd_layer[0].enabled_status_saved = 1; \
		spin_unlock_irqrestore(&video_onoff_lock, flags); \
	} while (0)

#define VIDEO_LAYER_OFF() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&video_onoff_lock, flags); \
		vd_layer[0].onoff_state = VIDEO_ENABLE_STATE_OFF_REQ; \
		vd_layer[0].enabled = 0; \
		vd_layer[0].enabled_status_saved = 0; \
		spin_unlock_irqrestore(&video_onoff_lock, flags); \
	} while (0)

#define VIDEO_LAYER2_ON() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&video2_onoff_lock, flags); \
		if (vd_layer[1].onoff_state != VIDEO_ENABLE_STATE_ON_PENDING) \
			vd_layer[1].onoff_state = \
			VIDEO_ENABLE_STATE_ON_PENDING; \
		vd_layer[1].enabled = 1; \
		vd_layer[1].enabled_status_saved = 1; \
		spin_unlock_irqrestore(&video2_onoff_lock, flags); \
	} while (0)

#define VIDEO_LAYER2_OFF() \
	do { \
		unsigned long flags; \
		spin_lock_irqsave(&video2_onoff_lock, flags); \
		vd_layer[1].onoff_state = VIDEO_ENABLE_STATE_OFF_REQ; \
		vd_layer[1].enabled = 0; \
		vd_layer[1].enabled_status_saved = 0; \
		spin_unlock_irqrestore(&video2_onoff_lock, flags); \
	} while (0)

#ifdef CONFIG_AMLOGIC_VPU
#define enable_video_layer()  \
	do { \
		VD1_MEM_POWER_ON(); \
		VIDEO_LAYER_ON(); \
	} while (0)

#define enable_video_layer2()  \
	do { \
		VD2_MEM_POWER_ON(); \
		if (vd_layer[1].vpp_index == VPP0) \
			VIDEO_LAYER2_ON(); \
	} while (0)

#define disable_video_layer(async) \
	do { \
		if (!(async)  && \
			cur_dev->display_module != C3_DISPLAY_MODULE) { \
			CLEAR_VCBUS_REG_MASK( \
				VPP_MISC + cur_dev->vpp_off, \
				VPP_VD1_PREBLEND | \
				VPP_VD1_POSTBLEND); \
			if (!legacy_vpp) \
				WRITE_VCBUS_REG( \
					VD1_BLEND_SRC_CTRL + \
					vd_layer[0].misc_reg_offt, 0); \
			if (!vd_layer[0].vd1_vd2_mux) { \
				WRITE_VCBUS_REG( \
					vd_layer[0].vd_afbc_reg.afbc_enable, 0); \
				WRITE_VCBUS_REG( \
					vd_layer[0].vd_mif_reg.vd_if0_gen_reg, 0); \
			} else { \
				WRITE_VCBUS_REG( \
					vd_layer[1].vd_mif_reg.vd_if0_gen_reg, 0); \
			} \
		} \
		VIDEO_LAYER_OFF(); \
		VD1_MEM_POWER_OFF(); \
		if (vd_layer[0].global_debug & DEBUG_FLAG_BASIC_INFO) {  \
			pr_info("VIDEO: DisableVideoLayer()\n"); \
		} \
	} while (0)

#define disable_video_layer2(async) \
	do { \
		if (!(async)) { \
			CLEAR_VCBUS_REG_MASK( \
				VPP_MISC + cur_dev->vpp_off, \
				VPP_VD2_POSTBLEND | \
				VPP_VD2_PREBLEND); \
			if (!legacy_vpp) \
				WRITE_VCBUS_REG( \
					VD2_BLEND_SRC_CTRL + \
					vd_layer[1].misc_reg_offt, 0); \
			WRITE_VCBUS_REG( \
				vd_layer[1].vd_afbc_reg.afbc_enable, 0); \
			WRITE_VCBUS_REG( \
				vd_layer[1].vd_mif_reg.vd_if0_gen_reg, 0); \
		} \
		if (vd_layer[1].vpp_index == VPP0) \
			VIDEO_LAYER2_OFF(); \
		VD2_MEM_POWER_OFF(); \
		if (vd_layer[1].global_debug & DEBUG_FLAG_BASIC_INFO) {  \
			pr_info("VIDEO: DisableVideoLayer2()\n"); \
		} \
	} while (0)
#endif

#ifndef CONFIG_AMLOGIC_VPU
#define enable_video_layer()		VIDEO_LAYER_ON()

#define enable_video_layer2()  \
		do { \
			if (vd_layer[1].vpp_index == VPP0) \
				VIDEO_LAYER2_ON(); \
		} while (0)

#define disable_video_layer(async) \
		do { \
			if (!(async)) { \
				CLEAR_VCBUS_REG_MASK( \
					VPP_MISC + cur_dev->vpp_off, \
					VPP_VD1_PREBLEND | \
					VPP_VD1_POSTBLEND); \
				if (!legacy_vpp) \
					WRITE_VCBUS_REG( \
						VD1_BLEND_SRC_CTRL + \
						vd_layer[0].misc_reg_offt, 0); \
				if (!vd_layer[0].vd1_vd2_mux) { \
					WRITE_VCBUS_REG( \
						vd_layer[0].vd_afbc_reg.afbc_enable, 0); \
					WRITE_VCBUS_REG( \
						vd_layer[0].vd_mif_reg.vd_if0_gen_reg, 0); \
				} else { \
					WRITE_VCBUS_REG( \
						vd_layer[1].vd_mif_reg.vd_if0_gen_reg, 0); \
				} \
			} \
			VIDEO_LAYER_OFF(); \
			if (vd_layer[0].global_debug & DEBUG_FLAG_BASIC_INFO) {  \
				pr_info("VIDEO: DisableVideoLayer()\n"); \
			} \
		} while (0)

#define disable_video_layer2(async) \
		do { \
			if (!(async)) { \
				CLEAR_VCBUS_REG_MASK( \
					VPP_MISC + cur_dev->vpp_off, \
					VPP_VD2_POSTBLEND | \
					VPP_VD2_PREBLEND); \
				if (!legacy_vpp) \
					WRITE_VCBUS_REG( \
						VD2_BLEND_SRC_CTRL + \
						vd_layer[1].misc_reg_offt, 0); \
				WRITE_VCBUS_REG( \
					vd_layer[1].vd_afbc_reg.afbc_enable, 0); \
				WRITE_VCBUS_REG( \
					vd_layer[1].vd_mif_reg.vd_if0_gen_reg, 0); \
			} \
			if (vd_layer[1].vpp_index == VPP0) \
				VIDEO_LAYER2_OFF(); \
			if (vd_layer[1].global_debug & DEBUG_FLAG_BASIC_INFO) {  \
				pr_info("VIDEO: DisableVideoLayer2()\n"); \
			} \
		} while (0)
#endif

#define disable_video_all_layer_nodelay() \
	do { \
		CLEAR_VCBUS_REG_MASK( \
			VPP_MISC + cur_dev->vpp_off, \
			VPP_VD1_PREBLEND | VPP_VD2_PREBLEND |\
			VPP_VD2_POSTBLEND | VPP_VD1_POSTBLEND); \
		if (!legacy_vpp) \
			WRITE_VCBUS_REG( \
				VD1_BLEND_SRC_CTRL + \
				vd_layer[0].misc_reg_offt, 0); \
		if (!vd_layer[0].vd1_vd2_mux) \
			WRITE_VCBUS_REG( \
				vd_layer[0].vd_afbc_reg.afbc_enable, 0); \
		WRITE_VCBUS_REG( \
			vd_layer[0].vd_mif_reg.vd_if0_gen_reg, \
			0); \
		if (!legacy_vpp) \
			WRITE_VCBUS_REG( \
				VD2_BLEND_SRC_CTRL + \
				vd_layer[1].misc_reg_offt, 0); \
		WRITE_VCBUS_REG( \
			vd_layer[1].vd_afbc_reg.afbc_enable, 0); \
		WRITE_VCBUS_REG( \
			vd_layer[1].vd_mif_reg.vd_if0_gen_reg, 0); \
		pr_info("VIDEO: disable_video_all_layer_nodelay()\n"); \
	} while (0)

#ifdef CONFIG_AMLOGIC_VPU
void dv_mem_power_off(enum vpu_mod_e mode)
{
#ifdef DV_PWR
	unsigned long flags;
#endif
	unsigned int dv_flag;

	if (mode == VPU_DOLBY0)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0;
	else if (mode == VPU_DOLBY1A)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A;
	else if (mode == VPU_DOLBY1B)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B;
	else if (mode == VPU_DOLBY2)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2;
	else if (mode == VPU_DOLBY_CORE3)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3;
	else if (mode == VPU_PRIME_DOLBY_RAM)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY;
	else
		return;
	/*currently power function not ok*/
#ifdef DV_PWR
	spin_lock_irqsave(&delay_work_lock, flags);
	vpu_delay_work_flag |= dv_flag;
	dv_mem_power_off_count = DV_MEM_POWEROFF_DELAY;
	spin_unlock_irqrestore(&delay_work_lock, flags);
#endif
}
EXPORT_SYMBOL(dv_mem_power_off);

void dv_mem_power_on(enum vpu_mod_e mode)
{
	unsigned long flags;
	unsigned int dv_flag;
	static struct vpu_dev_s *p_dev;

	if (mode == VPU_DOLBY0) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0;
		p_dev = vpu_dolby0;
	} else if (mode == VPU_DOLBY1A) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A;
		p_dev = vpu_dolby1a;
	} else if (mode == VPU_DOLBY1B) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B;
		p_dev = vpu_dolby1b;
	} else if (mode == VPU_DOLBY2) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2;
		p_dev = vpu_dolby2;
	} else if (mode == VPU_DOLBY_CORE3) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3;
		p_dev = vpu_dolby_core3;
	} else if (mode == VPU_PRIME_DOLBY_RAM) {
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY;
		p_dev = vpu_prime_dolby_ram;
	} else {
		return;
	}
	spin_lock_irqsave(&delay_work_lock, flags);
	vpu_delay_work_flag &= ~dv_flag;
	spin_unlock_irqrestore(&delay_work_lock, flags);
	vpu_dev_mem_power_on(p_dev);
}
EXPORT_SYMBOL(dv_mem_power_on);

/*get mem power status on the way*/
int get_dv_mem_power_flag(enum vpu_mod_e mode)
{
#ifdef DV_PWR
	unsigned long flags;
#endif
	unsigned int dv_flag;
	int ret = VPU_MEM_POWER_ON;

	if (mode == VPU_DOLBY0)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0;
	else if (mode == VPU_DOLBY1A)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A;
	else if (mode == VPU_DOLBY1B)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B;
	else if (mode == VPU_DOLBY2)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2;
	else if (mode == VPU_DOLBY_CORE3)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3;
	else if (mode == VPU_PRIME_DOLBY_RAM)
		dv_flag = VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY;
	else
		return -1;
	/*currently power function not ok*/
#ifndef DV_PWR
	return ret;
#else
	spin_lock_irqsave(&delay_work_lock, flags);
	if (vpu_delay_work_flag & dv_flag)
		ret = VPU_MEM_POWER_DOWN;
	spin_unlock_irqrestore(&delay_work_lock, flags);
	return ret;
#endif
}
EXPORT_SYMBOL(get_dv_mem_power_flag);

/*get vpu mem power status*/
int get_dv_vpu_mem_power_status(enum vpu_mod_e mode)
{
	int ret = VPU_MEM_POWER_ON;
	static struct vpu_dev_s *p_dev;

	if (mode == VPU_DOLBY0)
		p_dev = vpu_dolby0;
	else if (mode == VPU_DOLBY1A)
		p_dev = vpu_dolby1a;
	else if (mode == VPU_DOLBY1B)
		p_dev = vpu_dolby1b;
	else if (mode == VPU_DOLBY2)
		p_dev = vpu_dolby2;
	else if (mode == VPU_DOLBY_CORE3)
		p_dev = vpu_dolby_core3;
	else if (mode == VPU_PRIME_DOLBY_RAM)
		p_dev = vpu_prime_dolby_ram;
	else
		return ret;
	/*currently power function not ok*/
#ifndef DV_PWR
	return ret;
#else
	ret = vpu_dev_mem_pd_get(p_dev);
	return ret;
#endif
}
EXPORT_SYMBOL(get_dv_vpu_mem_power_status);
#endif

/*********************************************************/
static struct vframe_pic_mode_s gpic_info[MAX_VD_LAYERS];
u32 reference_zorder = 128;
static int param_vpp_num = VPP_MAX;
u32 vpp_hold_line[VPP_MAX] = {8, 8, 8, 8};
static u32 cur_vpp_num = MAX_VPP_NUM;
static unsigned int cur_vf_flag[MAX_VPP_NUM];
static u32 vpp_ofifo_size = 0x1000;
static u32 conv_lbuf_len[MAX_VD_LAYER] = {0x100, 0x100};

static const enum f2v_vphase_type_e vpp_phase_table[4][3] = {
	{F2V_P2IT, F2V_P2IB, F2V_P2P},	/* VIDTYPE_PROGRESSIVE */
	{F2V_IT2IT, F2V_IT2IB, F2V_IT2P},	/* VIDTYPE_INTERLACE_TOP */
	{F2V_P2IT, F2V_P2IB, F2V_P2P},
	{F2V_IB2IT, F2V_IB2IB, F2V_IB2P}	/* VIDTYPE_INTERLACE_BOTTOM */
};

static const u8 skip_tab[6] = { 0x24, 0x04, 0x68, 0x48, 0x28, 0x08 };

static bool video_mute_on;
/* 0: off, 1: vpp mute 2:dv mute */
static int video_mute_status;
static bool output_mute_on;
/* 0: off, 1: on */
static int output_mute_status;
static int debug_flag_3d = 0xf;
static  int vd1_matrix;

static int video_testpattern_status[MAX_VD_LAYER];
static int postblend_testpattern_status;
static bool vdx_test_pattern_on[MAX_VD_LAYER];
static bool postblend_test_pattern_on;
static u32 vdx_color[MAX_VD_LAYER];
static u32 postblend_color;

u32 g_mosaic_mode;
MODULE_PARM_DESC(g_mosaic_mode, "\n g_mosaic_mode\n");
module_param(g_mosaic_mode, uint, 0664);
u32 pic_axis[4][4];
/*********************************************************
 * Utils APIs
 *********************************************************/
#ifndef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
#if IS_MODULE(CONFIG_AMLOGIC_MEDIA_VIDEO)
bool is_amdv_enable(void)
{
	return false;
}
#endif

bool is_amdv_on(void)
{
	return false;
}

bool is_amdv_stb_mode(void)
{
	return false;
}

bool for_amdv_certification(void)
{
	return false;
}

void dv_vf_light_reg_provider(void)
{
}

void dv_vf_light_unreg_provider(void)
{
}

void amdv_update_backlight(void)
{
}

int is_amdv_frame(struct vframe_s *vf)
{
	return false;
}

void amdv_set_toggle_flag(int flag)
{
}

int get_dv_support_info(void)
{
	return false;
}

bool support_multi_core1(void)
{
	return false;
}

bool is_hdmi_ll_as_hdr10(void)
{
	return false;
}

#if IS_MODULE(CONFIG_AMLOGIC_MEDIA_VIDEO)
int get_amdv_mode(void)
{
	return false;
}

void set_amdv_mode(int mode)
{
}
#endif
#endif

bool is_dovi_tv_on(void)
{
#ifndef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
	return false;
#else
	return ((is_meson_txlx_package_962X() ||
		is_meson_tm2_cpu() ||
		is_meson_t7_cpu() ||
		is_meson_t3_cpu() ||
		is_meson_t5w_cpu() ||
		is_meson_t5m_cpu()) &&
		!is_amdv_stb_mode() && is_amdv_on());
#endif
}

struct video_dev_s *get_video_cur_dev(void)
{
	return cur_dev;
}

u32 get_video_enabled(u8 layer_id)
{
	u32 global_output = 0;

	switch (layer_id) {
	case 0:
		global_output = vd_layer[0].enabled &&
			vd_layer[0].global_output &&
			!vd_layer[0].force_disable;
		break;
	case 1:
		if (vd_layer[1].vpp_index == VPP0)
			global_output = vd_layer[1].enabled &&
				vd_layer[1].global_output &&
				!vd_layer[1].force_disable;
		break;
	case 2:
		if (vd_layer[2].vpp_index == VPP0)
			global_output = vd_layer[2].enabled &&
				vd_layer[2].global_output &&
				!vd_layer[2].force_disable;
		break;
	default:
		break;
	}
	return global_output;
}
EXPORT_SYMBOL(get_video_enabled);

void set_video_enabled(u32 value, u32 index)
{
	u32 disable_video = value ? 0 : 1;
	struct video_layer_s *layer = NULL;

	if (index >= MAX_VD_LAYER)
		return;
	layer = get_layer_by_layer_id(index);

	if (!layer)
		return;

	layer->global_output = value;
	if (index == 0)
		_video_set_disable(disable_video);
	else
		_videopip_set_disable(index, disable_video);
}
EXPORT_SYMBOL(set_video_enabled);

bool get_disable_video_flag(enum vd_path_e vd_path)
{
	if (vd_path == VD1_PATH)
		return vd_layer[0].disable_video == VIDEO_DISABLE_NORMAL;
	else if (vd_path == VD2_PATH)
		return vd_layer[1].disable_video == VIDEO_DISABLE_NORMAL;
	else if (vd_path == VD3_PATH)
		return vd_layer[2].disable_video == VIDEO_DISABLE_NORMAL;
	return false;
}
EXPORT_SYMBOL(get_disable_video_flag);

u32 get_video_onoff_state(u8 layer_id)
{
	u32 state = VIDEO_ENABLE_STATE_IDLE;

	switch (layer_id) {
	case 0:
		state = vd_layer[0].onoff_state;
		break;
	case 1:
		if (vd_layer[1].vpp_index == VPP0)
			state = vd_layer[1].onoff_state;
		break;
	case 2:
		if (vd_layer[2].vpp_index == VPP0)
			state = vd_layer[2].onoff_state;
		break;
	default:
		break;
	}
	return state;
}

struct video_layer_s *get_vd_layer(u8 layer_id)
{
	struct video_layer_s *layer = &vd_layer[layer_id];

	if (is_vpp0(layer_id))
		/* single vpp */
		layer = &vd_layer[layer_id];
	return layer;
}

void update_vd_src_info(u8 layer_id,
							u32 src_width, u32 src_height,
							u32 compWidth, u32 compHeight)
{
	struct video_layer_s *layer = get_vd_layer(layer_id);

	layer->src_width = src_width;
	layer->src_height = src_height;
	layer->compWidth = compWidth;
	layer->compHeight = compHeight;
}

bool is_bandwidth_policy_hit(u8 layer_id)
{
	return false;
}

bool is_di_on(void)
{
	bool ret = false;

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if ((dil_get_diff_ver_flag() == DI_DRV_DEINTERLACE) &&
	    (DI_POST_REG_RD(DI_IF1_GEN_REG) & 0x1))
		ret = true;
#endif
	return ret;
}

bool is_di_post_on(void)
{
	bool ret = false;

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if ((dil_get_diff_ver_flag() == DI_DRV_DEINTERLACE) &&
	    (DI_POST_REG_RD(DI_POST_CTRL) & 0x100))
		ret = true;
#endif
	return ret;
}

bool is_di_post_link_on(void)
{
	bool ret = false;

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if ((dil_get_diff_ver_flag() == DI_DRV_DEINTERLACE) &&
	    (DI_POST_REG_RD(DI_POST_CTRL) & 0x1000))
		ret = true;
#endif
	return ret;
}

bool is_di_post_mode(struct vframe_s *vf)
{
	bool ret = false;

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if ((dil_get_diff_ver_flag() == DI_DRV_DEINTERLACE) &&
	    vf && IS_DI_POST(vf->type))
		ret = true;
#endif
	return ret;
}

bool is_pre_link_source(struct vframe_s *vf)
{
#ifdef ENABLE_PRE_LINK
	if (vf && pvpp_check_vf(vf) > 0 && vf->vf_ext)
		return true;
#endif
	return false;
}

bool is_pre_link_on(struct video_layer_s *layer)
{
	if (!layer)
		return false;
	return layer->pre_link_en;
}

#ifdef ENABLE_PRE_LINK
bool is_pre_link_available(struct vframe_s *vf)
{
	if (pvpp_check_vf(vf) > 0 && pvpp_check_act() > 0)
		return true;
	return false;
}
#endif

bool is_afbc_enabled(u8 layer_id)
{
	struct video_layer_s *layer = NULL;
	u32 value;

	if (layer_id >= MAX_VD_LAYER)
		return -1;

	layer = &vd_layer[layer_id];
	value = READ_VCBUS_REG(layer->vd_afbc_reg.afbc_enable);

	return (value & 0x100) ? true : false;
}

bool is_local_vf(struct vframe_s *vf)
{
	if (vf &&
	    (vf == &vf_local[0] ||
	     vf == &vf_local2 ||
	     vf == &vf_local[1] ||
	     vf == &vf_local[2]))
		return true;

	/* FIXEME: remove gvideo_recv */
	if (vf && gvideo_recv[0] &&
	    vf == &gvideo_recv[0]->local_buf)
		return true;
	if (vf && gvideo_recv[1] &&
	    vf == &gvideo_recv[1]->local_buf)
		return true;
	if (vf && gvideo_recv[2] &&
	    vf == &gvideo_recv[2]->local_buf)
		return true;
	if (vf && gvideo_recv_vpp[0] &&
	    vf == &gvideo_recv_vpp[0]->local_buf)
		return true;
	if (vf && gvideo_recv_vpp[1] &&
	    vf == &gvideo_recv_vpp[1]->local_buf)
		return true;
	return false;
}

/*********************************************************
 * vd HW APIs
 *********************************************************/
void vpu_module_clk_enable(u32 vpp_index, u32 module, bool async)
{
	if (!cur_dev->power_ctrl)
		return;
	if (async) {
		switch (module) {
		case VD1_SCALER:
			WRITE_VCBUS_REG_BITS
			(VPP_SC_GCLK_CTRL_T7,
			0x0, 0, 16);
		break;
		case VD2_SCALER:
			WRITE_VCBUS_REG_BITS
			(VD2_SC_GCLK_CTRL_T7,
			0x0, 0, 16);
		break;
		case SR0:
			WRITE_VCBUS_REG_BITS
			(VPP_SRSCL_GCLK_CTRL,
			0x0, 0, 8);
		break;
		case SR1:
			WRITE_VCBUS_REG_BITS
			(VPP_SRSCL_GCLK_CTRL,
			0x0, 8, 8);
		break;
		case VD1_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(VD1_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case VD2_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(VD2_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case OSD1_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(OSD1_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case OSD2_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(OSD2_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case DV_TVCORE:
			WRITE_VCBUS_REG_BITS
			(VPU_CLK_GATE,
			1, 4, 1);
		break;
		}

	} else {
		switch (module) {
		case VD1_SCALER:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SC_GCLK_CTRL_T7,
			0x0, 0, 16);
		break;
		case VD2_SCALER:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD2_SC_GCLK_CTRL_T7,
			0x0, 0, 16);
		break;
		case SR0:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SRSCL_GCLK_CTRL,
			0x0, 0, 8);
		break;
		case SR1:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SRSCL_GCLK_CTRL,
			0x0, 8, 8);
		break;
		case VD1_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD1_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case VD2_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD2_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case OSD1_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(OSD1_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case OSD2_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(OSD2_HDR2_CLK_GATE,
			0x0, 0, 30);
		break;
		case DV_TVCORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPU_CLK_GATE,
			1, 4, 1);
		break;
		}
	}
}

void vpu_module_clk_disable(u32 vpp_index, u32 module, bool async)
{
	if (!cur_dev->power_ctrl)
		return;
	if (async) {
		switch (module) {
		case VD1_SCALER:
			WRITE_VCBUS_REG_BITS
			(VPP_SC_GCLK_CTRL_T7,
			0x5554, 0, 16);
		break;
		case VD2_SCALER:
			WRITE_VCBUS_REG_BITS
			(VD2_SC_GCLK_CTRL_T7,
			0x5554, 0, 16);
		break;
		case SR0:
			if ((READ_VCBUS_REG(VPP_SRSCL_GCLK_CTRL) &
			    0xff) != 0x05)
				WRITE_VCBUS_REG_BITS
				(VPP_SRSCL_GCLK_CTRL,
				0x05, 0, 8);
		break;
		case SR1:
			if ((READ_VCBUS_REG(VPP_SRSCL_GCLK_CTRL) &
			    0xff00) != 0x0500)
				WRITE_VCBUS_REG_BITS
				(VPP_SRSCL_GCLK_CTRL,
				0x05, 8, 8);
		break;
		case VD1_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(VD1_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case VD2_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(VD2_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case OSD1_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(OSD1_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case OSD2_HDR_CORE:
			WRITE_VCBUS_REG_BITS
			(OSD2_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case DV_TVCORE:
			WRITE_VCBUS_REG_BITS
			(VPU_CLK_GATE,
			0, 4, 1);
		break;
		}
	} else {
		switch (module) {
		case VD1_SCALER:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SC_GCLK_CTRL_T7,
			0x5554, 0, 16);
		break;
		case VD2_SCALER:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD2_SC_GCLK_CTRL_T7,
			0x5554, 0, 16);
		break;
		case SR0:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SRSCL_GCLK_CTRL,
			0x05, 0, 8);
		break;
		case SR1:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_SRSCL_GCLK_CTRL,
			0x05, 8, 8);
		break;
		case VD1_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD1_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case VD2_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD2_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case OSD1_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(OSD1_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case OSD2_HDR_CORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(OSD2_HDR2_CLK_GATE,
			0x11555555, 0, 30);
		break;
		case DV_TVCORE:
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPU_CLK_GATE,
			0, 4, 1);
		break;
		}
	}
}

void safe_switch_videolayer(u8 layer_id, bool on, bool async)
{
	if (layer_id == 0xff)
		pr_debug("VID: VD all %s with async %s\n",
			 on ? "on" : "off",
			 async ? "true" : "false");
	else
		pr_debug("VID: VD%d %s with async %s\n",
			 layer_id,
			 on ? "on" : "off",
			 async ? "true" : "false");

	if (layer_id == 0) {
		if (on)
			enable_video_layer();
		else
			disable_video_layer(async);
	}

	if (layer_id == 1) {
		if (on)
			enable_video_layer2();
		else
			disable_video_layer2(async);
	}

	if (layer_id == 0xff) {
		if (on) {
			enable_video_layer();
			enable_video_layer2();
		} else  if (async) {
			disable_video_layer(async);
			disable_video_layer2(async);
		} else {
			disable_video_all_layer_nodelay();
		}
	}
}

static void vd1_path_select(struct video_layer_s *layer,
			    bool afbc, bool di_afbc,
			    bool di_post, bool di_pre_link)
{
	u32 misc_off = layer->misc_reg_offt;
	u8 vpp_index;

	vpp_index = layer->vpp_index;
	if (cur_dev->display_module == T7_DISPLAY_MODULE)
		return;

	if (!legacy_vpp && !layer->vd1_vd2_mux) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD1_AFBCD0_MISC_CTRL,
			/* go field sel */
			(0 << 20) |
			/* linebuffer en */
			(0 << 16) |
			/* vd1 -> dolby -> vpp top */
			(0 << 14) |
			/* axi sel: vd1 mif or afbc */
			(((afbc || di_afbc) ? 1 : 0) << 12) |
			/* data sel: vd1 & afbc0 (not osd4) */
			(0 << 11) |
			/* data sel: afbc0 or vd1 */
			((afbc ? 1 : 0) << 10) |
			/* afbc0 to vd1 (not di) */
			((di_afbc ? 1 : 0) << 9) |
			/* vd1 mif to vpp (not di) */
			(0 << 8) |
			/* afbc0 gclk ctrl */
			(0 << 0),
			0, 22);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1))
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VD1_AFBCD0_MISC_CTRL,
				/* Vd1_afbc0_mem_sel */
				(afbc ? 1 : 0),
				22, 1);
		if (di_post || di_pre_link) {
			/* check di_vpp_out_en bit */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VD1_AFBCD0_MISC_CTRL,
				/* vd1 mif to di */
				1,
				8, 2);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VD1_AFBCD0_MISC_CTRL,
				/* go field select di post */
				1,
				20, 2);
		}
	} else {
		if (!di_post)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VIU_MISC_CTRL0 + misc_off,
				0, 16, 3);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VIU_MISC_CTRL0 + misc_off,
			(afbc ? 1 : 0), 20, 1);
	}
}

static void vdx_path_select(struct video_layer_s *layer,
			    bool afbc, bool di_afbc)
{
	u32 misc_off = layer->misc_reg_offt;
	u8 vpp_index;

	vpp_index = layer->vpp_index;
	if (cur_dev->display_module == T7_DISPLAY_MODULE)
		return;

	if (!legacy_vpp) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD2_AFBCD1_MISC_CTRL,
			/* go field sel */
			(0 << 20) |
			/* linebuffer en */
			(0 << 16) |
			/* TODO: vd2 -> dolby -> vpp top ?? */
			(0 << 14) |
			/* axi sel: vd2 mif */
			(((afbc || di_afbc) ? 1 : 0) << 12) |
			/* data sel: vd2 & afbc1 (not osd4) */
			(0 << 11) |
			/* data sel: afbc1 */
			((afbc ? 1 : 0) << 10) |
			/* afbc1 to vd2 (not di) */
			((di_afbc ? 1 : 0)  << 9) |
			/* vd2 mif to vpp (not di) */
			(0 << 8) |
			/* afbc1 gclk ctrl */
			(0 << 0),
			0, 22);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1))
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VD2_AFBCD1_MISC_CTRL,
				/* Vd2_afbc0_mem_sel */
				(afbc ? 1 : 0),
				22, 1);
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VIU_MISC_CTRL1 + misc_off,
			(afbc ? 2 : 0), 0, 2);
	}
}

static void set_vd_mif_linear_cs(struct video_layer_s *layer,
				   struct canvas_s *cs0,
				   struct canvas_s *cs1,
				   struct canvas_s *cs2,
				   struct vframe_s *vf,
				   u32 lr_select)
{
	u32 y_line_stride;
	u32 c_line_stride;
	int y_buffer_width, c_buffer_width;
	u64 baddr_y, baddr_cb, baddr_cr;
	struct hw_vd_linear_reg_s *vd_mif_linear_reg = &layer->vd_mif_linear_reg;
	u8 vpp_index;
	u32 vd_if_baddr_y, vd_if_baddr_cb, vd_if_baddr_cr;
	u32 vd_if_stride_0, vd_if_stride_1;

	if (!lr_select) {
		vd_if_baddr_y = vd_mif_linear_reg->vd_if0_baddr_y;
		vd_if_baddr_cb = vd_mif_linear_reg->vd_if0_baddr_cb;
		vd_if_baddr_cr = vd_mif_linear_reg->vd_if0_baddr_cr;
		vd_if_stride_0 = vd_mif_linear_reg->vd_if0_stride_0;
		vd_if_stride_1 = vd_mif_linear_reg->vd_if0_stride_1;
	} else {
		vd_if_baddr_y = vd_mif_linear_reg->vd_if0_baddr_y_f1;
		vd_if_baddr_cb = vd_mif_linear_reg->vd_if0_baddr_cb_f1;
		vd_if_baddr_cr = vd_mif_linear_reg->vd_if0_baddr_cr_f1;
		vd_if_stride_0 = vd_mif_linear_reg->vd_if0_stride_0_f1;
		vd_if_stride_1 = vd_mif_linear_reg->vd_if0_stride_1_f1;
	}
	vpp_index = layer->vpp_index;
	if ((vf->type & VIDTYPE_VIU_444) ||
		    (vf->type & VIDTYPE_RGB_444) ||
		    (vf->type & VIDTYPE_VIU_422)) {
		baddr_y = cs0->addr;
		y_buffer_width = cs0->width;
		y_line_stride = viu_line_stride(y_buffer_width);
		baddr_cb = 0;
		baddr_cr = 0;
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_y,
			baddr_y >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cb,
			baddr_cb >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cr,
			baddr_cr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_stride_0,
			y_line_stride | 0 << 16);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_if_stride_1,
			1 << 16 | 0, 0, 16);
	} else {
		baddr_y = cs0->addr;
		y_buffer_width = cs0->width;
		baddr_cb = cs1->addr;
		c_buffer_width = cs1->width;
		baddr_cr = cs2->addr;

		y_line_stride = viu_line_stride(y_buffer_width);
		c_line_stride = viu_line_stride(c_buffer_width);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_y,
			baddr_y >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cb,
			baddr_cb >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cr,
			baddr_cr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_stride_0,
			y_line_stride | c_line_stride << 16);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_if_stride_1,
			1 << 16 | c_line_stride, 0, 16);
	}
}

static void set_vd_mif_linear(struct video_layer_s *layer,
				   struct canvas_config_s *config,
				   u32 planes,
				   struct vframe_s *vf,
				   u32 lr_select)
{
	u32 y_line_stride;
	u32 c_line_stride;
	int y_buffer_width, c_buffer_width;
	u64 baddr_y, baddr_cb, baddr_cr;
	struct hw_vd_linear_reg_s *vd_mif_linear_reg = &layer->vd_mif_linear_reg;
	struct canvas_config_s *cfg = config;
	u8 vpp_index;
	u32 vd_if_baddr_y, vd_if_baddr_cb, vd_if_baddr_cr;
	u32 vd_if_stride_0, vd_if_stride_1;

	vpp_index = layer->vpp_index;
	if (!lr_select) {
		vd_if_baddr_y = vd_mif_linear_reg->vd_if0_baddr_y;
		vd_if_baddr_cb = vd_mif_linear_reg->vd_if0_baddr_cb;
		vd_if_baddr_cr = vd_mif_linear_reg->vd_if0_baddr_cr;
		vd_if_stride_0 = vd_mif_linear_reg->vd_if0_stride_0;
		vd_if_stride_1 = vd_mif_linear_reg->vd_if0_stride_1;
	} else {
		vd_if_baddr_y = vd_mif_linear_reg->vd_if0_baddr_y_f1;
		vd_if_baddr_cb = vd_mif_linear_reg->vd_if0_baddr_cb_f1;
		vd_if_baddr_cr = vd_mif_linear_reg->vd_if0_baddr_cr_f1;
		vd_if_stride_0 = vd_mif_linear_reg->vd_if0_stride_0_f1;
		vd_if_stride_1 = vd_mif_linear_reg->vd_if0_stride_1_f1;
	}
	switch (planes) {
	case 1:
		baddr_y = cfg->phy_addr;
		y_buffer_width = cfg->width;
		y_line_stride = viu_line_stride(y_buffer_width);
		baddr_cb = 0;
		baddr_cr = 0;
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_y,
			baddr_y >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cb,
			baddr_cb >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cr,
			baddr_cr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_stride_0,
			y_line_stride | 0 << 16);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_if_stride_1,
			1 << 16 | 0, 0, 16);
		break;
	case 2:
		baddr_y = cfg->phy_addr;
		y_buffer_width = cfg->width;
		cfg++;
		baddr_cb = cfg->phy_addr;
		c_buffer_width = cfg->width;
		baddr_cr = 0;
		y_line_stride = viu_line_stride(y_buffer_width);
		c_line_stride = viu_line_stride(c_buffer_width);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_y,
			baddr_y >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cb,
			baddr_cb >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cr,
			baddr_cr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_stride_0,
			y_line_stride | c_line_stride << 16);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_if_stride_1,
			1 << 16 | c_line_stride, 0, 16);
		break;
	case 3:
		baddr_y = cfg->phy_addr;
		y_buffer_width = cfg->width;
		cfg++;
		baddr_cb = cfg->phy_addr;
		c_buffer_width = cfg->width;
		cfg++;
		baddr_cr = cfg->phy_addr;
		y_line_stride = viu_line_stride(y_buffer_width);
		c_line_stride = viu_line_stride(c_buffer_width);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_y,
			baddr_y >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cb,
			baddr_cb >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_baddr_cr,
			baddr_cr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_if_stride_0,
			y_line_stride | c_line_stride << 16);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_if_stride_1,
			1 << 16 | c_line_stride, 0, 16);
		break;
	default:
		pr_err("error planes=%d\n", planes);
		break;
	}
}

static void vd_set_blk_mode(struct video_layer_s *layer, u8 block_mode)
{
	struct hw_vd_reg_s *vd_mif_reg = &layer->vd_mif_reg;
	u32 pic_32byte_aligned = 0;
	u8 vpp_index;

	vpp_index = layer->vpp_index;
	cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_mif_reg->vd_if0_gen_reg3,
		block_mode, 12, 2);
	cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_mif_reg->vd_if0_gen_reg3,
		block_mode, 14, 2);
	if (block_mode)
		pic_32byte_aligned = 7;
	cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_mif_reg->vd_if0_gen_reg3,
		(pic_32byte_aligned << 6) |
		(block_mode << 4) |
		(block_mode << 2) |
		(block_mode << 0),
		18,
		9);
	/* VD1_IF0_STRIDE_1_F1 bit31:18 same as vd_if0_gen_reg3 */
	if (process_3d_type & MODE_3D_ENABLE) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(layer->vd_mif_linear_reg.vd_if0_stride_1_f1,
			(pic_32byte_aligned << 6) |
			(block_mode << 4) |
			(block_mode << 2) |
			(block_mode << 0),
			18,
			9);
	}
}

static void set_vd1_vd2_mux_reg(u8 vpp_index)
{
	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_INPUT_CTRL, 0x280);
}

static void set_vd1_vd2_unmux_reg(u8 vpp_index)
{
	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_INPUT_CTRL, 0x440);
}

static void vd1_set_dcu(struct video_layer_s *layer,
			struct vpp_frame_par_s *frame_par,
			struct vframe_s *vf)
{
	u32 r;
	u32 vphase, vini_phase, vformatter, vrepeat, hphase = 0;
	u32 hformatter;
	u32 pat, loop;
	static const u32 vpat[MAX_VSKIP_COUNT + 1] = {
		0, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
	u32 u, v;
	u32 type, bit_mode = 0, canvas_w;
	bool is_mvc = false;
	u8 burst_len = 1;
	struct hw_vd_reg_s *vd_mif_reg = &layer->vd_mif_reg;
	struct hw_vd_reg_s *vd2_mif_reg = &vd_layer[1].vd_mif_reg;
	struct hw_afbc_reg_s *vd_afbc_reg = &layer->vd_afbc_reg;
	bool di_post = false, di_pre_link = false;
	u8 vpp_index = layer->vpp_index;
	bool skip_afbc = false;

	if (!vf) {
		pr_info("%s vf NULL, return\n", __func__);
		return;
	}

	type = vf->type;
#ifdef ENABLE_PRE_LINK
	if (video_is_meson_t5d_revb_cpu() &&
	    !layer->vd1_vd2_mux &&
	    !is_local_vf(vf) &&
	    is_pre_link_on(layer) &&
	    is_pre_link_source(vf)) {
		struct vframe_s *dec_vf;

		if (vf->uvm_vf)
			dec_vf = vf->uvm_vf;
		else
			dec_vf = (struct vframe_s *)vf->vf_ext;
		if (dec_vf && (dec_vf->type & VIDTYPE_COMPRESS)) {
			type |= VIDTYPE_COMPRESS;
			skip_afbc = true;
		}
	}
#endif

	if (type & VIDTYPE_MVC)
		is_mvc = true;

	pr_debug("%s for vd%d %p, type:0x%x, flag:%x\n",
		 __func__, layer->layer_id, vf, type, vf->flag);
	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		if (layer->vd1_vd2_mux) {
			vd_mif_reg = &vd_layer[1].vd_mif_reg;
			vd_afbc_reg = &vd_layer[1].vd_afbc_reg;
			set_vd1_vd2_mux_reg(vpp_index);
		} else {
			set_vd1_vd2_unmux_reg(vpp_index);
		}
	}

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if (is_di_post_mode(vf) && is_di_post_on())
		di_post = true;
#ifdef ENABLE_PRE_LINK
	if (is_pre_link_on(layer))
		di_pre_link = true;
#endif
#endif

	if (frame_par->nocomp)
		type &= ~VIDTYPE_COMPRESS;
/*
 * not dead code,just for T5DB enable
 */
/*coverity[dead_error_line:SUPPRESS]*/
	if (skip_afbc && (type & VIDTYPE_COMPRESS)) {
		vd1_path_select(layer, true, false, di_post, di_pre_link);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VD1_AFBCD0_MISC_CTRL,
			/* vd1 afbc to di */
			1, 1, 1);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_gen_reg, 0);
		return;
	} else if (type & VIDTYPE_COMPRESS) {
		if (!legacy_vpp || is_meson_txlx_cpu())
			burst_len = 2;
		r = (3 << 24) |
			(vpp_hold_line[vpp_index] << 16) |
			(burst_len << 14) | /* burst1 */
			(vf->bitdepth & BITDEPTH_MASK);

		if (for_amdv_certification()) {
			if (frame_par->hscale_skip_count)
				r |= 0x11;
			if (frame_par->vscale_skip_count)
				r |= 0x44;
		} else {
			if (frame_par->hscale_skip_count)
				r |= 0x33;
			if (frame_par->vscale_skip_count)
				r |= 0xcc;
		}

		/* FIXME: don't use glayer_info[0].reverse */
		if (glayer_info[0].reverse)
			r |= (1 << 26) | (1 << 27);
		else if (glayer_info[0].mirror == H_MIRROR)
			r |= (1 << 26) | (0 << 27);
		else if (glayer_info[0].mirror == V_MIRROR)
			r |= (0 << 26) | (1 << 27);

		if (vf->bitdepth & BITDEPTH_SAVING_MODE)
			r |= (1 << 28); /* mem_saving_mode */
		if (type & VIDTYPE_SCATTER)
			r |= (1 << 29);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_mode, r);

		r = 0x1700;
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if (vf &&
			    (vf->source_type != VFRAME_SOURCE_TYPE_HDMI &&
			    !IS_DI_POSTWRTIE(vf->type)))
				r |= (1 << 19); /* dos_uncomp */
			if (type & VIDTYPE_COMB_MODE)
				r |= (1 << 20);
		}
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_enable, r);

		r = conv_lbuf_len[layer->layer_id];
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if ((type & VIDTYPE_VIU_444) ||
			    (type & VIDTYPE_RGB_444))
				r |= 0;
			else if (type & VIDTYPE_VIU_422)
				r |= (1 << 12);
			else
				r |= (2 << 12);
		}
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_conv_ctrl, r);

		u = (vf->bitdepth >> (BITDEPTH_U_SHIFT)) & 0x3;
		v = (vf->bitdepth >> (BITDEPTH_V_SHIFT)) & 0x3;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_dec_def_color,
			0x3FF00000 | /*Y,bit20+*/
			0x80 << (u + 10) |
			0x80 << v);
		/* chroma formatter */
		r = HFORMATTER_REPEAT |
			HFORMATTER_YC_RATIO_2_1 |
			HFORMATTER_EN |
			(0x8 << VFORMATTER_PHASE_BIT) |
			VFORMATTER_EN;
		if (is_dovi_tv_on())
			r |= VFORMATTER_ALWAYS_RPT |
				(0x0 << VFORMATTER_INIPHASE_BIT);
		else
			r |= VFORMATTER_RPTLINE0_EN |
				(0xc << VFORMATTER_INIPHASE_BIT);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if ((type & VIDTYPE_VIU_444) ||
			    (type & VIDTYPE_RGB_444)) {
				r &= ~HFORMATTER_EN;
				r &= ~VFORMATTER_EN;
				r &= ~HFORMATTER_YC_RATIO_2_1;
			} else if (type & VIDTYPE_VIU_422) {
				r &= ~VFORMATTER_EN;
			}
		}
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_vd_cfmt_ctrl, r);

		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if (type & VIDTYPE_COMPRESS_LOSS)
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_afbc_reg->afbcdec_iquant_enable,
					((1 << 11) |
					(1 << 10) |
					(1 << 4) |
					(1 << 0)));
			else
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_afbc_reg->afbcdec_iquant_enable, 0);
		}
		vd1_path_select(layer, true, false, di_post, di_pre_link);
		if (is_mvc)
			vdx_path_select(layer, true, false);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_gen_reg, 0);
		return;
	}

	/* DI only output NV21 8bit DW buffer */
	if (frame_par->nocomp &&
	    vf->plane_num == 2 &&
	    (vf->flag & VFRAME_FLAG_DI_DW)) {
		type &= ~(VIDTYPE_VIU_SINGLE_PLANE |
			VIDTYPE_VIU_NV12 |
			VIDTYPE_VIU_422 |
			VIDTYPE_VIU_444 |
			VIDTYPE_RGB_444);
		type |= VIDTYPE_VIU_NV21;
	}

	if (cur_dev->display_module == T7_DISPLAY_MODULE)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_afbc_reg->afbc_top_ctrl,
			0, 13, 2);

	/* vd mif burst len is 2 as default */
	burst_len = 2;
	if (vf->canvas0Addr != (u32)-1)
		canvas_w = canvas_get_width
			(vf->canvas0Addr & 0xff);
	else
		canvas_w = vf->canvas0_config[0].width;

	if (canvas_w % 32)
		burst_len = 0;
	else if (canvas_w % 64)
		burst_len = 1;
	if (layer->mif_setting.block_mode)
		burst_len = layer->mif_setting.block_mode;
	if ((vf->bitdepth & BITDEPTH_Y10) &&
	    !(vf->flag & VFRAME_FLAG_DI_DW) &&
	    !frame_par->nocomp) {
		if ((vf->type & VIDTYPE_VIU_444) ||
		    (vf->type & VIDTYPE_RGB_444)) {
			bit_mode = 2;
		} else {
			if (vf->bitdepth & FULL_PACK_422_MODE)
				bit_mode = 3;
			else
				bit_mode = 1;
		}
	} else {
		bit_mode = 0;
	}

	if (!legacy_vpp) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(bit_mode & 0x3), 8, 2);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(burst_len & 0x3), 1, 2);
		if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				0, 0, 1);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				1, 0, 1);
		vd_set_blk_mode(layer, layer->mif_setting.block_mode);
		if (is_mvc) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd2_mif_reg->vd_if0_gen_reg3,
				(bit_mode & 0x3), 8, 2);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd2_mif_reg->vd_if0_gen_reg3,
				(burst_len & 0x3), 1, 2);
			if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg3,
					0, 0, 1);
			else
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg3,
					1, 0, 1);
			vd_set_blk_mode(&vd_layer[1], layer->mif_setting.block_mode);
		}
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(bit_mode & 0x3), 8, 2);
		if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				0, 0, 1);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				1, 0, 1);

		if (is_mvc) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd2_mif_reg->vd_if0_gen_reg3,
				(bit_mode & 0x3), 8, 2);
			if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg3,
					0, 0, 1);
			else
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg3,
					1, 0, 1);
		}
	}
#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if (is_di_post_mode(vf)) {
		DI_POST_WR_REG_BITS
		(DI_IF1_GEN_REG3,
		(bit_mode & 0x3), 8, 2);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TXLX))
			DI_POST_WR_REG_BITS
				(DI_IF2_GEN_REG3,
				(bit_mode & 0x3), 8, 2);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12A))
			DI_POST_WR_REG_BITS
				(DI_IF0_GEN_REG3,
				(bit_mode & 0x3), 8, 2);
	}
#endif
	if (glayer_info[0].need_no_compress ||
	    (vf->type & VIDTYPE_PRE_DI_AFBC)) {
		vd1_path_select(layer, false, true, di_post, di_pre_link);
	} else {
		vd1_path_select(layer, false, false, di_post, di_pre_link);
		if (is_mvc)
			vdx_path_select(layer, false, false);
		if (!layer->vd1_vd2_mux &&
			cur_dev->display_module != C3_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_enable, 0);
	}

	if (cur_dev->display_module != C3_DISPLAY_MODULE)
		r = (3 << VDIF_URGENT_BIT) |
			(vpp_hold_line[vpp_index] << VDIF_HOLD_LINES_BIT) |
			VDIF_FORMAT_SPLIT |
			VDIF_CHRO_RPT_LAST | VDIF_ENABLE;
	else
		r = (3 << VDIF_URGENT_BIT) |
			VDIF_LUMA_END_AT_LAST_LINE |
			(vpp_hold_line[vpp_index] << VDIF_HOLD_LINES_BIT) |
			VDIF_LAST_LINE |
			VDIF_FORMAT_SPLIT |
			(2 << VDIF_BURSTSIZE_Y_BIT) |
			(2 << VDIF_BURSTSIZE_CB_BIT) |
			(2 << VDIF_BURSTSIZE_CR_BIT) |
			VDIF_CHRO_RPT_LAST | VDIF_ENABLE;
			/* add no dummy data(bit25), push dummy pixel(bit18) */
	/*  | VDIF_RESET_ON_GO_FIELD;*/
	if (layer->global_debug & DEBUG_FLAG_GOFIELD_MANUL)
		r |= 1 << 7; /*for manul trigger gofiled.*/

	if ((type & VIDTYPE_VIU_SINGLE_PLANE) == 0) {
		r |= VDIF_SEPARATE_EN;
	} else {
		if (type & VIDTYPE_VIU_422)
			r |= VDIF_FORMAT_422;
		else
			r |= VDIF_FORMAT_RGB888_YUV444 |
			    VDIF_DEMUX_MODE_RGB_444;
	}

	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		if (frame_par->hscale_skip_count) {
			if ((type & VIDTYPE_VIU_444) || (type & VIDTYPE_RGB_444))
				r |= VDIF_LUMA_HZ_AVG;
			else
				r |= VDIF_CHROMA_HZ_AVG | VDIF_LUMA_HZ_AVG;
		}
	}
	if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
		r |= (1 << 4);

	/*enable go field reset default according to vlsi*/
	r |= VDIF_RESET_ON_GO_FIELD;
	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_gen_reg, r);
	if (is_mvc)
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_gen_reg, r);

	if (type & VIDTYPE_VIU_NV21)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2, 1, 0, 2);
	else if (type & VIDTYPE_VIU_NV12)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2, 2, 0, 2);
	else
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2, 0, 0, 2);

	/* FIXME: don't use glayer_info[0].reverse */
	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		if (glayer_info[0].reverse) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0xf, 2, 4);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg2, 0xf, 2, 4);
		} else if (glayer_info[0].mirror == H_MIRROR) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0x5, 2, 4);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg2, 0x5, 2, 4);
		} else if (glayer_info[0].mirror == V_MIRROR) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0xa, 2, 4);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg2, 0xa, 2, 4);
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0, 2, 4);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd2_mif_reg->vd_if0_gen_reg2, 0, 2, 4);
		}
	} else {
		if (glayer_info[0].reverse)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0x3, 2, 2);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg2, 0x0, 2, 2);
	}
	/* chroma formatter */
	if ((type & VIDTYPE_VIU_444) ||
	    (type & VIDTYPE_RGB_444)) {
		r = HFORMATTER_YC_RATIO_1_1;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->viu_vd_fmt_ctrl, r);
		if (is_mvc)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->viu_vd_fmt_ctrl, r);
	} else {
		/* TODO: if always use HFORMATTER_REPEAT */
		if (is_crop_left_odd(frame_par)) {
			if ((type & VIDTYPE_VIU_NV21) ||
			     (type & VIDTYPE_VIU_NV12) ||
			    (type & VIDTYPE_VIU_422))
				hphase = 0x8 << HFORMATTER_PHASE_BIT;
		}
		if (frame_par->hscale_skip_count &&
		    (type & VIDTYPE_VIU_422))
			hformatter =
			(HFORMATTER_YC_RATIO_2_1 |
			hphase);
		else
			hformatter =
				(HFORMATTER_YC_RATIO_2_1 |
				hphase |
				HFORMATTER_EN);
		if (is_amdv_on())
			hformatter |= HFORMATTER_REPEAT;
		vrepeat = VFORMATTER_RPTLINE0_EN;
		vini_phase = (0xc << VFORMATTER_INIPHASE_BIT);
		if (type & VIDTYPE_VIU_422) {
			vformatter = 0;
			vphase = (0x10 << VFORMATTER_PHASE_BIT);
		} else {
			/*vlsi suggest only for yuv420 vformatter should be 1*/
			vformatter = VFORMATTER_EN;
			vphase = (0x08 << VFORMATTER_PHASE_BIT);
		}
		if (is_dovi_tv_on()) {
			/* dolby vision tv mode */
			vini_phase = (0 << VFORMATTER_INIPHASE_BIT);
			/* TODO: check the vrepeat */
			if (type & VIDTYPE_VIU_422)
				vrepeat = VFORMATTER_RPTLINE0_EN;
			else
				vrepeat = VFORMATTER_ALWAYS_RPT;
		} else if (is_mvc) {
			/* mvc source */
			vini_phase = (0xe << VFORMATTER_INIPHASE_BIT);
		} else if (type & VIDTYPE_TYPEMASK) {
			/* interlace source */
			if ((type & VIDTYPE_TYPEMASK) ==
			     VIDTYPE_INTERLACE_TOP)
				vini_phase =
					(0xe << VFORMATTER_INIPHASE_BIT);
			else
				vini_phase =
					(0xa << VFORMATTER_INIPHASE_BIT);
		}
		vformatter |= (vphase | vini_phase | vrepeat);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->viu_vd_fmt_ctrl,
			hformatter | vformatter);

		if (is_mvc) {
			vini_phase = (0xa << VFORMATTER_INIPHASE_BIT);
			vformatter = (vphase | vini_phase
				| vrepeat | VFORMATTER_EN);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->viu_vd_fmt_ctrl,
				hformatter | vformatter);
		}
	}

	if (is_meson_txlx_cpu()	||
	    cpu_after_eq(MESON_CPU_MAJOR_ID_G12A))
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->viu_vd_fmt_ctrl,
			1, 29, 1);

	/* LOOP/SKIP pattern */
	pat = vpat[frame_par->vscale_skip_count];

	if (type & VIDTYPE_VIU_FIELD) {
		loop = 0;

		if (type & VIDTYPE_INTERLACE)
			pat = vpat[frame_par->vscale_skip_count >> 1];
	} else if (is_mvc) {
		loop = 0x11;
		if (framepacking_support)
			pat = 0;
		else
			pat = 0x80;
	} else if ((type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP) {
		loop = 0x11;
		pat <<= 4;
	} else {
		loop = 0;
	}

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_rpt_loop,
		(loop << VDIF_CHROMA_LOOP1_BIT) |
		(loop << VDIF_LUMA_LOOP1_BIT) |
		(loop << VDIF_CHROMA_LOOP0_BIT) |
		(loop << VDIF_LUMA_LOOP0_BIT));
	if (is_mvc)
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_rpt_loop,
			(loop << VDIF_CHROMA_LOOP1_BIT) |
			(loop << VDIF_LUMA_LOOP1_BIT) |
			(loop << VDIF_CHROMA_LOOP0_BIT) |
			(loop << VDIF_LUMA_LOOP0_BIT));

	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_luma0_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_chroma0_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_luma1_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_chroma1_rpt_pat, pat);

	if (is_mvc) {
		if (framepacking_support)
			pat = 0;
		else
			pat = 0x88;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_luma0_rpt_pat, pat);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_chroma0_rpt_pat, pat);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_luma1_rpt_pat, pat);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd2_mif_reg->vd_if0_chroma1_rpt_pat, pat);
	}

	/* picture 0/1 control */
	if ((((type & VIDTYPE_INTERLACE) == 0) &&
	     ((type & VIDTYPE_VIU_FIELD) == 0) &&
	     ((type & VIDTYPE_MVC) == 0)) ||
	    (frame_par->vpp_2pic_mode & 0x3)) {
		/* progressive frame in two pictures */
		r = ((2 << 26) | /* two pic mode */
			(2 << 24) | /* use own last line */
			0x01); /* loop pattern */
		if (frame_par->vpp_2pic_mode & VPP_PIC1_FIRST)
			r |= (1 << 8); /* use pic1 first*/
		else
			r |= (2 << 8);	 /* use pic0 first */
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_luma_psel, r);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_chroma_psel, r);
	} else if (process_3d_type & MODE_3D_OUT_FA_MASK) {
		/*FA LR/TB output , do nothing*/
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_luma_psel, 0);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_chroma_psel, 0);
		if (is_mvc) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_luma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_chroma_psel, 0);
		}
	}
}

static void vdx_set_dcu(struct video_layer_s *layer,
			struct vpp_frame_par_s *frame_par,
			struct vframe_s *vf)
{
	u32 r;
	u32 vphase, vini_phase, vformatter, vrepeat, hphase = 0;
	u32 hformatter;
	u32 pat, loop;
	static const u32 vpat[MAX_VSKIP_COUNT + 1] = {
		0, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
	u32 u, v;
	u32 type, bit_mode = 0, canvas_w;
	bool is_mvc = false;
	u8 burst_len = 1;
	struct hw_vd_reg_s *vd_mif_reg = &layer->vd_mif_reg;
	struct hw_afbc_reg_s *vd_afbc_reg = &layer->vd_afbc_reg;
	int layer_id = layer->layer_id;
	u8 vpp_index = layer->vpp_index;

	if (!vf) {
		pr_err("%s vf is NULL\n", __func__);
		return;
	}
	if (!glayer_info[layer_id].layer_support)
		return;

	type = vf->type;
	if (type & VIDTYPE_MVC)
		is_mvc = true;
	pr_debug("%s for vd%d %p, type:0x%x, flag:%x\n",
		 __func__, layer->layer_id, vf, type, vf->flag);

	if (frame_par->nocomp)
		type &= ~VIDTYPE_COMPRESS;

	if (type & VIDTYPE_COMPRESS) {
		if (cur_dev->display_module == T7_DISPLAY_MODULE) {
			if (conv_lbuf_len[layer->layer_id] == VIDEO_USE_4K_RAM)
				r = 3;
			else
				r = 1;

			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_afbc_reg->afbc_top_ctrl,
				 r, 13, 2);
		}

		if (!legacy_vpp || is_meson_txlx_cpu())
			burst_len = 2;
		r = (3 << 24) |
		    (vpp_hold_line[vpp_index] << 16) |
		    (burst_len << 14) | /* burst1 */
		    (vf->bitdepth & BITDEPTH_MASK);

		if (for_amdv_certification()) {
			if (frame_par->hscale_skip_count)
				r |= 0x11;
			if (frame_par->vscale_skip_count)
				r |= 0x44;
		} else {
			if (frame_par->hscale_skip_count)
				r |= 0x33;
			if (frame_par->vscale_skip_count)
				r |= 0xcc;
		}

		/* FIXME: don't use glayer_info[1].reverse */
		if (glayer_info[layer_id].reverse)
			r |= (1 << 26) | (1 << 27);
		else if (glayer_info[layer_id].mirror == H_MIRROR)
			r |= (1 << 26) | (0 << 27);
		else if (glayer_info[layer_id].mirror == V_MIRROR)
			r |= (0 << 26) | (1 << 27);

		if (vf->bitdepth & BITDEPTH_SAVING_MODE)
			r |= (1 << 28); /* mem_saving_mode */
		if (type & VIDTYPE_SCATTER)
			r |= (1 << 29);
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_mode, r);

		r = 0x1700;
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if (vf &&
			    (vf->source_type != VFRAME_SOURCE_TYPE_HDMI &&
			    !IS_DI_POSTWRTIE(vf->type)))
				r |= (1 << 19); /* dos_uncomp */
			if (type & VIDTYPE_COMB_MODE)
				r |= (1 << 20);
		}
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_enable, r);

		r = conv_lbuf_len[layer_id];
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if ((type & VIDTYPE_VIU_444) ||
			    (type & VIDTYPE_RGB_444))
				r |= 0;
			else if (type & VIDTYPE_VIU_422)
				r |= (1 << 12);
			else
				r |= (2 << 12);
		}
		cur_dev->rdma_func[vpp_index].rdma_wr(vd_afbc_reg->afbc_conv_ctrl, r);

		u = (vf->bitdepth >> (BITDEPTH_U_SHIFT)) & 0x3;
		v = (vf->bitdepth >> (BITDEPTH_V_SHIFT)) & 0x3;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_dec_def_color,
			0x3FF00000 | /*Y,bit20+*/
			0x80 << (u + 10) |
			0x80 << v);

		/* chroma formatter */
		r = HFORMATTER_YC_RATIO_2_1 |
			HFORMATTER_EN |
			(0x8 << VFORMATTER_PHASE_BIT) |
			VFORMATTER_EN;
		if (is_dovi_tv_on())
			r |= HFORMATTER_REPEAT |
				VFORMATTER_ALWAYS_RPT |
				(0x0 << VFORMATTER_INIPHASE_BIT);
		else if (is_amdv_on()) /* stb case */
			r |= HFORMATTER_REPEAT |
				VFORMATTER_RPTLINE0_EN |
				(0xc << VFORMATTER_INIPHASE_BIT);
		else
			r |= HFORMATTER_RRT_PIXEL0 |
				VFORMATTER_RPTLINE0_EN |
				(0 << VFORMATTER_INIPHASE_BIT);
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if ((type & VIDTYPE_VIU_444) ||
			    (type & VIDTYPE_RGB_444)) {
				r &= ~HFORMATTER_EN;
				r &= ~VFORMATTER_EN;
				r &= ~HFORMATTER_YC_RATIO_2_1;
			} else if (type & VIDTYPE_VIU_422) {
				r &= ~VFORMATTER_EN;
			}
		}
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_vd_cfmt_ctrl, r);

		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
			if (type & VIDTYPE_COMPRESS_LOSS)
				cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbcdec_iquant_enable,
				((1 << 11) |
				(1 << 10) |
				(1 << 4) |
				(1 << 0)));
			else
				cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbcdec_iquant_enable, 0);
		}

		vdx_path_select(layer, true, false);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_gen_reg, 0);
		return;
	}

	/* DI only output NV21 8bit DW buffer */
	if (frame_par->nocomp &&
	    vf->plane_num == 2 &&
	    (vf->flag & VFRAME_FLAG_DI_DW)) {
		type &= ~(VIDTYPE_VIU_SINGLE_PLANE |
			VIDTYPE_VIU_NV12 |
			VIDTYPE_VIU_422 |
			VIDTYPE_VIU_444 |
			VIDTYPE_RGB_444);
		type |= VIDTYPE_VIU_NV21;
	}

	if (cur_dev->display_module == T7_DISPLAY_MODULE)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_afbc_reg->afbc_top_ctrl,
			0, 13, 2);

	/* vd mif burst len is 2 as default */
	burst_len = 2;
	if (vf->canvas0Addr != (u32)-1)
		canvas_w = canvas_get_width
			(vf->canvas0Addr & 0xff);
	else
		canvas_w = vf->canvas0_config[0].width;

	if (canvas_w % 32)
		burst_len = 0;
	else if (canvas_w % 64)
		burst_len = 1;
	if (layer->mif_setting.block_mode)
		burst_len = layer->mif_setting.block_mode;

	if ((vf->bitdepth & BITDEPTH_Y10) &&
	    !(vf->flag & VFRAME_FLAG_DI_DW) &&
	    !frame_par->nocomp) {
		if ((vf->type & VIDTYPE_VIU_444) ||
		    (vf->type & VIDTYPE_RGB_444)) {
			bit_mode = 2;
		} else {
			if (vf->bitdepth & FULL_PACK_422_MODE)
				bit_mode = 3;
			else
				bit_mode = 1;
		}
	} else {
		bit_mode = 0;
	}

	vdx_path_select(layer, false, false);
	if (!legacy_vpp) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(bit_mode & 0x3), 8, 2);
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(burst_len & 0x3), 1, 2);
		if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				0, 0, 1);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				1, 0, 1);
		vd_set_blk_mode(layer, layer->mif_setting.block_mode);
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg3,
			(bit_mode & 0x3), 8, 2);
		if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				0, 0, 1);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_mif_reg->vd_if0_gen_reg3,
				1, 0, 1);
	}
	if (!(cur_dev->rdma_func[vpp_index].rdma_rd(VIU_MISC_CTRL1) & 0x1))
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_enable, 0);

	r = (3 << VDIF_URGENT_BIT) |
		(vpp_hold_line[vpp_index] << VDIF_HOLD_LINES_BIT) |
		VDIF_FORMAT_SPLIT |
		VDIF_CHRO_RPT_LAST | VDIF_ENABLE;
	/*  | VDIF_RESET_ON_GO_FIELD;*/
	if (layer->global_debug & DEBUG_FLAG_GOFIELD_MANUL)
		r |= 1 << 7; /*for manul trigger gofiled.*/

	if ((type & VIDTYPE_VIU_SINGLE_PLANE) == 0) {
		r |= VDIF_SEPARATE_EN;
	} else {
		if (type & VIDTYPE_VIU_422)
			r |= VDIF_FORMAT_422;
		else
			r |= VDIF_FORMAT_RGB888_YUV444 |
			    VDIF_DEMUX_MODE_RGB_444;
	}

	if (frame_par->hscale_skip_count) {
		if ((type & VIDTYPE_VIU_444) || (type & VIDTYPE_RGB_444))
			r |= VDIF_LUMA_HZ_AVG;
		else
			r |= VDIF_CHROMA_HZ_AVG | VDIF_LUMA_HZ_AVG;
	}

	if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
		r |= (1 << 4);

	cur_dev->rdma_func[vpp_index].rdma_wr(vd_mif_reg->vd_if0_gen_reg, r);

	if (type & VIDTYPE_VIU_NV21)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			1, 0, 2);
	else if (type & VIDTYPE_VIU_NV12)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			2, 0, 2);
	else
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			0, 0, 2);

	/* FIXME: don't use glayer_info[1].reverse */
	if (glayer_info[layer_id].reverse)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			0xf, 2, 4);
	else if (glayer_info[layer_id].mirror == H_MIRROR)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			0x5, 2, 4);
	else if (glayer_info[layer_id].mirror == V_MIRROR)
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			0xa, 2, 4);
	else
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->vd_if0_gen_reg2,
			0, 2, 4);

	/* chroma formatter */
	if ((type & VIDTYPE_VIU_444) ||
	    (type & VIDTYPE_RGB_444)) {
		r = HFORMATTER_YC_RATIO_1_1;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->viu_vd_fmt_ctrl, r);
	} else {
		/* TODO: if always use HFORMATTER_REPEAT */
		if (is_crop_left_odd(frame_par)) {
			if ((type & VIDTYPE_VIU_NV21) ||
			     (type & VIDTYPE_VIU_NV12) ||
			    (type & VIDTYPE_VIU_422))
				hphase = 0x8 << HFORMATTER_PHASE_BIT;
		}
		if (frame_par->hscale_skip_count &&
		    (type & VIDTYPE_VIU_422))
			hformatter =
			(HFORMATTER_YC_RATIO_2_1 |
			hphase);
		else
			hformatter =
				(HFORMATTER_YC_RATIO_2_1 |
				hphase |
				HFORMATTER_EN);
		if (is_amdv_on())
			hformatter |= HFORMATTER_REPEAT;
		vrepeat = VFORMATTER_RPTLINE0_EN;
		vini_phase = (0xc << VFORMATTER_INIPHASE_BIT);
		if (type & VIDTYPE_VIU_422) {
			vformatter = 0;
			vphase = (0x10 << VFORMATTER_PHASE_BIT);
		} else {
			/*vlsi suggest only for yuv420 vformatter should be 1*/
			vformatter = VFORMATTER_EN;
			vphase = (0x08 << VFORMATTER_PHASE_BIT);
		}
		if (is_dovi_tv_on()) {
			/* dolby vision tv mode */
			vini_phase = (0 << VFORMATTER_INIPHASE_BIT);
			/* TODO: check the vrepeat */
			if (type & VIDTYPE_VIU_422)
				vrepeat = VFORMATTER_RPTLINE0_EN;
			else
				vrepeat = VFORMATTER_ALWAYS_RPT;
		} else if (is_mvc) {
			/* mvc source */
			/* vini_phase = (0xe << VFORMATTER_INIPHASE_BIT); */
			vini_phase = (0xa << VFORMATTER_INIPHASE_BIT);
		} else if (type & VIDTYPE_TYPEMASK) {
			/* interlace source */
			if ((type & VIDTYPE_TYPEMASK) ==
			    VIDTYPE_INTERLACE_TOP)
				vini_phase =
					(0xe << VFORMATTER_INIPHASE_BIT);
			else
				vini_phase =
					(0xa << VFORMATTER_INIPHASE_BIT);
		}
		vformatter |= (vphase | vini_phase | vrepeat);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->viu_vd_fmt_ctrl,
			hformatter | vformatter);
	}
	if (is_meson_txlx_cpu()	||
	    cpu_after_eq(MESON_CPU_MAJOR_ID_G12A))
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_mif_reg->viu_vd_fmt_ctrl,
			1, 29, 1);
	/* LOOP/SKIP pattern */
	pat = vpat[frame_par->vscale_skip_count];

	if (type & VIDTYPE_VIU_FIELD) {
		loop = 0;

		if (type & VIDTYPE_INTERLACE)
			pat = vpat[frame_par->vscale_skip_count >> 1];
	} else if (is_mvc) {
		loop = 0x11;
		pat = 0x80;
	} else if ((type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP) {
		loop = 0x11;
		pat <<= 4;
	} else {
		loop = 0;
	}

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_rpt_loop,
		(loop << VDIF_CHROMA_LOOP1_BIT) |
		(loop << VDIF_LUMA_LOOP1_BIT) |
		(loop << VDIF_CHROMA_LOOP0_BIT) |
		(loop << VDIF_LUMA_LOOP0_BIT));

	if (is_mvc)
		pat = 0x88;

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_luma0_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_chroma0_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_luma1_rpt_pat, pat);
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_chroma1_rpt_pat, pat);

	/* picture 0/1 control */
	if ((((type & VIDTYPE_INTERLACE) == 0) &&
	     ((type & VIDTYPE_VIU_FIELD) == 0) &&
	     ((type & VIDTYPE_MVC) == 0)) ||
	    (frame_par->vpp_2pic_mode & 0x3)) {
		/* progressive frame in two pictures */
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_luma_psel, 0);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_chroma_psel, 0);
	}
}

static void vd_afbc_setting_tl1(struct video_layer_s *layer, struct mif_pos_s *setting)
{
	int crop_left, crop_top;
	int vsize_in, hsize_in;
	int mif_blk_bgn_h, mif_blk_end_h;
	int mif_blk_bgn_v, mif_blk_end_v;
	int pix_bgn_h, pix_end_h;
	int pix_bgn_v, pix_end_v;
	struct hw_afbc_reg_s *vd_afbc_reg;
	u8 vpp_index, layer_id;

	if (!setting)
		return;
	layer_id = layer->layer_id;
	vpp_index = layer->vpp_index;

	vd_afbc_reg = setting->p_vd_afbc_reg;
	/* afbc horizontal setting */
	crop_left = setting->start_x_lines;
	hsize_in = round_up
		((setting->src_w - 1) + 1, 32);
	mif_blk_bgn_h = crop_left / 32;
	mif_blk_end_h = (crop_left + setting->end_x_lines -
		setting->start_x_lines) / 32;
	pix_bgn_h = crop_left - mif_blk_bgn_h * 32;
	pix_end_h = pix_bgn_h + setting->end_x_lines -
		setting->start_x_lines;

	if (((process_3d_type & MODE_3D_FA) ||
	     (process_3d_type & MODE_FORCE_3D_FA_LR)) &&
	    setting->vpp_3d_mode == 1) {
		/* do nothing*/
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_mif_hor_scope,
			(mif_blk_bgn_h << 16) |
			mif_blk_end_h);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_pixel_hor_scope,
			((pix_bgn_h << 16) |
			pix_end_h));
	}

	/* afbc vertical setting */
	crop_top = setting->start_y_lines;
	vsize_in = round_up((setting->src_h - 1) + 1, 4);
	mif_blk_bgn_v = crop_top / 4;
	mif_blk_end_v = (crop_top + setting->end_y_lines -
		setting->start_y_lines) / 4;
	pix_bgn_v = crop_top - mif_blk_bgn_v * 4;
	pix_end_v = pix_bgn_v + setting->end_y_lines -
		setting->start_y_lines;

	if (layer_id != 2 &&
	   ((process_3d_type & MODE_3D_FA) ||
	   (process_3d_type & MODE_FORCE_3D_FA_TB)) &&
	    setting->vpp_3d_mode == 2) {
		int block_h;

		block_h = vsize_in;
		block_h = block_h / 8;
		if (toggle_3d_fa_frame == OUT_FA_B_FRAME) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_mif_ver_scope,
				(block_h << 16) |
				(vsize_in / 4));
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_mif_ver_scope,
				(0 << 16) |
				block_h);
		}
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_mif_ver_scope,
			(mif_blk_bgn_v << 16) |
			mif_blk_end_v);
	}
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_pixel_ver_scope,
		(pix_bgn_v << 16) |
		pix_end_v);

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_size_in,
		(hsize_in << 16) |
		(vsize_in & 0xffff));
}

void vd_mif_setting(struct video_layer_s *layer,
			struct mif_pos_s *setting)
{
	u32 ls = 0, le = 0;
	u32 h_skip, v_skip, hc_skip, vc_skip;
	int l_aligned, r_aligned;
	int t_aligned, b_aligned, ori_t_aligned, ori_b_aligned;
	int content_w, content_l, content_r;
	struct hw_vd_reg_s *vd_mif_reg;
	struct hw_afbc_reg_s *vd_afbc_reg;
	u8 vpp_index, layer_id;

	if (!setting)
		return;
	layer_id = layer->layer_id;
	vpp_index = layer->vpp_index;
	vd_mif_reg = setting->p_vd_mif_reg;
	vd_afbc_reg = setting->p_vd_afbc_reg;
	h_skip = setting->h_skip + 1;
	v_skip = setting->v_skip + 1;
	hc_skip = setting->hc_skip;
	vc_skip = setting->vc_skip;

	/* vd horizontal setting */
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_luma_x0,
		(setting->l_hs_luma << VDIF_PIC_START_BIT) |
		(setting->l_he_luma << VDIF_PIC_END_BIT));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_chroma_x0,
		(setting->l_hs_chrm << VDIF_PIC_START_BIT) |
		(setting->l_he_chrm << VDIF_PIC_END_BIT));

	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_luma_x1,
			(setting->r_hs_luma  << VDIF_PIC_START_BIT) |
			(setting->r_he_luma  << VDIF_PIC_END_BIT));

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_chroma_x1,
			(setting->r_hs_chrm << VDIF_PIC_START_BIT) |
			(setting->r_he_chrm << VDIF_PIC_END_BIT));
	}
	ls = setting->start_x_lines;
	le = setting->end_x_lines;
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->viu_vd_fmt_w,
		(((le - ls + 1) / h_skip)
		<< VD1_FMT_LUMA_WIDTH_BIT) |
		(((le / 2 - ls / 2 + 1) / h_skip)
		<< VD1_FMT_CHROMA_WIDTH_BIT));

	/* vd vertical setting */
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_luma_y0,
		(setting->l_vs_luma << VDIF_PIC_START_BIT) |
		(setting->l_ve_luma << VDIF_PIC_END_BIT));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_mif_reg->vd_if0_chroma_y0,
		(setting->l_vs_chrm << VDIF_PIC_START_BIT) |
		(setting->l_ve_chrm << VDIF_PIC_END_BIT));

	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_luma_y1,
			(setting->r_vs_luma << VDIF_PIC_START_BIT) |
			(setting->r_ve_luma << VDIF_PIC_END_BIT));

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_chroma_y1,
			(setting->r_vs_chrm << VDIF_PIC_START_BIT) |
			(setting->r_ve_chrm << VDIF_PIC_END_BIT));
	}
	if (setting->skip_afbc ||
		cur_dev->display_module == C3_DISPLAY_MODULE)
		goto SKIP_VD1_AFBC;

	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_TL1)
		return vd_afbc_setting_tl1(layer, setting);

	/* afbc horizontal setting */
	if (setting->start_x_lines > 0 ||
	    (setting->end_x_lines < (setting->src_w - 1))) {
		/* also 0 line as start */
		l_aligned = round_down(0, 32);
		r_aligned = round_up
			((setting->src_w - 1) + 1, 32);
	} else {
		l_aligned = round_down
			(setting->start_x_lines, 32);
		r_aligned = round_up
			(setting->end_x_lines + 1, 32);
	}

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_vd_cfmt_w,
		(((r_aligned - l_aligned) / h_skip) << 16) |
		((r_aligned / 2 - l_aligned / hc_skip) / h_skip));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_mif_hor_scope,
		((l_aligned / 32) << 16) |
		((r_aligned / 32) - 1));

	if (setting->reverse) {
		content_w = setting->end_x_lines
			- setting->start_x_lines + 1;
		if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1))
			content_l = 0;
		else
			content_l = r_aligned - setting->end_x_lines - 1;
		content_r = content_l + content_w - 1;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_pixel_hor_scope,
			(((content_l << 16)) | content_r) / h_skip);
	} else {
		if (((process_3d_type & MODE_3D_FA) ||
		     (process_3d_type & MODE_FORCE_3D_FA_LR)) &&
		    setting->vpp_3d_mode == 1) {
			/* do nothing*/
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_pixel_hor_scope,
				(((setting->start_x_lines - l_aligned) << 16) |
				(setting->end_x_lines - l_aligned)) / h_skip);
		}
	}

	/* afbc vertical setting */
	t_aligned = round_down(setting->start_y_lines, 4);
	b_aligned = round_up(setting->end_y_lines + 1, 4);

	ori_t_aligned = round_down(0, 4);
	ori_b_aligned = round_up((setting->src_h - 1) + 1, 4);

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_vd_cfmt_h,
		(b_aligned - t_aligned) / vc_skip / v_skip);

	if (layer_id == 0 &&
	     ((process_3d_type & MODE_3D_FA) ||
	     (process_3d_type & MODE_FORCE_3D_FA_TB)) &&
	    setting->vpp_3d_mode == 2) {
		int block_h;

		block_h = ori_b_aligned - ori_t_aligned;
		block_h = block_h / 8;
		if (toggle_3d_fa_frame == OUT_FA_B_FRAME) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_mif_ver_scope,
				(((ori_t_aligned / 4) +  block_h) << 16) |
				((ori_b_aligned / 4)  - 1));
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_afbc_reg->afbc_mif_ver_scope,
				((ori_t_aligned / 4) << 16) |
				((ori_t_aligned / 4)  + block_h - 1));
		}
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_mif_ver_scope,
			((t_aligned / 4) << 16) |
			((b_aligned / 4) - 1));
	}

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_mif_ver_scope,
		((t_aligned / 4) << 16) |
		((b_aligned / 4) - 1));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_pixel_ver_scope,
		((setting->start_y_lines - t_aligned) << 16) |
		(setting->end_y_lines - t_aligned));

	/* TODO: if need remove this limit */
	/* afbc pixel vertical output region must be */
	/* [0, end_y_lines - start_y_lines] */
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_pixel_ver_scope,
		(setting->end_y_lines -
		setting->start_y_lines));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vd_afbc_reg->afbc_size_in,
		((r_aligned - l_aligned) << 16) |
		((ori_b_aligned - ori_t_aligned) & 0xffff));
	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXL) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_size_out,
			(((r_aligned - l_aligned) / h_skip) << 16) |
			(((b_aligned - t_aligned) / v_skip) & 0xffff));
	}
SKIP_VD1_AFBC:
	return;
}

static void vd1_scaler_setting(struct video_layer_s *layer, struct scaler_setting_s *setting)
{
	u32 misc_off, i;
	u32 r1, r2, r3;
	struct vpp_frame_par_s *frame_par;
	struct vppfilter_mode_s *vpp_filter;
	struct vppfilter_mode_s *aisr_vpp_filter = NULL;
	u32 hsc_init_rev_num0 = 4;
	struct hw_pps_reg_s *vd_pps_reg;
	u32 bit9_mode = 0, s11_mode = 0;
	u8 vpp_index;
	const u32 *vpp_vert_coeff;
	const u32 *vpp_horz_coeff;

	if (!setting || !setting->frame_par)
		return;

	vd_pps_reg = &layer->pps_reg;
	frame_par = setting->frame_par;
	misc_off = setting->misc_reg_offt;
	vpp_index = layer->vpp_index;
	/* vpp super scaler */
	vpu_module_clk_enable(vd_layer[0].vpp_index, SR0, 0);
	vpu_module_clk_enable(vd_layer[0].vpp_index, SR1, 0);
	vpp_set_super_scaler_regs
		(layer,
		frame_par->supscl_path,
		frame_par->supsc0_enable,
		frame_par->spsc0_w_in,
		frame_par->spsc0_h_in,
		frame_par->supsc0_hori_ratio,
		frame_par->supsc0_vert_ratio,
		frame_par->supsc1_enable,
		frame_par->spsc1_w_in,
		frame_par->spsc1_h_in,
		frame_par->supsc1_hori_ratio,
		frame_par->supsc1_vert_ratio,
		setting->vinfo_width,
		setting->vinfo_height);

	if (is_amdv_on() &&
	    is_amdv_stb_mode() &&
	    !frame_par->supsc0_enable &&
	    !frame_par->supsc1_enable) {
		cur_dev->rdma_func[vpp_index].rdma_wr(VPP_SRSHARP0_CTRL, 0);
		cur_dev->rdma_func[vpp_index].rdma_wr(VPP_SRSHARP1_CTRL, 0);
	}

	vpp_filter = &frame_par->vpp_filter;
	aisr_vpp_filter = &cur_dev->aisr_frame_parms.vpp_filter;
	if (setting->sc_top_enable) {
		u32 sc_misc_val;

		sc_misc_val = VPP_SC_TOP_EN | VPP_SC_V1OUT_EN;
		/* enable seprate luma chroma coef */
		if (cur_dev->scaler_sep_coef_en)
			sc_misc_val |= VPP_HF_SEP_COEF_4SRNET_EN;
		if (setting->sc_h_enable) {
			if (has_pre_hscaler_ntap(0)) {
				/* for sc2/t5 support hscaler 8 tap */
				if (pre_scaler[0].pre_hscaler_ntap_enable) {
					sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT)
					| VPP_SC_HORZ_EN);
				} else {
					sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT_OLD)
					| VPP_SC_HORZ_EN);
				}
			} else {
				sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT)
					| VPP_SC_HORZ_EN);
			}
			if (hscaler_8tap_enable[0]) {
				sc_misc_val |=
				((vpp_filter->vpp_horz_coeff[0] & 0xf)
					<< VPP_SC_HBANK_LENGTH_BIT);

			} else {
				sc_misc_val |=
					((vpp_filter->vpp_horz_coeff[0] & 7)
					<< VPP_SC_HBANK_LENGTH_BIT);
			}
		}

		if (setting->sc_v_enable) {
			sc_misc_val |= (((vpp_filter->vpp_pre_vsc_en & 1)
				<< VPP_SC_PREVERT_EN_BIT)
				| VPP_SC_VERT_EN);
			sc_misc_val |= ((vpp_filter->vpp_pre_vsc_en & 1)
				<< VPP_LINE_BUFFER_EN_BIT);
			sc_misc_val |= ((vpp_filter->vpp_vert_coeff[0] & 7)
				<< VPP_SC_VBANK_LENGTH_BIT);
		}

		if (setting->last_line_fix)
			sc_misc_val |= VPP_PPS_LAST_LINE_FIX;

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_sc_misc,
			sc_misc_val);
		vpu_module_clk_enable(vpp_index, VD1_SCALER, 0);
	} else {
		setting->sc_v_enable = false;
		setting->sc_h_enable = false;
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_sc_misc,
			0, VPP_SC_TOP_EN_BIT,
			VPP_SC_TOP_EN_WID);
		vpu_module_clk_disable(vpp_index, VD1_SCALER, 0);
	}

	/* horizontal filter settings */
	if (setting->sc_h_enable) {
		bit9_mode = vpp_filter->vpp_horz_coeff[1] & 0x8000;
		s11_mode = vpp_filter->vpp_horz_coeff[1] & 0x4000;
		if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x199, 12, 9);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x77, 12, 9);
		if (layer->aisr_mif_setting.aisr_enable &&
		   cur_dev->pps_auto_calc)
			vpp_horz_coeff = aisr_vpp_filter->vpp_horz_coeff;
		else
			vpp_horz_coeff = vpp_filter->vpp_horz_coeff;
		if (hscaler_8tap_enable[0]) {
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ | VPP_COEF_9BIT);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ |
						  VPP_COEF_VERT_CHROMA |
						  VPP_COEF_9BIT);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM * 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM * 3]);
				}
				if (cur_dev->scaler_sep_coef_en) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2]);
					}
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
					}
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA_PARTB |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2 +
						VPP_FILER_COEFS_NUM * 2]);
					}
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2 +
						VPP_FILER_COEFS_NUM * 3]);
					}
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2]);
				}
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ |
						  VPP_COEF_VERT_CHROMA);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
				if (cur_dev->scaler_sep_coef_en) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF);
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2]);
					}
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA_PARTB |
						VPP_SEP_COEF);
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
					}
				}
			}
		} else {
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ | VPP_COEF_9BIT);
				for (i = 0; i <
					(vpp_filter->vpp_horz_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
				if (cur_dev->scaler_sep_coef_en) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					for (i = 0; i <
						(vpp_filter->vpp_horz_coeff[1]
						& 0xff); i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2]);
					}
					for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
					}
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ);
				for (i = 0; i < (vpp_filter->vpp_horz_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_horz_coeff[i + 2]);
				}
				if (cur_dev->scaler_sep_coef_en) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef_idx,
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF);
					for (i = 0; i < (vpp_filter->vpp_horz_coeff[1]
						& 0xff); i++) {
						cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_horz_coeff[i + 2]);
					}
				}
			}
		}
		r1 = frame_par->VPP_hsc_linear_startp
			- frame_par->VPP_hsc_startp;
		r2 = frame_par->VPP_hsc_linear_endp
			- frame_par->VPP_hsc_startp;
		r3 = frame_par->VPP_hsc_endp
			- frame_par->VPP_hsc_startp;

		if (frame_par->supscl_path ==
		     CORE0_PPS_CORE1 ||
		    frame_par->supscl_path ==
		     CORE1_AFTER_PPS ||
		    frame_par->supscl_path ==
		     PPS_CORE0_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_CORE0_POSTBLEND_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_POSTBLEND_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_CORE1_CM)
			r3 >>= frame_par->supsc1_hori_ratio;
		if (frame_par->supscl_path ==
		     CORE0_AFTER_PPS ||
		    frame_par->supscl_path ==
		     PPS_CORE0_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_CORE0_POSTBLEND_CORE1)
			r3 >>= frame_par->supsc0_hori_ratio;

		if (has_pre_hscaler_ntap(0)) {
			int ds_ratio = 1;
			int flt_num = 4;
			int pre_hscaler_table[4] = {
				0x0, 0x0, 0xf8, 0x48};
			get_pre_hscaler_para(0, &ds_ratio, &flt_num);
			if (hscaler_8tap_enable[0])
				hsc_init_rev_num0 = 8;
			else
				hsc_init_rev_num0 = 4;
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				frame_par->VPP_hf_ini_phase_,
				VPP_HSC_TOP_INI_PHASE_BIT,
				VPP_HSC_TOP_INI_PHASE_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl,
				hsc_init_rev_num0,
				VPP_HSC_INIRCV_NUM_BIT,
				VPP_HSC_INIRCV_NUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl,
				frame_par->hsc_rpt_p0_num0,
				VPP_HSC_INIRPT_NUM_BIT_8TAP,
				VPP_HSC_INIRPT_NUM_WID_8TAP);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				hsc_init_rev_num0,
				VPP_HSC_INIRCV_NUM_BIT,
				VPP_HSC_INIRCV_NUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				frame_par->hsc_rpt_p0_num0,
				VPP_HSC_INIRPT_NUM_BIT_8TAP,
				VPP_HSC_INIRPT_NUM_WID_8TAP);
			if (has_pre_hscaler_8tap(0)) {
				/* 8 tap */
				get_pre_hscaler_coef(0, pre_hscaler_table);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[0],
					VPP_PREHSC_8TAP_COEF0_BIT,
					VPP_PREHSC_8TAP_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[1],
					VPP_PREHSC_8TAP_COEF1_BIT,
					VPP_PREHSC_8TAP_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef1,
					pre_hscaler_table[2],
					VPP_PREHSC_8TAP_COEF2_BIT,
					VPP_PREHSC_8TAP_COEF2_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef1,
					pre_hscaler_table[3],
					VPP_PREHSC_8TAP_COEF3_BIT,
					VPP_PREHSC_8TAP_COEF3_WID);
			} else {
				/* 2,4 tap */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[0],
					VPP_PREHSC_COEF0_BIT,
					VPP_PREHSC_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[1],
					VPP_PREHSC_COEF1_BIT,
					VPP_PREHSC_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[2],
					VPP_PREHSC_COEF2_BIT,
					VPP_PREHSC_COEF2_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[3],
					VPP_PREHSC_COEF3_BIT,
					VPP_PREHSC_COEF3_WID);
			}
			if (has_pre_vscaler_ntap(0)) {
				/* T5, T7 */
				if (has_pre_hscaler_8tap(0)) {
					/* T7 */
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						flt_num,
						VPP_PREHSC_FLT_NUM_BIT_T7,
						VPP_PREHSC_FLT_NUM_WID_T7);
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						ds_ratio,
						VPP_PREHSC_DS_RATIO_BIT_T7,
						VPP_PREHSC_DS_RATIO_WID_T7);
				} else {
					/* T5 */
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						flt_num,
						VPP_PREHSC_FLT_NUM_BIT_T5,
						VPP_PREHSC_FLT_NUM_WID_T5);
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						ds_ratio,
						VPP_PREHSC_DS_RATIO_BIT_T5,
						VPP_PREHSC_DS_RATIO_WID_T5);
				}
			} else {
				/* SC2 */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREHSC_FLT_NUM_BIT,
					VPP_PREHSC_FLT_NUM_WID);
			}
		}
		if (has_pre_vscaler_ntap(0)) {
			int ds_ratio = 1;
			int flt_num = 4;
			int pre_vscaler_table[2];

			get_pre_vscaler_para(0, &ds_ratio, &flt_num);
			get_pre_vscaler_coef(0, pre_vscaler_table);
			if (has_pre_hscaler_8tap(0)) {
				//int pre_vscaler_table[2] = {0xc0, 0x40};

				if (!pre_scaler[0].pre_vscaler_ntap_enable) {
					pre_vscaler_table[0] = 0x100;
					pre_vscaler_table[1] = 0x0;
					flt_num = 2;
				}
				/* T7 */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[0],
					VPP_PREVSC_COEF0_BIT_T7,
					VPP_PREVSC_COEF0_WID_T7);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[1],
					VPP_PREVSC_COEF1_BIT_T7,
					VPP_PREVSC_COEF1_WID_T7);

				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREVSC_FLT_NUM_BIT_T7,
					VPP_PREVSC_FLT_NUM_WID_T7);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					ds_ratio,
					VPP_PREVSC_DS_RATIO_BIT_T7,
					VPP_PREVSC_DS_RATIO_WID_T7);
			} else {
				//int pre_vscaler_table[2] = {0xf8, 0x48};

				if (!pre_scaler[0].pre_vscaler_ntap_enable) {
					pre_vscaler_table[0] = 0;
					pre_vscaler_table[1] = 0x40;
					flt_num = 2;
				}
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[0],
					VPP_PREVSC_COEF0_BIT,
					VPP_PREVSC_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[1],
					VPP_PREVSC_COEF1_BIT,
					VPP_PREVSC_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREVSC_FLT_NUM_BIT_T5,
					VPP_PREVSC_FLT_NUM_WID_T5);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					ds_ratio,
					VPP_PREVSC_DS_RATIO_BIT_T5,
					VPP_PREVSC_DS_RATIO_WID_T5);
			}
		}

		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_hsc_phase_ctrl,
			frame_par->VPP_hf_ini_phase_,
			VPP_HSC_TOP_INI_PHASE_BIT,
			VPP_HSC_TOP_INI_PHASE_WID);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region12_startp,
			(0 << VPP_REGION1_BIT) |
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION2_BIT));
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region34_startp,
			((r2 & VPP_REGION_MASK)
			<< VPP_REGION3_BIT) |
			((r3 & VPP_REGION_MASK)
			<< VPP_REGION4_BIT));
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region4_endp, r3);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_start_phase_step,
			vpp_filter->vpp_hf_start_phase_step);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region1_phase_slope,
			vpp_filter->vpp_hf_start_phase_slope);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region3_phase_slope,
			vpp_filter->vpp_hf_end_phase_slope);
	}

	/* vertical filter settings */
	if (setting->sc_v_enable) {
		if (cur_dev->pps_auto_calc) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_vsc_phase_ctrl,
				4,
				VPP_PHASECTL_INIRCVNUMT_BIT,
				VPP_PHASECTL_INIRCVNUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_vsc_phase_ctrl,
				frame_par->vsc_top_rpt_l0_num,
				VPP_PHASECTL_INIRPTNUMT_BIT,
				VPP_PHASECTL_INIRPTNUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_pps_reg->vd_vsc_init_phase,
				frame_par->VPP_vf_init_phase |
				(frame_par->VPP_vf_init_phase << 16));
		}
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_vsc_phase_ctrl,
			(vpp_filter->vpp_vert_coeff[0] == 2) ? 1 : 0,
			VPP_PHASECTL_DOUBLELINE_BIT,
			VPP_PHASECTL_DOUBLELINE_WID);
		bit9_mode = vpp_filter->vpp_vert_coeff[1] & 0x8000;
		s11_mode = vpp_filter->vpp_vert_coeff[1] & 0x4000;
		if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x199, 12, 9);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x77, 12, 9);
		if (layer->aisr_mif_setting.aisr_enable &&
		   cur_dev->pps_auto_calc)
			vpp_vert_coeff = aisr_vpp_filter->vpp_vert_coeff;
		else
			vpp_vert_coeff = vpp_filter->vpp_vert_coeff;
		if (bit9_mode || s11_mode) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_pps_reg->vd_scale_coef_idx,
				VPP_COEF_VERT |
				VPP_COEF_9BIT);
			for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
				& 0xff); i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_vert_coeff[i + 2]);
			}
			for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_vert_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
			}
			if (cur_dev->scaler_sep_coef_en) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_SEP_COEF_VERT_CHROMA |
					VPP_SEP_COEF |
					VPP_COEF_9BIT);
				for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_vert_coeff[i + 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_vert_coeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
				}
			}
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_pps_reg->vd_scale_coef_idx,
				VPP_COEF_VERT);
			for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
				& 0xff); i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_vert_coeff[i + 2]);
			}
			if (cur_dev->scaler_sep_coef_en) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_SEP_COEF_VERT_CHROMA |
					VPP_SEP_COEF);
				for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						vpp_filter->vpp_vert_coeff[i + 2]);
				}
			}
		}
		/* vertical chroma filter settings */
		if (vpp_filter->vpp_vert_chroma_filter_en) {
			const u32 *pcoeff = vpp_filter->vpp_vert_chroma_coeff;

			bit9_mode = pcoeff[1] & 0x8000;
			s11_mode = pcoeff[1] & 0x4000;
			if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_pre_scale_ctrl,
				0x199, 12, 9);
			else
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_pre_scale_ctrl,
				0x77, 12, 9);
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_VERT_CHROMA | VPP_COEF_SEP_EN);
				for (i = 0; i < pcoeff[1]; i++)
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2]);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_VERT_CHROMA | VPP_COEF_SEP_EN);
				for (i = 0; i < pcoeff[1]; i++)
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2]);
			}
		}

		r1 = frame_par->VPP_vsc_endp
			- frame_par->VPP_vsc_startp;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region12_startp, 0);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region34_startp,
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION3_BIT) |
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION4_BIT));

		if (frame_par->supscl_path ==
		     CORE0_PPS_CORE1 ||
		    frame_par->supscl_path ==
		     CORE1_AFTER_PPS ||
		    frame_par->supscl_path ==
		     PPS_CORE0_POSTBLEND_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_POSTBLEND_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_CORE1_CM)
			r1 >>= frame_par->supsc1_vert_ratio;
		if (frame_par->supscl_path ==
		     CORE0_AFTER_PPS ||
		    frame_par->supscl_path ==
		     PPS_CORE0_CORE1 ||
		    frame_par->supscl_path ==
		     PPS_CORE0_POSTBLEND_CORE1 ||
		     frame_par->supscl_path ==
		     PPS_POSTBLEND_CORE1 ||
		     frame_par->supscl_path ==
		     PPS_CORE1_CM)
			r1 >>= frame_par->supsc0_vert_ratio;

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region4_endp, r1);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_start_phase_step,
			vpp_filter->vpp_vsc_start_phase_step);
	}

	/*vpp input size setting*/
	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_IN_H_V_SIZE + misc_off,
		((frame_par->video_input_w & 0x1fff) << 16)
		| (frame_par->video_input_h & 0x1fff));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_PIC_IN_HEIGHT + misc_off,
		frame_par->VPP_pic_in_height_);

	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_LINE_IN_LENGTH + misc_off,
		frame_par->VPP_line_in_length_);
	if (cur_dev->display_module == T7_DISPLAY_MODULE)
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VD1_HDR_IN_SIZE + misc_off,
			(frame_par->VPP_pic_in_height_ << 16)
			| frame_par->VPP_line_in_length_);
	/* FIXME: if enable it */
#ifdef CHECK_LATER
	if (layer->enabled == 1) {
		struct vppfilter_mode_s *vpp_filter =
		    &cur_frame_par[0]->vpp_filter;
		u32 h_phase_step, v_phase_step;

		h_phase_step = READ_VCBUS_REG
			(vd_pps_reg->vd_hsc_start_phase_step);
		v_phase_step = READ_VCBUS_REG
			(vd_pps_reg->vd_vsc_start_phase_step);
		if (vpp_filter->vpp_hf_start_phase_step != h_phase_step ||
		    vpp_filter->vpp_vsc_start_phase_step != v_phase_step)
			layer->property_changed = true;
	}
#endif
}

static void vdx_scaler_setting(struct video_layer_s *layer, struct scaler_setting_s *setting)
{
	u32 misc_off, i;
	u32 r1, r2, r3;
	struct vpp_frame_par_s *frame_par;
	struct vppfilter_mode_s *vpp_filter;
	u32 hsc_rpt_p0_num0 = 1;
	u32 hsc_init_rev_num0 = 4;
	struct hw_pps_reg_s *vd_pps_reg;
	u32 bit9_mode = 0, s11_mode = 0;
	u8 vpp_index, layer_id;

	if (!setting || !setting->frame_par)
		return;

	layer_id = layer->layer_id;
	frame_par = setting->frame_par;
	misc_off = setting->misc_reg_offt;
	vpp_filter = &frame_par->vpp_filter;
	vd_pps_reg = &layer->pps_reg;
	vpp_index = layer->vpp_index;

	if (setting->sc_top_enable) {
		u32 sc_misc_val;

		sc_misc_val = VPP_SC_TOP_EN | VPP_SC_V1OUT_EN;
		if (setting->sc_h_enable) {
			if (has_pre_hscaler_ntap(layer_id)) {
				if (pre_scaler[layer_id].pre_hscaler_ntap_enable) {
					sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT)
					| VPP_SC_HORZ_EN);
				} else {
					sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT_OLD)
					| VPP_SC_HORZ_EN);
				}
			} else {
				sc_misc_val |=
					(((vpp_filter->vpp_pre_hsc_en & 1)
					<< VPP_SC_PREHORZ_EN_BIT)
					| VPP_SC_HORZ_EN);
			}
			if (hscaler_8tap_enable[layer_id])
				sc_misc_val |=
					((vpp_filter->vpp_horz_coeff[0] & 0xf)
					<< VPP_SC_HBANK_LENGTH_BIT);
			else
				sc_misc_val |=
					((vpp_filter->vpp_horz_coeff[0] & 7)
					<< VPP_SC_HBANK_LENGTH_BIT);
		}

		if (setting->sc_v_enable) {
			sc_misc_val |= (((vpp_filter->vpp_pre_vsc_en & 1)
				<< VPP_SC_PREVERT_EN_BIT)
				| VPP_SC_VERT_EN);
			sc_misc_val |= ((vpp_filter->vpp_pre_vsc_en & 1)
				<< VPP_LINE_BUFFER_EN_BIT);
			sc_misc_val |= ((vpp_filter->vpp_vert_coeff[0] & 7)
				<< VPP_SC_VBANK_LENGTH_BIT);
		}

		if (setting->last_line_fix)
			sc_misc_val |= VPP_PPS_LAST_LINE_FIX;

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_sc_misc,
			sc_misc_val);
		if (layer_id == 1)
			vpu_module_clk_enable(vpp_index, VD2_SCALER, 0);
	} else {
		setting->sc_v_enable = false;
		setting->sc_h_enable = false;
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_sc_misc,
			0, VPP_SC_TOP_EN_BIT,
			VPP_SC_TOP_EN_WID);
		if (layer_id == 1)
			vpu_module_clk_disable(vpp_index, VD2_SCALER, 0);
	}

	/* horizontal filter settings */
	if (setting->sc_h_enable) {
		bit9_mode = vpp_filter->vpp_horz_coeff[1] & 0x8000;
		s11_mode = vpp_filter->vpp_horz_coeff[1] & 0x4000;
		if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x199, 12, 9);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x77, 12, 9);
		if (hscaler_8tap_enable[layer_id]) {
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ | VPP_COEF_9BIT);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ |
						  VPP_COEF_VERT_CHROMA |
						  VPP_COEF_9BIT);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM * 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM * 3]);
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2]);
				}
				cur_dev->rdma_func[vpp_index].rdma_wr(vd_pps_reg->vd_scale_coef_idx,
						  VPP_COEF_HORZ |
						  VPP_COEF_VERT_CHROMA);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
			}
		} else {
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ | VPP_COEF_9BIT);
				for (i = 0; i < (vpp_filter->vpp_horz_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2]);
				}
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_HORZ);
				for (i = 0; i < (vpp_filter->vpp_horz_coeff[1]
					& 0xff); i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_horz_coeff[i + 2]);
				}
			}
		}
		r1 = frame_par->VPP_hsc_linear_startp
			- frame_par->VPP_hsc_startp;
		r2 = frame_par->VPP_hsc_linear_endp
			- frame_par->VPP_hsc_startp;
		r3 = frame_par->VPP_hsc_endp
			- frame_par->VPP_hsc_startp;

		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_hsc_phase_ctrl,
			frame_par->VPP_hf_ini_phase_,
			VPP_HSC_TOP_INI_PHASE_BIT,
			VPP_HSC_TOP_INI_PHASE_WID);
		if (has_pre_hscaler_ntap(layer_id)) {
			int ds_ratio = 1;
			int flt_num = 4;
			int pre_hscaler_table[4] = {
				0x0, 0x0, 0xf8, 0x48};

			get_pre_hscaler_para(layer_id, &ds_ratio, &flt_num);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				frame_par->VPP_hf_ini_phase_,
				VPP_HSC_TOP_INI_PHASE_BIT,
				VPP_HSC_TOP_INI_PHASE_WID);
			if (hscaler_8tap_enable[layer_id]) {
				hsc_rpt_p0_num0 = 3;
				hsc_init_rev_num0 = 8;
			} else {
				hsc_init_rev_num0 = 4;
				hsc_rpt_p0_num0 = 1;
			}
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl,
				hsc_init_rev_num0,
				VPP_HSC_INIRCV_NUM_BIT,
				VPP_HSC_INIRCV_NUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl,
				hsc_rpt_p0_num0,
				VPP_HSC_INIRPT_NUM_BIT_8TAP,
				VPP_HSC_INIRPT_NUM_WID_8TAP);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				hsc_init_rev_num0,
				VPP_HSC_INIRCV_NUM_BIT,
				VPP_HSC_INIRCV_NUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_hsc_phase_ctrl1,
				hsc_rpt_p0_num0,
				VPP_HSC_INIRPT_NUM_BIT_8TAP,
				VPP_HSC_INIRPT_NUM_WID_8TAP);
			if (has_pre_hscaler_8tap(layer_id)) {
				/* 8 tap */
				get_pre_hscaler_coef(layer_id, pre_hscaler_table);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[0],
					VPP_PREHSC_8TAP_COEF0_BIT,
					VPP_PREHSC_8TAP_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[1],
					VPP_PREHSC_8TAP_COEF1_BIT,
					VPP_PREHSC_8TAP_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef1,
					pre_hscaler_table[2],
					VPP_PREHSC_8TAP_COEF2_BIT,
					VPP_PREHSC_8TAP_COEF2_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef1,
					pre_hscaler_table[3],
					VPP_PREHSC_8TAP_COEF3_BIT,
					VPP_PREHSC_8TAP_COEF3_WID);
			} else {
				/* 2,4 tap */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[0],
					VPP_PREHSC_COEF0_BIT,
					VPP_PREHSC_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[1],
					VPP_PREHSC_COEF1_BIT,
					VPP_PREHSC_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[2],
					VPP_PREHSC_COEF2_BIT,
					VPP_PREHSC_COEF2_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prehsc_coef,
					pre_hscaler_table[3],
					VPP_PREHSC_COEF3_BIT,
					VPP_PREHSC_COEF3_WID);
			}
			if (has_pre_vscaler_ntap(layer_id)) {
				/* T5, T7 */
				if (has_pre_hscaler_8tap(layer_id)) {
					/* T7 */
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						flt_num,
						VPP_PREHSC_FLT_NUM_BIT_T7,
						VPP_PREHSC_FLT_NUM_WID_T7);
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						ds_ratio,
						VPP_PREHSC_DS_RATIO_BIT_T7,
						VPP_PREHSC_DS_RATIO_WID_T7);
				} else {
					/* T5 */
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						flt_num,
						VPP_PREHSC_FLT_NUM_BIT_T5,
						VPP_PREHSC_FLT_NUM_WID_T5);
					cur_dev->rdma_func[vpp_index].rdma_wr_bits
						(vd_pps_reg->vd_pre_scale_ctrl,
						ds_ratio,
						VPP_PREHSC_DS_RATIO_BIT_T5,
						VPP_PREHSC_DS_RATIO_WID_T5);
				}
			} else {
				/* SC2 */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREHSC_FLT_NUM_BIT,
					VPP_PREHSC_FLT_NUM_WID);
			}
		}
		if (has_pre_vscaler_ntap(layer_id)) {
			int ds_ratio = 1;
			int flt_num = 4;

			if (has_pre_hscaler_8tap(layer_id)) {
				int pre_vscaler_table[2] = {0xc0, 0x40};

				if (!pre_scaler[layer_id].pre_vscaler_ntap_enable) {
					pre_vscaler_table[0] = 0x100;
					pre_vscaler_table[1] = 0x0;
					flt_num = 2;
				}
				/* T7 */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[0],
					VPP_PREVSC_COEF0_BIT_T7,
					VPP_PREVSC_COEF0_WID_T7);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[1],
					VPP_PREVSC_COEF1_BIT_T7,
					VPP_PREVSC_COEF1_WID_T7);

				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREVSC_FLT_NUM_BIT_T7,
					VPP_PREVSC_FLT_NUM_WID_T7);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					ds_ratio,
					VPP_PREVSC_DS_RATIO_BIT_T7,
					VPP_PREVSC_DS_RATIO_WID_T7);

			} else {
				int pre_vscaler_table[2] = {0xf8, 0x48};

				if (!pre_scaler[layer_id].pre_vscaler_ntap_enable) {
					pre_vscaler_table[0] = 0;
					pre_vscaler_table[1] = 0x40;
					flt_num = 2;
				}
				/* T5 */
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[0],
					VPP_PREVSC_COEF0_BIT,
					VPP_PREVSC_COEF0_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_prevsc_coef,
					pre_vscaler_table[1],
					VPP_PREVSC_COEF1_BIT,
					VPP_PREVSC_COEF1_WID);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					flt_num,
					VPP_PREVSC_FLT_NUM_BIT_T5,
					VPP_PREVSC_FLT_NUM_WID_T5);
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
					(vd_pps_reg->vd_pre_scale_ctrl,
					ds_ratio,
					VPP_PREVSC_DS_RATIO_BIT_T5,
					VPP_PREVSC_DS_RATIO_WID_T5);
			}
		}
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region12_startp,
			(0 << VPP_REGION1_BIT) |
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION2_BIT));
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region34_startp,
			((r2 & VPP_REGION_MASK)
			<< VPP_REGION3_BIT) |
			((r3 & VPP_REGION_MASK)
			<< VPP_REGION4_BIT));
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region4_endp, r3);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_start_phase_step,
			vpp_filter->vpp_hf_start_phase_step);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region1_phase_slope,
			vpp_filter->vpp_hf_start_phase_slope);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_hsc_region3_phase_slope,
			vpp_filter->vpp_hf_end_phase_slope);
	}

	/* vertical filter settings */
	if (setting->sc_v_enable) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_vsc_phase_ctrl,
			(vpp_filter->vpp_vert_coeff[0] == 2) ? 1 : 0,
			VPP_PHASECTL_DOUBLELINE_BIT,
			VPP_PHASECTL_DOUBLELINE_WID);
		bit9_mode = vpp_filter->vpp_vert_coeff[1] & 0x8000;
		s11_mode = vpp_filter->vpp_vert_coeff[1] & 0x4000;
		if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x199, 12, 9);
		else
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(vd_pps_reg->vd_pre_scale_ctrl,
					       0x77, 12, 9);
		if (bit9_mode || s11_mode) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_pps_reg->vd_scale_coef_idx,
				VPP_COEF_VERT |
				VPP_COEF_9BIT);
			for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
				& 0xff); i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_vert_coeff[i + 2]);
			}
			for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_vert_coeff[i + 2 +
					VPP_FILER_COEFS_NUM]);
			}
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_pps_reg->vd_scale_coef_idx,
				VPP_COEF_VERT);
			for (i = 0; i < (vpp_filter->vpp_vert_coeff[1]
				& 0xff); i++) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef,
					vpp_filter->vpp_vert_coeff[i + 2]);
			}
		}

		/* vertical chroma filter settings */
		if (vpp_filter->vpp_vert_chroma_filter_en) {
			const u32 *pcoeff = vpp_filter->vpp_vert_chroma_coeff;

			bit9_mode = pcoeff[1] & 0x8000;
			s11_mode = pcoeff[1] & 0x4000;
			if (s11_mode && cur_dev->display_module == T7_DISPLAY_MODULE)
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_pre_scale_ctrl,
				0x199, 12, 9);
			else
				cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_pre_scale_ctrl,
				0x77, 12, 9);
			if (bit9_mode || s11_mode) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_VERT_CHROMA | VPP_COEF_SEP_EN);
				for (i = 0; i < pcoeff[1]; i++)
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2]);
				for (i = 0; i < VPP_FILER_COEFS_NUM; i++) {
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2 +
						VPP_FILER_COEFS_NUM]);
				}
			} else {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_VERT_CHROMA | VPP_COEF_SEP_EN);
				for (i = 0; i < pcoeff[1]; i++)
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_pps_reg->vd_scale_coef,
						pcoeff[i + 2]);
			}
		}

		r1 = frame_par->VPP_vsc_endp
			- frame_par->VPP_vsc_startp;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region12_startp, 0);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region34_startp,
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION3_BIT) |
			((r1 & VPP_REGION_MASK)
			<< VPP_REGION4_BIT));

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_region4_endp, r1);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_start_phase_step,
			vpp_filter->vpp_vsc_start_phase_step);
	}
	if (layer_id == 1) {
		if (cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VD2_HDR_IN_SIZE + misc_off,
				(frame_par->VPP_pic_in_height_ << 16)
				| frame_par->VPP_line_in_length_);

		else
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_VD2_HDR_IN_SIZE + misc_off,
				(frame_par->VPP_pic_in_height_ << 16)
				| frame_par->VPP_line_in_length_);
	} else if (layer_id == 2) {
		if (cur_dev->display_module == T7_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VD3_HDR_IN_SIZE + misc_off,
				(frame_par->VPP_pic_in_height_ << 16)
				| frame_par->VPP_line_in_length_);
	}
}

static void disable_vd1_blend(struct video_layer_s *layer)
{
	u32 misc_off;
	u8 vpp_index;
	static bool first_set = true;

	if (!layer)
		return;
	vpp_index = layer->vpp_index;

	misc_off = layer->misc_reg_offt;
	if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
		pr_info("VIDEO: VD1 AFBC off now. dispbuf:%p vf_ext:%p, *dispbuf_mapping:%p, local: %p, %p, %p, %p\n",
			layer->dispbuf,
			layer->vf_ext,
			layer->dispbuf_mapping ?
			*layer->dispbuf_mapping : NULL,
			&vf_local[0], &vf_local[1],
			gvideo_recv[0] ? &gvideo_recv[0]->local_buf : NULL,
			gvideo_recv[1] ? &gvideo_recv[1]->local_buf : NULL);
	if (layer->vd1_vd2_mux) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(layer->vd_mif_reg.vd_if0_gen_reg, 0);
	} else {
		if (cur_dev->display_module != C3_DISPLAY_MODULE)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(layer->vd_afbc_reg.afbc_enable, 0);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(layer->vd_mif_reg.vd_if0_gen_reg, 0);
	}

	if (is_amdv_enable()) {
		if (is_meson_txlx_cpu() ||
		    is_meson_gxm_cpu()) {
#ifndef CONFIG_AMLOGIC_REMOVE_OLD
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VIU_MISC_CTRL1 + misc_off,
				3, 16, 2); /* bypass core1 */
#endif
		} else if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12A))
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(AMDV_PATH_CTRL + misc_off,
				3, 0, 2);
	}

	/*auto disable sr when video off*/
	if (!is_meson_txl_cpu() &&
	    !is_meson_txlx_cpu()) {
		cur_dev->rdma_func[vpp_index].rdma_wr(VPP_SRSHARP0_CTRL, 0);
		cur_dev->rdma_func[vpp_index].rdma_wr(VPP_SRSHARP1_CTRL, 0);
	}
	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		vpu_module_clk_disable(vpp_index, VD1_SCALER, 0);
		if (!first_set) {
			vpu_module_clk_disable(vpp_index, SR0, 0);
			vpu_module_clk_disable(vpp_index, SR1, 0);
		}
		first_set = false;
	}

	if (layer->dispbuf &&
	    is_local_vf(layer->dispbuf)) {
		layer->dispbuf = NULL;
		layer->vf_ext = NULL;
	}
	if (layer->dispbuf_mapping) {
		if (*layer->dispbuf_mapping &&
		    is_local_vf(*layer->dispbuf_mapping))
			*layer->dispbuf_mapping = NULL;
		layer->dispbuf_mapping = NULL;
		layer->dispbuf = NULL;
		layer->vf_ext = NULL;
	}
	layer->new_vframe_count = 0;
}

static void disable_vd2_blend(struct video_layer_s *layer)
{
	u8 vpp_index;

	if (!layer)
		return;
	vpp_index = layer->vpp_index;

	if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
		pr_info("VIDEO: VD2 AFBC off now. dispbuf:%p, vf_ext:%p, *dispbuf_mapping:%p, local: %p, %p, %p, %p\n",
			layer->dispbuf,
			layer->vf_ext,
			layer->dispbuf_mapping ?
			*layer->dispbuf_mapping : NULL,
			&vf_local[0], &vf_local[1],
			gvideo_recv[0] ? &gvideo_recv[0]->local_buf : NULL,
			gvideo_recv[1] ? &gvideo_recv[1]->local_buf : NULL);
	cur_dev->rdma_func[vpp_index].rdma_wr(layer->vd_afbc_reg.afbc_enable, 0);
	cur_dev->rdma_func[vpp_index].rdma_wr(layer->vd_mif_reg.vd_if0_gen_reg, 0);

	vpu_module_clk_disable(vpp_index, VD2_SCALER, 0);

	if (layer->dispbuf &&
	    is_local_vf(layer->dispbuf)) {
		layer->dispbuf = NULL;
		layer->vf_ext = NULL;
	}
	if (layer->dispbuf_mapping) {
		if (*layer->dispbuf_mapping &&
		    is_local_vf(*layer->dispbuf_mapping))
			*layer->dispbuf_mapping = NULL;
		layer->dispbuf_mapping = NULL;
		layer->dispbuf = NULL;
		layer->vf_ext = NULL;
	}
	/* FIXME: remove global variables */
	last_el_status = 0;
	need_disable_vd[1] = false;
	layer->new_vframe_count = 0;
}

static void vdx_clip_setting(u8 layer_id, struct clip_setting_s *setting)
{
	u32 misc_off;
	u8 vpp_index = 0;
	struct video_layer_s *layer;

	if (!setting)
		return;
	layer = get_vd_layer(layer_id);
	vpp_index = layer->vpp_index;

	switch (layer_id) {
	case 0:
		misc_off = setting->misc_reg_offt;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC0 + misc_off,
			setting->clip_max);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC1 + misc_off,
			setting->clip_min);
		break;
	case 1:
		misc_off = setting->misc_reg_offt;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD2_CLIP_MISC0 + misc_off,
			setting->clip_max);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD2_CLIP_MISC1 + misc_off,
			setting->clip_min);
		break;
	case 2:
		misc_off = setting->misc_reg_offt;
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD3_CLIP_MISC0 + misc_off,
			setting->clip_max);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD3_CLIP_MISC1 + misc_off,
			setting->clip_min);
		break;
	default:
		break;
	}
}

/*********************************************************
 * DV EL APIs
 *********************************************************/
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
static bool is_sc_enable_before_pps(struct vpp_frame_par_s *par)
{
	bool ret = false;

	if (par) {
		if (par->supsc0_enable &&
		    (par->supscl_path == CORE0_PPS_CORE1 ||
		     par->supscl_path == CORE0_BEFORE_PPS))
			ret = true;
		else if (par->supsc1_enable &&
			 (par->supscl_path == CORE1_BEFORE_PPS))
			ret = true;
		else if ((par->supsc0_enable || par->supsc1_enable) &&
			 (par->supscl_path == CORE0_CORE1_PPS))
			ret = true;
	}
	return ret;
}

void config_dvel_position(struct video_layer_s *layer,
			  struct mif_pos_s *setting,
			  struct vframe_s *el_vf)
{
	int width_bl, width_el;
	int height_bl, height_el;
	int shift = 0, line_in_length;
	struct vpp_frame_par_s *cur_frame_par;
	struct vframe_s *bl_vf;

	if (!layer || !layer->cur_frame_par ||
	    !layer->dispbuf || !setting || !el_vf)
		return;

	bl_vf = layer->dispbuf;
	cur_frame_par = layer->cur_frame_par;

	setting->id = 1;
	setting->p_vd_mif_reg = &vd_layer[1].vd_mif_reg;
	setting->p_vd_afbc_reg = &vd_layer[1].vd_afbc_reg;
	setting->reverse = glayer_info[0].reverse;

	width_el = (el_vf->type & VIDTYPE_COMPRESS) ?
		el_vf->compWidth : el_vf->width;
	if (!(el_vf->type & VIDTYPE_VD2)) {
		width_bl =
			(bl_vf->type & VIDTYPE_COMPRESS) ?
			bl_vf->compWidth : bl_vf->width;
		if (width_el >= width_bl)
			width_el = width_bl;
		else if (width_el != width_bl / 2)
			width_el = width_bl / 2;
		if (width_el >= width_bl)
			shift = 0;
		else
			shift = 1;
	}

	height_el = (el_vf->type & VIDTYPE_COMPRESS) ?
		el_vf->compHeight : el_vf->height;
	if (!(el_vf->type & VIDTYPE_VD2)) {
		height_bl =
			(bl_vf->type & VIDTYPE_COMPRESS) ?
			bl_vf->compHeight : bl_vf->height;
		if (height_el >= height_bl)
			height_el = height_bl;
		else if (height_el != height_bl / 2)
			height_el = height_bl / 2;
	}

	setting->src_w = width_el;
	setting->src_h = height_el;

	setting->start_x_lines = 0;
	setting->end_x_lines = width_el - 1;
	setting->start_y_lines = 0;
	setting->end_y_lines = height_el - 1;

	/* TODO: remove it */
	if ((is_amdv_on() == true) &&
	    cur_frame_par->VPP_line_in_length_ > 0 &&
	    !is_sc_enable_before_pps(cur_frame_par)) {
		setting->start_x_lines =
			cur_frame_par->VPP_hd_start_lines_ >> shift;
		line_in_length =
			cur_frame_par->VPP_line_in_length_ >> shift;
		line_in_length &= 0xfffffffe;
		if (line_in_length > 1)
			setting->end_x_lines =
				setting->start_x_lines +
				line_in_length - 1;
		else
			setting->end_x_lines = setting->start_x_lines;

		setting->start_y_lines =
			cur_frame_par->VPP_vd_start_lines_ >> shift;
		if (setting->start_y_lines >= setting->end_y_lines)
			setting->end_y_lines = setting->start_y_lines;
		/* FIXME: if el len is 0, need disable bl */
	}

	setting->h_skip = layer->cur_frame_par->hscale_skip_count;
	setting->v_skip = layer->cur_frame_par->vscale_skip_count;
	/* afbc is nv12 as default */
	setting->hc_skip = 2;
	setting->vc_skip = 2;

	if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
		if ((bl_vf->type & VIDTYPE_VIU_444) ||
		    (bl_vf->type & VIDTYPE_RGB_444)) {
			setting->hc_skip = 1;
			setting->vc_skip = 1;
		} else if (bl_vf->type & VIDTYPE_VIU_422) {
			setting->vc_skip = 1;
		}
	}

	if ((bl_vf->type & VIDTYPE_COMPRESS) &&
	    !layer->cur_frame_par->nocomp)
		setting->skip_afbc = false;
	else
		setting->skip_afbc = true;

	setting->l_hs_luma = setting->start_x_lines;
	setting->l_he_luma = setting->end_x_lines;
	setting->l_vs_luma = setting->start_y_lines;
	setting->l_ve_luma = setting->end_y_lines;
	setting->r_hs_luma = setting->start_x_lines;
	setting->r_he_luma = setting->end_x_lines;
	setting->r_vs_luma = setting->start_y_lines;
	setting->r_ve_luma = setting->end_y_lines;

	setting->l_hs_chrm = setting->l_hs_luma >> 1;
	setting->l_he_chrm = setting->l_he_luma >> 1;
	setting->r_hs_chrm = setting->r_hs_luma >> 1;
	setting->r_he_chrm = setting->r_he_luma >> 1;
	setting->l_vs_chrm = setting->l_vs_luma >> 1;
	setting->l_ve_chrm = setting->l_ve_luma >> 1;
	setting->r_vs_chrm = setting->r_vs_luma >> 1;
	setting->r_ve_chrm = setting->r_ve_luma >> 1;
	setting->vpp_3d_mode = 0;
}

s32 config_dvel_pps(struct video_layer_s *layer,
		    struct scaler_setting_s *setting,
		    const struct vinfo_s *info)
{
	if (!layer || !layer->cur_frame_par ||
	    !setting || !info)
		return -1;

	setting->support =
		glayer_info[1].pps_support;
	setting->frame_par = layer->cur_frame_par;
	setting->id = 1;
	setting->misc_reg_offt = vd_layer[1].misc_reg_offt;

	if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12B))
		setting->last_line_fix = true;
	else
		setting->last_line_fix = false;

	setting->sc_top_enable = false;

	setting->vinfo_width = info->width;
	setting->vinfo_height = info->height;
	return 0;
}

s32 config_dvel_blend(struct video_layer_s *layer,
		      struct blend_setting_s *setting,
		      struct vframe_s *dvel_vf)
{
	if (!layer || !layer->cur_frame_par || !setting)
		return -1;

	setting->frame_par = layer->cur_frame_par;
	setting->id = 1;
	setting->misc_reg_offt = vd_layer[1].misc_reg_offt;
	setting->layer_alpha = layer->layer_alpha;

	setting->postblend_h_start = 0;
	setting->postblend_h_end =
		((dvel_vf->type & VIDTYPE_COMPRESS) ?
		dvel_vf->compWidth :
		dvel_vf->width) - 1;
	setting->postblend_v_start = 0;
	setting->postblend_v_end =
		((dvel_vf->type & VIDTYPE_COMPRESS) ?
		dvel_vf->compHeight :
		dvel_vf->height) - 1;
	return 0;
}
#endif

/*********************************************************
 * 3D APIs
 *********************************************************/
#ifdef TV_3D_FUNCTION_OPEN
static void get_3d_horz_pos(struct video_layer_s *layer,
			    struct vframe_s *vf,
	u32 *ls, u32 *le, u32 *rs, u32 *re)
{
	u32 crop_sx, crop_ex;
	int frame_width;
	struct disp_info_s *info = &glayer_info[0];
	struct vpp_frame_par_s *cur_frame_par;
	u32 vpp_3d_mode;

	if (!vf || !layer->cur_frame_par)
		return;

	cur_frame_par = layer->cur_frame_par;
	vpp_3d_mode = cur_frame_par->vpp_3d_mode;
	crop_sx = info->crop_left;
	crop_ex = info->crop_right;

	if (!cur_frame_par->nocomp &&
	    (vf->type & VIDTYPE_COMPRESS))
		frame_width = vf->compWidth;
	else
		frame_width = vf->width;

	if (vpp_3d_mode == VPP_3D_MODE_LR) {
		/*half width, double height */
		*ls = layer->start_x_lines;
		if (process_3d_type & MODE_3D_OUT_LR)
			*le = layer->end_x_lines >> 1;
		else
			*le = layer->end_x_lines;
		*rs = *ls + (frame_width >> 1);
		*re = *le + (frame_width >> 1);
	} else {
		/* VPP_3D_MODE_TB: */
		/* VPP_3D_MODE_LA: */
		/* VPP_3D_MODE_FA: */
		/* and others. */
#ifdef CONFIG_AMLOGIC_MEDIA_TVIN
		if (vf->trans_fmt == TVIN_TFMT_3D_FP) {
			*ls = vf->left_eye.start_x + crop_sx;
			*le = vf->left_eye.start_x
				+ vf->left_eye.width
				- crop_ex - 1;
			*rs = vf->right_eye.start_x + crop_sx;
			*re = vf->right_eye.start_x
				+ vf->right_eye.width
				- crop_ex - 1;
		} else if (process_3d_type
			   & MODE_3D_OUT_LR) {
			*ls = layer->start_x_lines;
			*le = layer->end_x_lines >> 1;
			*rs = *ls;
			*re = *le;
		} else {
			*ls = *rs = layer->start_x_lines;
			*le = *re = layer->end_x_lines;
		}
#endif
	}
}

static void get_3d_vert_pos(struct video_layer_s *layer,
			    struct vframe_s *vf,
	u32 *ls, u32 *le, u32 *rs, u32 *re)
{
	u32 crop_sy, crop_ey, height;
	int frame_height;
	struct disp_info_s *info = &glayer_info[0];
	struct vpp_frame_par_s *cur_frame_par;
	u32 vpp_3d_mode;

	if (!vf || !layer->cur_frame_par)
		return;

	cur_frame_par = layer->cur_frame_par;
	vpp_3d_mode = cur_frame_par->vpp_3d_mode;
	crop_sy = info->crop_top;
	crop_ey = info->crop_bottom;

	if (!cur_frame_par->nocomp &&
	    (vf->type & VIDTYPE_COMPRESS))
		frame_height = vf->compHeight;
	else
		frame_height = vf->height;

	if (vf->type & VIDTYPE_INTERLACE)
		height = frame_height >> 1;
	else
		height = frame_height;

	if (vpp_3d_mode == VPP_3D_MODE_TB) {
#ifdef CONFIG_AMLOGIC_MEDIA_TVIN
		if (vf->trans_fmt == TVIN_TFMT_3D_FP) {
			*ls = vf->left_eye.start_y + (crop_sy >> 1);
			*le = vf->left_eye.start_y
				+ vf->left_eye.height
				- (crop_ey >> 1);
			*rs = vf->right_eye.start_y + (crop_sy >> 1);
			*re = vf->right_eye.start_y
				+ vf->left_eye.height
				- (crop_ey >> 1);
			if (vf->type & VIDTYPE_INTERLACE) {
				/*if input is interlace vertical*/
				/*crop will be reduce by half */
				*ls = *ls >> 1;
				*le = *le >> 1;
				*rs = *rs >> 1;
				*re = *re >> 1;
			}
			*le = *le - 1;
			*re = *re - 1;
		} else {
			*ls = layer->start_y_lines >> 1;
			*le = layer->end_y_lines >> 1;
			if ((vf->type & VIDTYPE_VIU_FIELD) &&
			    (vf->type & VIDTYPE_INTERLACE)) {
				/* vdin interlace case */
				*rs = *ls + (height >> 1);
				*re = *le + (height >> 1);
			} else if (vf->type & VIDTYPE_INTERLACE) {
				/* decode interlace case */
				*rs = *ls + height;
				*re = *le + height;
			} else {
				/* mvc case: same width, same height */
				*rs = *ls + (height >> 1);
				*re = *le + (height >> 1);
			}
			if ((process_3d_type
			     & MODE_3D_TO_2D_MASK) ||
			    (process_3d_type
			     & MODE_3D_OUT_LR)) {
				/* same width, half height */
				*ls = layer->start_y_lines;
				*le = layer->end_y_lines;
				*rs = *ls + (height >> 1);
				*re = *le + (height >> 1);
			}
		}
#endif
	} else if (vpp_3d_mode == VPP_3D_MODE_LR) {
		if ((process_3d_type & MODE_3D_TO_2D_MASK) ||
		    (process_3d_type & MODE_3D_OUT_LR)) {
			/*half width ,same height */
			*ls = layer->start_y_lines;
			*le = layer->end_y_lines;
			*rs = layer->start_y_lines;
			*re = layer->end_y_lines;
		} else {
			/* half width,double height */
			*ls = layer->start_y_lines >> 1;
			*le = layer->end_y_lines >> 1;
			*rs = layer->start_y_lines >> 1;
			*re = layer->end_y_lines >> 1;
		}
	} else if (vpp_3d_mode == VPP_3D_MODE_FA) {
		/*same width same height */
		if ((process_3d_type & MODE_3D_TO_2D_MASK) ||
		    (process_3d_type & MODE_3D_OUT_LR)) {
			*ls = layer->start_y_lines;
			*le = layer->end_y_lines;
			*rs = layer->start_y_lines;
			*re = layer->end_y_lines;
		} else {
			*ls = (layer->start_y_lines + crop_sy) >> 1;
			*le = (layer->end_y_lines + crop_ey) >> 1;
			*rs = (layer->start_y_lines + crop_sy) >> 1;
			*re = (layer->end_y_lines + crop_ey) >> 1;
		}
	} else if (vpp_3d_mode == VPP_3D_MODE_LA) {
		if ((process_3d_type & MODE_3D_OUT_FA_MASK) ||
		    (process_3d_type & MODE_3D_OUT_TB) ||
		    (process_3d_type & MODE_3D_OUT_LR)) {
			*rs = layer->start_y_lines + 1;
			*ls = layer->start_y_lines;
		} else if ((process_3d_type & MODE_3D_LR_SWITCH) ||
			   (process_3d_type & MODE_3D_TO_2D_R)) {
			*ls = layer->start_y_lines + 1;
			*rs = layer->start_y_lines + 1;
		} else {
			*ls = layer->start_y_lines;
			*rs = layer->start_y_lines;
		}
		*le = layer->end_y_lines;
		*re = layer->end_y_lines;
	} else {
		*ls = layer->start_y_lines;
		*le = layer->end_y_lines;
		*rs = layer->start_y_lines;
		*re = layer->end_y_lines;
	}
}

void config_3d_vd2_position(struct video_layer_s *layer,
			    struct mif_pos_s *setting)
{
	struct vframe_s *dispbuf = NULL;

	if (!layer || !layer->cur_frame_par || !layer->dispbuf || !setting)
		return;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;

	memcpy(setting, &layer->mif_setting, sizeof(struct mif_pos_s));
	setting->id = 1;
	setting->p_vd_mif_reg = &vd_layer[1].vd_mif_reg;
	setting->p_vd_afbc_reg = &vd_layer[1].vd_afbc_reg;

	/* need not change the horz position */
	/* not framepacking mode, need process vert position more */
	/* framepacking mode, need use right eye position */
	/* to set vd2 Y0 not vd1 Y1*/
	if ((dispbuf->type & VIDTYPE_MVC) && framepacking_support) {
		setting->l_vs_chrm =
			setting->r_vs_chrm;
		setting->l_ve_chrm =
			setting->r_ve_chrm;
		setting->l_vs_luma =
			setting->r_vs_luma;
		setting->l_ve_luma =
			setting->r_ve_luma;
		setting->r_vs_luma = 0;
		setting->r_ve_luma = 0;
		setting->r_vs_chrm = 0;
		setting->r_ve_chrm = 0;
	}
	setting->vpp_3d_mode = 0;
}

s32 config_3d_vd2_pps(struct video_layer_s *layer,
		      struct scaler_setting_s *setting,
		      const struct vinfo_s *info)
{
	if (!layer || !layer->cur_frame_par || !setting || !info)
		return -1;

	memcpy(setting, &layer->sc_setting, sizeof(struct scaler_setting_s));
	setting->support = glayer_info[1].pps_support;
	setting->sc_top_enable = false;
	setting->id = 1;
	return 0;
}

s32 config_3d_vd2_blend(struct video_layer_s *layer,
			struct blend_setting_s *setting)
{
	struct vpp_frame_par_s *cur_frame_par;
	u32 x_lines, y_lines;
	struct vframe_s *dispbuf = NULL;

	if (!layer || !layer->cur_frame_par || !setting)
		return -1;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;

	memcpy(setting, &layer->bld_setting, sizeof(struct blend_setting_s));
	setting->id = 1;

	cur_frame_par = layer->cur_frame_par;
	x_lines = layer->end_x_lines /
		(cur_frame_par->hscale_skip_count + 1);
	y_lines = layer->end_y_lines /
		(cur_frame_par->vscale_skip_count + 1);

	if (process_3d_type & MODE_3D_OUT_TB) {
		setting->postblend_h_start =
			layer->start_x_lines;
		setting->postblend_h_end =
			layer->end_x_lines;
		setting->postblend_v_start =
			(y_lines + 1) >> 1;
		setting->postblend_v_end = y_lines;
	} else if (process_3d_type & MODE_3D_OUT_LR) {
		/* vd1 and vd2 do pre blend */
		setting->postblend_h_start =
			(x_lines + 1) >> 1;
		setting->postblend_h_end = x_lines;
		setting->postblend_v_start =
			layer->start_y_lines;
		setting->postblend_v_end =
			layer->end_y_lines;
	} else if (dispbuf && (dispbuf->type & VIDTYPE_MVC)) {
		u32 blank;

		if (framepacking_support)
			blank = framepacking_blank;
		else
			blank = 0;
		setting->postblend_v_start =
			(cur_frame_par->VPP_vd_end_lines_
			- blank + 1) >> 1;
		setting->postblend_v_start += blank;
		setting->postblend_v_end =
			cur_frame_par->VPP_vd_end_lines_;
		setting->postblend_h_start =
			cur_frame_par->VPP_hd_start_lines_;
		setting->postblend_h_end =
			cur_frame_par->VPP_hd_end_lines_;
	}
	return 0;
}

void switch_3d_view_per_vsync(struct video_layer_s *layer)
{
	struct vpp_frame_par_s *cur_frame_par;
	u32 misc_off;
	u32 start_aligned, end_aligned, block_len;
	u32 FA_enable = process_3d_type & MODE_3D_OUT_FA_MASK;
	u32 src_start_x_lines, src_end_x_lines;
	u32 src_start_y_lines, src_end_y_lines;
	u32 mif_blk_bgn_h, mif_blk_end_h;
	struct vframe_s *dispbuf = NULL;
	struct hw_vd_reg_s *vd_mif_reg;
	struct hw_vd_reg_s *vd2_mif_reg;
	struct hw_afbc_reg_s *vd_afbc_reg;
	u8 vpp_index = VPP0;

	if (!layer || !layer->cur_frame_par || !layer->dispbuf)
		return;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;
	vd_mif_reg = &layer->vd_mif_reg;
	vd2_mif_reg = &vd_layer[1].vd_mif_reg;
	vd_afbc_reg = &layer->vd_afbc_reg;

	src_start_x_lines = 0;
	src_end_x_lines =
		(dispbuf->type & VIDTYPE_COMPRESS) ?
		dispbuf->compWidth : dispbuf->width;
	src_end_x_lines -= 1;
	src_start_y_lines = 0;
	src_end_y_lines =
		(dispbuf->type & VIDTYPE_COMPRESS) ?
		dispbuf->compHeight : dispbuf->height;
	src_end_y_lines -= 1;

	misc_off = layer->misc_reg_offt;
	cur_frame_par = layer->cur_frame_par;
	if (FA_enable && toggle_3d_fa_frame == OUT_FA_A_FRAME) {
		if (cur_dev->display_module == OLD_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VPP_MISC + misc_off, 1, 14, 1);
				/* VPP_VD1_PREBLEND disable */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits(VPP_MISC +
				VPP_MISC + misc_off, 1, 10, 1);
				/* VPP_VD1_POSTBLEND disable */
		}
		if (debug_flag_3d & 0x01) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_luma_psel,
				0x4000000);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_chroma_psel,
				0x4000000);
		}
		if (debug_flag_3d & 0x02) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_luma_psel,
				0x4000000);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_chroma_psel,
				0x4000000);
		}
		if (dispbuf->type & VIDTYPE_COMPRESS) {
			if ((process_3d_type & MODE_FORCE_3D_FA_LR) &&
			    cur_frame_par->vpp_3d_mode == 1) {
				if (get_cpu_type() >= MESON_CPU_MAJOR_ID_TL1) {
					start_aligned = src_start_x_lines;
					end_aligned = src_end_x_lines + 1;
					block_len =
						(end_aligned - start_aligned) / 2;
					mif_blk_bgn_h = src_start_x_lines / 32;
					mif_blk_end_h =
						(start_aligned + block_len - 1) / 32;
					start_aligned =
						src_start_x_lines -	(mif_blk_bgn_h << 5);
					block_len = block_len /
						(cur_frame_par->hscale_skip_count + 1);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_mif_hor_scope,
						(mif_blk_bgn_h << 16) |
						mif_blk_end_h);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_pixel_hor_scope,
						(start_aligned << 16) |
						(start_aligned + block_len - 1));
				} else {
					start_aligned = src_start_x_lines;
					end_aligned = src_end_x_lines + 1;
					block_len =
						(end_aligned - start_aligned) / 2;
					block_len = block_len /
						(cur_frame_par->hscale_skip_count + 1);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_pixel_hor_scope,
						(start_aligned << 16) |
						(start_aligned + block_len - 1));
				}
			}
			if ((process_3d_type & MODE_FORCE_3D_FA_TB) &&
			    cur_frame_par->vpp_3d_mode == 2) {
				start_aligned =
					round_down(src_start_y_lines, 4);
				end_aligned =
					round_up(src_end_y_lines + 1, 4);
				block_len = end_aligned - start_aligned;
				block_len = block_len / 8;
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_afbc_reg->afbc_mif_ver_scope,
					((start_aligned / 4) << 16) |
					((start_aligned / 4)  + block_len - 1));
			}
		}
	} else if (FA_enable && (toggle_3d_fa_frame == OUT_FA_B_FRAME)) {
		if (cur_dev->display_module == OLD_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VPP_MISC + misc_off, 1, 14, 1);
			/* VPP_VD1_PREBLEND disable */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VPP_MISC + misc_off, 1, 10, 1);
			/* VPP_VD1_POSTBLEND disable */
		}
		if (debug_flag_3d & 0x4) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_luma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_chroma_psel, 0);
		}
		if (debug_flag_3d & 0x8) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_luma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_chroma_psel, 0);
		}
		if (dispbuf->type & VIDTYPE_COMPRESS) {
			if ((process_3d_type & MODE_FORCE_3D_FA_LR) &&
			    cur_frame_par->vpp_3d_mode == 1) {
				if (get_cpu_type() >= MESON_CPU_MAJOR_ID_TL1) {
					start_aligned = src_start_x_lines;
					end_aligned = src_end_x_lines + 1;
					block_len =
						(end_aligned - start_aligned) / 2;
					mif_blk_bgn_h =
						(src_start_x_lines + block_len) / 32;
					mif_blk_end_h = src_end_x_lines / 32;
					start_aligned =
						src_start_x_lines + block_len -
						(mif_blk_bgn_h << 5);
					block_len = block_len /
						(cur_frame_par->hscale_skip_count + 1);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_mif_hor_scope,
						(mif_blk_bgn_h << 16) |
						mif_blk_end_h);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_pixel_hor_scope,
						(start_aligned << 16) |
						(start_aligned + block_len - 1));
				} else {
					start_aligned = src_start_x_lines;
					end_aligned = src_end_x_lines + 1;
					block_len =
						(end_aligned - start_aligned) / 2;
					block_len = block_len /
						(cur_frame_par->hscale_skip_count + 1);
					cur_dev->rdma_func[vpp_index].rdma_wr
						(vd_afbc_reg->afbc_pixel_hor_scope,
						((start_aligned + block_len) << 16) |
						(end_aligned - 1));
				}
			}
			if ((process_3d_type & MODE_FORCE_3D_FA_TB) &&
			    cur_frame_par->vpp_3d_mode == 2) {
				start_aligned =
					round_down(src_start_y_lines, 4);
				end_aligned =
					round_up(src_end_y_lines + 1, 4);
				block_len = end_aligned - start_aligned;
				block_len = block_len / 8;
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_afbc_reg->afbc_mif_ver_scope,
				(((start_aligned / 4) + block_len) << 16) |
				((end_aligned / 4)  - 1));
			}
		}
	} else if (FA_enable && (toggle_3d_fa_frame == OUT_FA_BANK_FRAME)) {
		if (cur_dev->display_module == OLD_DISPLAY_MODULE) {
			/* output a banking frame */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VPP_MISC + misc_off, 0, 14, 1);
			/* VPP_VD1_PREBLEND disable */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(VPP_MISC + misc_off, 0, 10, 1);
			/* VPP_VD1_POSTBLEND disable */
		}
	}

	if ((process_3d_type & MODE_3D_OUT_TB) ||
	    (process_3d_type & MODE_3D_OUT_LR)) {
		if (cur_frame_par->vpp_2pic_mode & VPP_PIC1_FIRST) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_luma_psel,
				0x4000000);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_chroma_psel,
				0x4000000);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_luma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_chroma_psel, 0);
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_luma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_chroma_psel, 0);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_luma_psel,
				0x4000000);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd2_mif_reg->vd_if0_chroma_psel,
				0x4000000);
		}
	}

	if (force_3d_scaler <= 3 &&
	    cur_frame_par->vpp_3d_scale) {
		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(VPP_VSC_PHASE_CTRL + misc_off,
			force_3d_scaler,
			VPP_PHASECTL_DOUBLELINE_BIT,
			VPP_PHASECTL_DOUBLELINE_WID);
	}
}
#endif

s32 config_vd_position_internal(struct video_layer_s *layer,
		       struct mif_pos_s *setting)
{
	struct vframe_s *dispbuf = NULL;

	if (!layer || !layer->cur_frame_par || !layer->dispbuf || !setting)
		return -1;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;
	setting->id = layer->layer_id;
	setting->p_vd_mif_reg = &layer->vd_mif_reg;
	setting->p_vd_afbc_reg = &layer->vd_afbc_reg;

	setting->reverse = glayer_info[setting->id].reverse;
	setting->src_w =
		(dispbuf->type & VIDTYPE_COMPRESS) ?
		dispbuf->compWidth : dispbuf->width;
	setting->src_h =
		(dispbuf->type & VIDTYPE_COMPRESS) ?
		dispbuf->compHeight : dispbuf->height;

	setting->start_x_lines = layer->start_x_lines;
	setting->end_x_lines = layer->end_x_lines;
	setting->start_y_lines = layer->start_y_lines;
	setting->end_y_lines = layer->end_y_lines;

	setting->h_skip = layer->cur_frame_par->hscale_skip_count;
	setting->v_skip = layer->cur_frame_par->vscale_skip_count;
	/* afbc is nv12 as default */
	setting->hc_skip = 2;
	setting->vc_skip = 2;

	if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
		if ((dispbuf->type & VIDTYPE_VIU_444) ||
		    (dispbuf->type & VIDTYPE_RGB_444)) {
			setting->hc_skip = 1;
			setting->vc_skip = 1;
		} else if (dispbuf->type & VIDTYPE_VIU_422) {
			setting->vc_skip = 1;
		}
	}

	if ((dispbuf->type & VIDTYPE_COMPRESS) &&
	    !layer->cur_frame_par->nocomp)
		setting->skip_afbc = false;
	else
		setting->skip_afbc = true;

	setting->l_hs_luma = layer->start_x_lines;
	setting->l_he_luma = layer->end_x_lines;
	setting->l_vs_luma = layer->start_y_lines;
	setting->l_ve_luma = layer->end_y_lines;
	setting->r_hs_luma = layer->start_x_lines;
	setting->r_he_luma = layer->end_x_lines;
	setting->r_vs_luma = layer->start_y_lines;
	setting->r_ve_luma = layer->end_y_lines;

#ifdef TV_3D_FUNCTION_OPEN
	if (setting->id == 0 &&
	    (process_3d_type & MODE_3D_ENABLE)) {
		get_3d_horz_pos
			(layer, dispbuf,
			&setting->l_hs_luma, &setting->l_he_luma,
			&setting->r_hs_luma, &setting->r_he_luma);
		get_3d_vert_pos
			(layer, dispbuf,
			&setting->l_vs_luma, &setting->l_ve_luma,
			&setting->r_vs_luma, &setting->r_ve_luma);
	}
#endif
	setting->l_hs_chrm = setting->l_hs_luma >> 1;
	setting->l_he_chrm = setting->l_he_luma >> 1;
	setting->r_hs_chrm = setting->r_hs_luma >> 1;
	setting->r_he_chrm = setting->r_he_luma >> 1;
	setting->l_vs_chrm = setting->l_vs_luma >> 1;
	setting->l_ve_chrm = setting->l_ve_luma >> 1;
	setting->r_vs_chrm = setting->r_vs_luma >> 1;
	setting->r_ve_chrm = setting->r_ve_luma >> 1;

#ifdef TV_3D_FUNCTION_OPEN
	/* need not change the horz position */
	/* not framepacking mode, need process vert position more */
	/* framepacking mode, need use right eye position */
	/* to set vd2 Y0 not vd1 Y1*/
	if (setting->id == 0 &&
	    (dispbuf->type & VIDTYPE_MVC) &&
	    !framepacking_support) {
		setting->l_vs_chrm =
			setting->l_vs_luma;
		setting->l_ve_chrm =
			setting->l_ve_luma - 1;
		setting->l_vs_luma *= 2;
		setting->l_ve_luma =
			setting->l_ve_luma * 2 - 1;
		setting->r_vs_luma = 0;
		setting->r_ve_luma = 0;
		setting->r_vs_chrm = 0;
		setting->r_ve_chrm = 0;
	}
#endif
	setting->vpp_3d_mode =
		layer->cur_frame_par->vpp_3d_mode;
	return 0;
}

static void config_vd_param_internal(struct video_layer_s *layer,
	struct vframe_s *dispbuf)
{
	u32 zoom_start_y, zoom_end_y, blank = 0;
	struct vpp_frame_par_s *frame_par = NULL;

	frame_par = layer->cur_frame_par;

	/* progressive or decode interlace case height 1:1 */
	/* vdin afbc and interlace case height 1:1 */
	zoom_start_y = frame_par->VPP_vd_start_lines_;
	zoom_end_y = frame_par->VPP_vd_end_lines_;
	if ((dispbuf->type & VIDTYPE_INTERLACE) &&
		(dispbuf->type & VIDTYPE_VIU_FIELD)) {
		/* vdin interlace non afbc frame case height/2 */
		zoom_start_y /= 2;
		zoom_end_y = ((zoom_end_y + 1) >> 1) - 1;
	} else if (dispbuf->type & VIDTYPE_MVC) {
		/* mvc case, (height - blank)/2 */
		if (framepacking_support)
			blank = framepacking_blank;
		else
			blank = 0;
		zoom_start_y /= 2;
		zoom_end_y = ((zoom_end_y - blank + 1) >> 1) - 1;
	}

	layer->start_x_lines = frame_par->VPP_hd_start_lines_;
	layer->end_x_lines = frame_par->VPP_hd_end_lines_;
	layer->start_y_lines = zoom_start_y;
	layer->end_y_lines = zoom_end_y;
}

void config_vd_param(struct video_layer_s *layer,
	struct vframe_s *dispbuf)
{
	config_vd_param_internal(layer, dispbuf);
}

s32 config_vd_position(struct video_layer_s *layer,
		       struct mif_pos_s *setting)
{
	config_vd_position_internal(layer, setting);

	return 0;
}

s32 config_vd_pps_internal(struct video_layer_s *layer,
		  struct scaler_setting_s *setting,
		  const struct vinfo_s *info)
{
	struct vppfilter_mode_s *vpp_filter;
	struct vpp_frame_par_s *cur_frame_par;
	s32 src_w, src_h, dst_w, dst_h;

	if (!layer || !layer->cur_frame_par || !setting || !info)
		return -1;

	setting->support = glayer_info[layer->layer_id].pps_support;
	cur_frame_par = layer->cur_frame_par;
	vpp_filter = &cur_frame_par->vpp_filter;
	setting->frame_par = cur_frame_par;
	setting->id = layer->layer_id;
	setting->misc_reg_offt = layer->misc_reg_offt;
	/* enable pps as default */
	setting->sc_top_enable = true;
	setting->sc_h_enable = true;
	setting->sc_v_enable = true;
	if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12B))
		setting->last_line_fix = true;
	else
		setting->last_line_fix = false;

	if (cur_dev->display_module != S5_DISPLAY_MODULE) {
		src_w = cur_frame_par->video_input_w << cur_frame_par->supsc0_hori_ratio;
		src_h = cur_frame_par->video_input_h << cur_frame_par->supsc0_vert_ratio;
		dst_w = cur_frame_par->VPP_hsc_endp - cur_frame_par->VPP_hsc_startp + 1;
		dst_h = cur_frame_par->VPP_vsc_endp - cur_frame_par->VPP_vsc_startp + 1;
	}
	if (vpp_filter->vpp_hsc_start_phase_step == 0x1000000 &&
	    vpp_filter->vpp_vsc_start_phase_step == 0x1000000 &&
	    vpp_filter->vpp_hsc_start_phase_step ==
	     vpp_filter->vpp_hf_start_phase_step &&
	    src_w == dst_w &&
	    src_h == dst_h &&
	    !vpp_filter->vpp_pre_vsc_en &&
	    !vpp_filter->vpp_pre_hsc_en &&
	    layer->bypass_pps &&
	    !glayer_info[layer->layer_id].ver_coef_adjust)
		setting->sc_top_enable = false;

	setting->vinfo_width = info->width;
	setting->vinfo_height = info->height;
	return 0;
}

s32 config_vd_pps(struct video_layer_s *layer,
		  struct scaler_setting_s *setting,
		  const struct vinfo_s *info)
{
	config_vd_pps_internal(layer, setting, info);
	return 0;
}

s32 config_vd_blend(struct video_layer_s *layer,
		    struct blend_setting_s *setting)
{
	struct vpp_frame_par_s *cur_frame_par;
	struct vframe_s *dispbuf = NULL;
	u32 x_lines, y_lines;
#ifdef CHECK_LATER
	u32 type = 0;
#endif

	if (!layer || !layer->cur_frame_par || !setting)
		return -1;

	if (layer->switch_vf && layer->vf_ext)
		dispbuf = layer->vf_ext;
	else
		dispbuf = layer->dispbuf;
	cur_frame_par = layer->cur_frame_par;
	setting->frame_par = cur_frame_par;
	setting->id = layer->layer_id;
	setting->misc_reg_offt = layer->misc_reg_offt;
	setting->layer_alpha = layer->layer_alpha;
	x_lines = layer->end_x_lines /
		(cur_frame_par->hscale_skip_count + 1);
	y_lines = layer->end_y_lines /
		(cur_frame_par->vscale_skip_count + 1);

	if (cur_dev->display_module == C3_DISPLAY_MODULE) {
		setting->postblend_h_start =
			cur_frame_par->VPP_hd_start_lines_;
		setting->postblend_h_end =
			cur_frame_par->VPP_hd_start_lines_ +
			cur_frame_par->video_input_w - 1;
		setting->postblend_v_start =
			cur_frame_par->VPP_vd_start_lines_;
		setting->postblend_v_end =
			cur_frame_par->VPP_vd_start_lines_ +
			cur_frame_par->video_input_h - 1;
		return 0;
	}

	if (legacy_vpp) {
		setting->preblend_h_start = 0;
		setting->preblend_h_end = 4096;
	} else {
		setting->preblend_h_start = 0;
		setting->preblend_h_end =
			cur_frame_par->video_input_w - 1;
	}
	if (dispbuf && (dispbuf->type & VIDTYPE_MVC)) {
		setting->preblend_v_start =
			cur_frame_par->VPP_vd_start_lines_;
		setting->preblend_v_end = y_lines;
		setting->preblend_h_start =
			cur_frame_par->VPP_hd_start_lines_;
		setting->preblend_h_end =
			cur_frame_par->VPP_hd_end_lines_;
	}

	if ((cur_frame_par->VPP_post_blend_vd_v_end_ -
	     cur_frame_par->VPP_post_blend_vd_v_start_ + 1)
	    > VPP_PREBLEND_VD_V_END_LIMIT) {
		setting->preblend_v_start =
			cur_frame_par->VPP_post_blend_vd_v_start_;
		setting->preblend_v_end =
			cur_frame_par->VPP_post_blend_vd_v_end_;
	} else {
		setting->preblend_v_start = 0;
		setting->preblend_v_end =
			VPP_PREBLEND_VD_V_END_LIMIT - 1;
	}

	setting->preblend_h_size =
		cur_frame_par->video_input_w;
	setting->postblend_h_size =
		cur_frame_par->VPP_post_blend_h_size_;

	/* no scaler, so only can use mif setting */
	if (!glayer_info[layer->layer_id].pps_support) {
		setting->postblend_h_start =
			cur_frame_par->VPP_hd_start_lines_;
		setting->postblend_h_end =
			cur_frame_par->VPP_hd_start_lines_ +
			cur_frame_par->video_input_w - 1;
		setting->postblend_v_start =
			cur_frame_par->VPP_vd_start_lines_;
		setting->postblend_v_end =
			cur_frame_par->VPP_vd_start_lines_ +
			cur_frame_par->video_input_h - 1;
	} else {
		/* FIXME: use sr output */
		setting->postblend_h_start =
			cur_frame_par->VPP_hsc_startp;
		setting->postblend_h_end =
			cur_frame_par->VPP_hsc_endp;
		setting->postblend_v_start =
			cur_frame_par->VPP_vsc_startp;
		setting->postblend_v_end =
			cur_frame_par->VPP_vsc_endp;
	}

	if (!legacy_vpp) {
		u32 temp_h = cur_frame_par->video_input_h;

		temp_h <<= 16;
		setting->preblend_h_size |= temp_h;

		temp_h =
			cur_frame_par->VPP_post_blend_vd_v_end_ + 1;
		temp_h <<= 16;
		setting->postblend_h_size |= temp_h;
	}
#ifdef TV_3D_FUNCTION_OPEN
	if (setting->id == 0) {
		if (process_3d_type & MODE_3D_OUT_TB) {
			setting->preblend_h_start =
				cur_frame_par->VPP_hd_start_lines_;
			setting->preblend_h_end =
				cur_frame_par->VPP_hd_end_lines_;
			setting->preblend_v_start =
				cur_frame_par->VPP_vd_start_lines_;
			setting->preblend_v_end = y_lines >> 1;
		} else if (process_3d_type & MODE_3D_OUT_LR) {
			/* vd1 and vd2 do pre blend */
			setting->preblend_h_start =
				cur_frame_par->VPP_hd_start_lines_;
			setting->preblend_h_end = x_lines >> 1;
			setting->preblend_v_start =
				cur_frame_par->VPP_vd_start_lines_;
			setting->preblend_v_end =
				cur_frame_par->VPP_vd_end_lines_;
		}
	}
#endif
	/* FIXME: need remove this work around check for afbc */
#ifdef CHECK_LATER
	if (dispbuf)
		type = dispbuf->type;
	if (cur_frame_par->nocomp)
		type &= ~VIDTYPE_COMPRESS;

	if (type & VIDTYPE_COMPRESS) {
		u32 v_phase;
		struct vppfilter_mode_s *vpp_filter =
			&cur_frame_par->vpp_filter;

		v_phase = vpp_filter->vpp_vsc_start_phase_step;
		v_phase *= (cur_frame_par->vscale_skip_count + 1);
		if (v_phase > 0x1000000 &&
		    glayer_info[setting->id].layer_top < 0) {
			if (cur_frame_par->VPP_vsc_endp > 0x6 &&
			    (cur_frame_par->VPP_vsc_endp < 0x250 ||
			     (cur_frame_par->VPP_vsc_endp <
			      cur_frame_par->VPP_post_blend_vd_v_end_ / 2))) {
				setting->postblend_v_end -= 6;
			}
		}
	}
#endif
	return 0;
}

void vd_blend_setting(struct video_layer_s *layer, struct blend_setting_s *setting)
{
	u32 misc_off = 0;
	u32 vd_size_mask = VPP_VD_SIZE_MASK;
	struct hw_vpp_blend_reg_s *vpp_blend_reg =
		&layer->vpp_blend_reg;
	u8 vpp_index = VPP0;

	if (!setting)
		return;
	/* g12a change to 13 bits */
	if (!legacy_vpp)
		vd_size_mask = 0x1fff;

	if (cur_dev->display_module == C3_DISPLAY_MODULE) {
		/* default video is background */
		/* forground setting */
		/* misc_off = 0x10; */
		cur_dev->rdma_func[vpp_index].rdma_wr
		(VPU_VOUT_BLD_SRC0_HPOS + misc_off,
		((setting->postblend_h_start & vd_size_mask)
		<< 0) |
		((setting->postblend_h_end & vd_size_mask)
		<< 16));

		cur_dev->rdma_func[vpp_index].rdma_wr
		(VPU_VOUT_BLD_SRC0_VPOS + misc_off,
		((setting->postblend_v_start & vd_size_mask)
		<< 0) |
		((setting->postblend_v_end & vd_size_mask)
		<< 16));
		return;
	}

	misc_off = setting->misc_reg_offt;
	/* preblend setting */
	if (layer->layer_id == 0) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vpp_blend_reg->preblend_h_start_end,
			((setting->preblend_h_start & vd_size_mask)
			<< VPP_VD1_START_BIT) |
			((setting->preblend_h_end & vd_size_mask)
			<< VPP_VD1_END_BIT));

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vpp_blend_reg->preblend_v_start_end,
			((setting->preblend_v_start & vd_size_mask)
			<< VPP_VD1_START_BIT) |
			((setting->preblend_v_end & vd_size_mask)
			<< VPP_VD1_END_BIT));

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vpp_blend_reg->preblend_h_size,
			setting->preblend_h_size);

		if (!legacy_vpp)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_MISC1 + misc_off,
				setting->layer_alpha);
	}
	/* postblend setting */
	cur_dev->rdma_func[vpp_index].rdma_wr
		(vpp_blend_reg->postblend_h_start_end,
		((setting->postblend_h_start & vd_size_mask)
		<< VPP_VD1_START_BIT) |
		((setting->postblend_h_end & vd_size_mask)
		<< VPP_VD1_END_BIT));

	cur_dev->rdma_func[vpp_index].rdma_wr
		(vpp_blend_reg->postblend_v_start_end,
		((setting->postblend_v_start & vd_size_mask)
		<< VPP_VD1_START_BIT) |
		((setting->postblend_v_end & vd_size_mask)
		<< VPP_VD1_END_BIT));
}

void vd_set_dcu(u8 layer_id,
		struct video_layer_s *layer,
		struct vpp_frame_par_s *frame_par,
		struct vframe_s *vf)
{
	if (layer_id == 0)
		vd1_set_dcu(layer, frame_par, vf);
	else
		vdx_set_dcu(layer, frame_par, vf);
}

void vd_scaler_setting(struct video_layer_s *layer,
		       struct scaler_setting_s *setting)
{
	u8 layer_id = 0;

	if (setting && !setting->support)
		return;
	layer_id = layer->layer_id;

	if (layer_id == 0)
		vd1_scaler_setting(layer, setting);
	else
		vdx_scaler_setting(layer, setting);
}

void vd_clip_setting(u8 vpp_index, u8 layer_id,
	struct clip_setting_s *setting)
{
	if (setting->clip_done)
		return;
	vdx_clip_setting(layer_id, setting);
	setting->clip_done = true;
}

void proc_vd_vsc_phase_per_vsync(struct video_layer_s *layer,
					 struct vpp_frame_par_s *frame_par,
					 struct vframe_s *vf)
{
	struct f2v_vphase_s *vphase;
	u32 misc_off, vin_type;
	struct vppfilter_mode_s *vpp_filter;
	struct hw_pps_reg_s *vd_pps_reg;
	u8 vpp_index;
	u8 layer_id = 0;
	u32 slice = 1;
	int i = 0;

	if (!layer || !frame_par || !vf)
		return;
	if (layer->aisr_mif_setting.aisr_enable)
		return;
	layer_id = layer->layer_id;
	if (!glayer_info[layer_id].pps_support)
		return;

	vpp_filter = &frame_par->vpp_filter;
	misc_off = layer->misc_reg_offt;
	vin_type = vf->type & VIDTYPE_TYPEMASK;
	vpp_index = layer->vpp_index;
	vd_pps_reg = &layer->pps_reg;
	slice = get_slice_num(layer_id);
	for (i = 0; i < slice; i++) {
		/* vertical phase */
		vphase = &frame_par->VPP_vf_ini_phase_
			[vpp_phase_table[vin_type]
			[layer->vout_type]];
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_pps_reg->vd_vsc_init_phase,
			((u32)(vphase->phase) << 8));

		if (vphase->repeat_skip >= 0) {
			/* skip lines */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_vsc_phase_ctrl,
				skip_tab[vphase->repeat_skip],
				VPP_PHASECTL_INIRCVNUMT_BIT,
				VPP_PHASECTL_INIRCVNUM_WID +
				VPP_PHASECTL_INIRPTNUM_WID);
		} else {
			/* repeat first line */
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_vsc_phase_ctrl,
				4,
				VPP_PHASECTL_INIRCVNUMT_BIT,
				VPP_PHASECTL_INIRCVNUM_WID);
			cur_dev->rdma_func[vpp_index].rdma_wr_bits
				(vd_pps_reg->vd_vsc_phase_ctrl,
				1 - vphase->repeat_skip,
				VPP_PHASECTL_INIRPTNUMT_BIT,
				VPP_PHASECTL_INIRPTNUM_WID);
		}

		cur_dev->rdma_func[vpp_index].rdma_wr_bits
			(vd_pps_reg->vd_vsc_phase_ctrl,
			(vpp_filter->vpp_vert_coeff[0] == 2) ? 1 : 0,
			VPP_PHASECTL_DOUBLELINE_BIT,
			VPP_PHASECTL_DOUBLELINE_WID);
	}
}

/*********************************************************
 * Vpp APIs
 *********************************************************/
void set_video_mute(u32 owner, bool on)
{
	set_video_mute_info(owner, on);
}
EXPORT_SYMBOL(set_video_mute);

bool get_video_mute_val(u32 owner)
{
	return video_mute_array[owner];
}
EXPORT_SYMBOL(get_video_mute_val);

int get_video_mute(void)
{
	return video_mute_status;
}
EXPORT_SYMBOL(get_video_mute);

void set_output_mute(bool on)
{
	output_mute_on = on;
}
EXPORT_SYMBOL(set_output_mute);

int get_output_mute(void)
{
	return output_mute_status;
}
EXPORT_SYMBOL(get_output_mute);

void set_vdx_test_pattern(u32 index, bool on, u32 color)
{
	vdx_test_pattern_on[index] = on;
	vdx_color[index] = color;
	if (on)
		video_testpattern_status[index] = VIDEO_TESTPATTERN_OFF;
	else
		video_testpattern_status[index] = VIDEO_TESTPATTERN_ON;
}
EXPORT_SYMBOL(set_vdx_test_pattern);

void get_vdx_test_pattern(u32 index, bool *on, u32 *color)
{
	*on = vdx_test_pattern_on[index];
	*color = vdx_color[index];
}
EXPORT_SYMBOL(get_vdx_test_pattern);

void set_postblend_test_pattern(bool on, u32 color)
{
	postblend_test_pattern_on = on;
	postblend_color = color;
	if (on)
		postblend_testpattern_status = VIDEO_TESTPATTERN_OFF;
	else
		postblend_testpattern_status = VIDEO_TESTPATTERN_ON;
}
EXPORT_SYMBOL(set_postblend_test_pattern);

void get_postblend_test_pattern(bool *on, u32 *color)
{
	*on = postblend_test_pattern_on;
	*color = postblend_color;
}
EXPORT_SYMBOL(get_postblend_test_pattern);

static inline bool is_tv_panel(void)
{
	const struct vinfo_s *vinfo = get_current_vinfo();

	if (!vinfo || vinfo->mode == VMODE_INVALID)
		return false;

	/*panel*/
	if (vinfo->viu_color_fmt == COLOR_FMT_RGB444 &&
		(get_cpu_type() == MESON_CPU_MAJOR_ID_TL1 || cur_dev->is_tv_panel))
		return true;
	else
		return false;
}

static inline void mute_vpp(void)
{
	u32 black_val;
	u8 vpp_index = VPP0;

	/*black_val = (0x0 << 20) | (0x0 << 10) | 0;*/ /* RGB */
	black_val = (0x0 << 20) | (0x200 << 10) | 0x200; /* YUV */

	if (is_tv_panel()) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC0,
			black_val);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC1,
			black_val);
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC0, black_val);
		WRITE_VCBUS_REG(VPP_VD1_CLIP_MISC1, black_val);
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_CLIP_MISC0,
			black_val);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_CLIP_MISC1,
			black_val);
		WRITE_VCBUS_REG(VPP_CLIP_MISC0, black_val);
		WRITE_VCBUS_REG(VPP_CLIP_MISC1, black_val);
	}
}

static inline void unmute_vpp(void)
{
	u8 vpp_index = VPP0;

	if (is_tv_panel()) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC0,
			(0x3ff << 20) |
			(0x3ff << 10) |
			0x3ff);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_VD1_CLIP_MISC1,
			(0x0 << 20) |
			(0x0 << 10) | 0x0);
	} else {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_CLIP_MISC0,
			(0x3ff << 20) |
			(0x3ff << 10) |
			0x3ff);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_CLIP_MISC1,
			(0x0 << 20) |
			(0x0 << 10) | 0x0);
	}
}

int set_video_mute_info(u32 owner, bool on)
{
	if (on) {
		if (video_mute_array[owner])
			return -EINVAL;
		video_mute_array[owner] = true;
		pr_info("%d mute video\n", owner);
	} else {
		if (!video_mute_array[owner])
			return -EINVAL;
		video_mute_array[owner] = false;
		pr_info("%d unmute video\n", owner);
	}
	return 0;
}

void get_video_mute_info(void)
{
	int i;

	pr_info("video mute owner list:\n");
	for (i = 0; i < MAX_VIDEO_MUTE_OWNER; i++) {
		if (video_mute_array[i])
			pr_info("%d\n", i);
	}
}

static void check_video_mute_state(void)
{
	int i;

	for (i = 0; i < MAX_VIDEO_MUTE_OWNER; i++) {
		if (video_mute_array[i]) {
			video_mute_on = true;
			return;
		}
	}
	video_mute_on = false;
}

void check_video_mute(void)
{
	check_video_mute_state();
	if (video_mute_on) {
		if (is_amdv_on()) {
			if (is_tv_panel()) {
				/*tv mode, mute vpp*/
				if (video_mute_status != VIDEO_MUTE_ON_VPP) {
					/* vpp black */
					mute_vpp();
					pr_info("DV: %s: VIDEO_MUTE_ON_VPP\n", __func__);
				}
				video_mute_status = VIDEO_MUTE_ON_VPP;
			} else {
				/* core 3 black */
				if (video_mute_status != VIDEO_MUTE_ON_DV) {
					amdv_set_toggle_flag(1);
					pr_info("DOLBY: %s: VIDEO_MUTE_ON_DV\n", __func__);
				}
				video_mute_status = VIDEO_MUTE_ON_DV;
			}
		} else {
			if (video_mute_status != VIDEO_MUTE_ON_VPP) {
				mute_vpp();
				pr_info("%s: VIDEO_MUTE_ON_VPP\n", __func__);
			}
			video_mute_status = VIDEO_MUTE_ON_VPP;
		}
	} else {
		if (is_amdv_on()) {
			if (is_tv_panel()) {
				/*tv mode, unmute vpp*/
				if (video_mute_status != VIDEO_MUTE_OFF) {
					unmute_vpp();
					pr_info("DV: %s: VIDEO_MUTE_OFF dv off\n", __func__);
				}
				video_mute_status = VIDEO_MUTE_OFF;
			} else {
				if (video_mute_status != VIDEO_MUTE_OFF) {
					amdv_set_toggle_flag(2);
					pr_info("DOLBY: %s: VIDEO_MUTE_OFF dv off\n", __func__);
				}
				video_mute_status = VIDEO_MUTE_OFF;
			}
		} else {
			if (video_mute_status != VIDEO_MUTE_OFF) {
				unmute_vpp();
				pr_info("%s: VIDEO_MUTE_OFF vpp\n", __func__);
			}
			video_mute_status = VIDEO_MUTE_OFF;
		}
	}
}

static inline void mute_output(void)
{
	u32 black_val;
	u8 vpp_index = VPP0;

	if (is_tv_panel())
		black_val = (0x0 << 20) | (0x0 << 10) | 0;
	else
		black_val = (0x0 << 20) | (0x200 << 10) | 0x200; /* YUV */

	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_CLIP_MISC0,
		black_val);
	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_CLIP_MISC1,
		black_val);
	WRITE_VCBUS_REG(VPP_CLIP_MISC0, black_val);
	WRITE_VCBUS_REG(VPP_CLIP_MISC1, black_val);
}

static inline void unmute_output(void)
{
	u8 vpp_index = VPP0;

	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_CLIP_MISC0,
		(0x3ff << 20) |
		(0x3ff << 10) |
		0x3ff);
	cur_dev->rdma_func[vpp_index].rdma_wr
		(VPP_CLIP_MISC1,
		(0x0 << 20) |
		(0x0 << 10) | 0x0);
}

static void check_output_mute(void)
{
	if (output_mute_on) {
		if (output_mute_status != VIDEO_MUTE_ON_VPP) {
			/* vpp black */
			mute_output();
			pr_info("%s: VPP OUTPUT_MUTE_ON\n", __func__);
		}
		output_mute_status = VIDEO_MUTE_ON_VPP;
	} else {
		if (output_mute_status != VIDEO_MUTE_OFF) {
			unmute_output();
			pr_info("%s: VPP OUTPUT_MUTE_OFF vpp\n", __func__);
		}
		output_mute_status = VIDEO_MUTE_OFF;
	}
}

static inline void vdx_test_pattern_output(u32 index, u32 on, u32 color)
{
	u8 vpp_index = VPP0;
	u32 vdx_clip_misc0, vdx_clip_misc1;

	switch (index) {
	case 0:
		vdx_clip_misc0 = viu_misc_reg.vd1_clip_misc0;
		vdx_clip_misc1 = viu_misc_reg.vd1_clip_misc1;
		break;
	case 1:
		vdx_clip_misc0 = viu_misc_reg.vd2_clip_misc0;
		vdx_clip_misc1 = viu_misc_reg.vd2_clip_misc1;
		break;
	case 2:
		vdx_clip_misc0 = viu_misc_reg.vd3_clip_misc0;
		vdx_clip_misc1 = viu_misc_reg.vd3_clip_misc1;
		break;
	default:
		return;
	}
	if (on) {
		if (!is_tv_panel()) {
			u32 R, G, B;

			R = (color & 0xff0000) >> 16;
			G = (color & 0xff00) >> 8;
			B = color & 0xff;
			pr_info("R=%x, G=%x, B=%x\n", R, G, B);
			color = (R << 22) | (G << 12) | (B << 2);
		} else {
			u32 Y, U, V;
			int R, G, B;

			R = ((color & 0xff0000) >> 16);
			G = ((color & 0xff00) >> 8);
			B = (color & 0xff);
			Y = 257 * R / 1000 +
				504 * G / 1000 +
				98 * B / 1000 + 16;
			V = 439 * R / 1000 -
				368 * G / 1000 -
				71 * B / 1000 + 128;
			U = -148 * R / 1000 -
				291 * G / 1000 +
				439 * B / 1000 + 128;
			pr_info("Y=%x, U=%x, V=%x\n", Y, U, V);
			color = (Y << 22) | (U << 12) | V << 2; /* YUV */
		}

		if (cur_dev->display_module != S5_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vdx_clip_misc0,
				color);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vdx_clip_misc1,
				color);
			WRITE_VCBUS_REG(vdx_clip_misc0, color);
			WRITE_VCBUS_REG(vdx_clip_misc1, color);
		}
	} else {
		if (cur_dev->display_module != S5_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vdx_clip_misc0,
				(0x3ff << 20) |
				(0x3ff << 10) |
				0x3ff);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vdx_clip_misc1,
				(0x0 << 20) |
				(0x0 << 10) | 0x0);
		}
	}
}

static void check_video_pattern_output(void)
{
	int index;

	for (index = 0; index < MAX_VD_LAYER; index++) {
		if (vdx_test_pattern_on[index]) {
			if (video_testpattern_status[index] != VIDEO_TESTPATTERN_ON) {
				vdx_test_pattern_output(index, 1, vdx_color[index]);
				if (debug_flag & DEBUG_FLAG_BASIC_INFO)
					pr_info("%s: VD%d TESTPATTERN ON\n", __func__, index);
			}
			video_testpattern_status[index] = VIDEO_TESTPATTERN_ON;
		} else {
			if (video_testpattern_status[index] != VIDEO_TESTPATTERN_OFF) {
				vdx_test_pattern_output(index, 0, vdx_color[index]);
				if (debug_flag & DEBUG_FLAG_BASIC_INFO)
					pr_info("%s: VD%d TESTPATTERN OFF\n", __func__, index);
			}
			video_testpattern_status[index] = VIDEO_TESTPATTERN_OFF;
		}
	}
}

static inline void postblend_test_pattern_output(u32 on, u32 color)
{
	u8 vpp_index = VPP0;
	static u32 blend_din_en_save;

	if (on) {
		if (is_tv_panel()) {
			u32 R, G, B;

			R = (color & 0xff0000) >> 16;
			G = (color & 0xff00) >> 8;
			B = color & 0xff;
			pr_info("R=%x, G=%x, B=%x\n", R, G, B);
			color = (R << 22) | (G << 12) | (B << 2);
		} else {
			u32 Y, U, V;
			int R, G, B;

			R = ((color & 0xff0000) >> 16);
			G = ((color & 0xff00) >> 8);
			B = (color & 0xff);
			Y = 257 * R / 1000 +
				504 * G / 1000 +
				98 * B / 1000 + 16;
			V = 439 * R / 1000 -
				368 * G / 1000 -
				71 * B / 1000 + 128;
			U = -148 * R / 1000 -
				291 * G / 1000 +
				439 * B / 1000 + 128;
			pr_info("Y=%x, U=%x, V=%x\n", Y, U, V);
			color = (Y << 20) | (U << 10) | V << 2; /* YUV */
		}

		if (cur_dev->display_module != C3_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_CLIP_MISC0,
				color);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_CLIP_MISC1,
				color);
			WRITE_VCBUS_REG(VPP_CLIP_MISC0, color);
			WRITE_VCBUS_REG(VPP_CLIP_MISC1, color);
		} else {
			blend_din_en_save = READ_VCBUS_REG(VPU_VOUT_BLEND_CTRL);
			WRITE_VCBUS_REG(VPU_VOUT_BLEND_DUMDATA, color);
			WRITE_VCBUS_REG(VPU_VOUT_BLEND_CTRL, 0x7);
		}
	} else {
		if (cur_dev->display_module != C3_DISPLAY_MODULE) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_CLIP_MISC0,
				(0x3ff << 20) |
				(0x3ff << 10) |
				0x3ff);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_CLIP_MISC1,
				(0x0 << 20) |
				(0x0 << 10) | 0x0);
		} else {
			WRITE_VCBUS_REG(VPU_VOUT_BLEND_DUMDATA, 0x0);
			WRITE_VCBUS_REG(VPU_VOUT_BLEND_CTRL,
				blend_din_en_save);
		}
	}
}

static void check_postblend_pattern_output(void)
{
	if (postblend_test_pattern_on) {
		if (postblend_testpattern_status != VIDEO_TESTPATTERN_ON) {
			postblend_test_pattern_output(1, postblend_color);
			if (debug_flag & DEBUG_FLAG_BASIC_INFO)
				pr_info("%s: VPP TESTPATTERN ON\n", __func__);
		}
		postblend_testpattern_status = VIDEO_TESTPATTERN_ON;
	} else {
		if (postblend_testpattern_status != VIDEO_TESTPATTERN_OFF) {
			postblend_test_pattern_output(0, postblend_color);
			if (debug_flag & DEBUG_FLAG_BASIC_INFO)
				pr_info("%s: VPP TESTPATTERN OFF\n", __func__);
		}
		postblend_testpattern_status = VIDEO_TESTPATTERN_OFF;
	}
}

static int vpp_zorder_check(void)
{
	int force_flush = 0;
	u32 layer0_sel = 0; /* vd1 */
	u32 layer1_sel = 1; /* vd2 */

	if (legacy_vpp)
		return 0;

	if (glayer_info[1].zorder < glayer_info[0].zorder) {
		layer0_sel = 1;
		layer1_sel = 0;
		if (glayer_info[0].zorder >= reference_zorder &&
		    glayer_info[1].zorder < reference_zorder)
			layer0_sel++;
	} else {
		layer0_sel = 0;
		layer1_sel = 1;
		if (glayer_info[1].zorder >= reference_zorder &&
		    glayer_info[0].zorder < reference_zorder)
			layer1_sel++;
	}
	if (glayer_info[0].zorder >= reference_zorder &&
	    glayer_info[1].zorder >= reference_zorder) {
		if (vd_layer[0].enabled && vd_layer[1].enabled) {
			if (glayer_info[1].zorder < glayer_info[0].zorder) {
				layer0_sel = 1;
				layer1_sel = 0;
			} else {
				layer0_sel = 0;
				layer1_sel = 1;
			}
		} else if (vd_layer[0].enabled) {
			layer0_sel = 2;
			layer1_sel = 0;
		} else if (vd_layer[1].enabled) {
			layer0_sel = 0;
			layer1_sel = 2;
		}
	}

	glayer_info[0].cur_sel_port = layer0_sel;
	glayer_info[1].cur_sel_port = layer1_sel;

	if (glayer_info[0].cur_sel_port != glayer_info[0].last_sel_port ||
	    glayer_info[1].cur_sel_port != glayer_info[1].last_sel_port) {
		force_flush = 1;
		glayer_info[0].last_sel_port = glayer_info[0].cur_sel_port;
		glayer_info[1].last_sel_port = glayer_info[1].cur_sel_port;
	}
	return force_flush;
}

static void post_blend_dummy_data_update(u32 vpp_index)
{
	rdma_wr_op rdma_wr = cur_dev->rdma_func[vpp_index].rdma_wr;
	u32 bg_color;

	if (vd_layer[0].enabled)
		bg_color = vd_layer[0].video_en_bg_color;
	else
		bg_color = vd_layer[0].video_dis_bg_color;

	/* for channel background setting
	 * no auto flag, return
	 */
	if (!(bg_color & VIDEO_AUTO_POST_BLEND_DUMMY))
		return;

	if (!legacy_vpp) {
		rdma_wr(VPP_POST_BLEND_BLEND_DUMMY_DATA, bg_color & 0x00ffffff);
		rdma_wr(VPP_POST_BLEND_DUMMY_ALPHA,
			vd_layer[0].dummy_alpha);
		if (vpp_index == 1) {
			rdma_wr(VPP1_BLEND_BLEND_DUMMY_DATA, bg_color & 0x00ffffff);
			rdma_wr(VPP1_BLEND_DUMMY_ALPHA,
				vd_layer[0].dummy_alpha);
		}
		if (vpp_index == 2) {
			rdma_wr(VPP2_BLEND_BLEND_DUMMY_DATA, bg_color & 0x00ffffff);
			rdma_wr(VPP2_BLEND_DUMMY_ALPHA,
				vd_layer[0].dummy_alpha);
		}
	} else {
		rdma_wr(VPP_DUMMY_DATA1, bg_color & 0x00ffffff);
	}
}

void vpp_blend_update(const struct vinfo_s *vinfo, u8 vpp_index)
{
	static u32 vd1_enabled;
	static u32 vpp_misc_set_save;
	u32 vpp_misc_save, vpp_misc_set, mode = 0;
	u32 vpp_off = cur_dev->vpp_off;
	int video1_off_req = 0;
	int video2_off_req = 0;
	unsigned long flags;
	struct vpp_frame_par_s *vd1_frame_par =
		vd_layer[0].cur_frame_par;
	bool force_flush = false;
	int i;

	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		if (vd1_vd2_mux) {
			vd_clip_setting(VPP0, 1, &vd_layer[0].clip_setting);
		} else {
			vd_clip_setting(VPP0, 0, &vd_layer[0].clip_setting);
			vd_clip_setting(VPP0, 1, &vd_layer[1].clip_setting);
		}
		if (cur_dev->max_vd_layers == 3)
			vd_clip_setting(VPP0, 2, &vd_layer[2].clip_setting);
	}

	check_video_pattern_output();
	check_postblend_pattern_output();
	check_video_mute();
	check_output_mute();

	if (vd_layer[0].enable_3d_mode == mode_3d_mvc_enable)
		mode |= COMPOSE_MODE_3D;
	else if (is_amdv_on() && last_el_status)
		mode |= COMPOSE_MODE_DV;
	if (bypass_cm)
		mode |= COMPOSE_MODE_BYPASS_CM;

	/* check the vpp post size first */
	if (!legacy_vpp && vinfo) {
		u32 read_value = cur_dev->rdma_func[vpp_index].rdma_rd
			(VPP_POSTBLEND_H_SIZE + vpp_off);
		if (((vinfo->field_height << 16) | vinfo->width)
			!= read_value)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_POSTBLEND_H_SIZE + vpp_off,
				((vinfo->field_height << 16) | vinfo->width));
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_OUT_H_V_SIZE + vpp_off,
			vinfo->width << 16 |
			vinfo->field_height);
	} else if (vinfo) {
		if (cur_dev->rdma_func[vpp_index].rdma_rd
			(VPP_POSTBLEND_H_SIZE + vpp_off)
			!= vinfo->width)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_POSTBLEND_H_SIZE + vpp_off,
				vinfo->width);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_OUT_H_V_SIZE + vpp_off,
			vinfo->width << 16 |
			vinfo->field_height);
	}

	vpp_misc_save = READ_VCBUS_REG(VPP_MISC + vpp_off);

	vpp_misc_set = vpp_misc_save;
	if (is_amdv_on() && !is_tv_panel())
		vpp_misc_set &= ~VPP_CM_ENABLE;
	else if (mode & COMPOSE_MODE_BYPASS_CM)
		vpp_misc_set &= ~VPP_CM_ENABLE;
	else
		vpp_misc_set |= VPP_CM_ENABLE;

	if (update_osd_vpp_misc && legacy_vpp) {
		vpp_misc_set &= ~osd_vpp_misc_mask;
		vpp_misc_set |=
			(osd_vpp_misc & osd_vpp_misc_mask);
		if (vpp_misc_set &
		    (VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND))
			vpp_misc_set |= VPP_POSTBLEND_EN;
	}

	for (i = 0; i < MAX_VD_LAYERS; i++) {
		vd_layer[i].pre_blend_en = 0;
		vd_layer[i].post_blend_en = 0;
	}
	if (likely(vd_layer[0].onoff_state != VIDEO_ENABLE_STATE_IDLE)) {
		/* state change for video layer enable/disable */
		spin_lock_irqsave(&video_onoff_lock, flags);
		if (vd_layer[0].onoff_state == VIDEO_ENABLE_STATE_ON_REQ) {
			/*
			 * the video layer is enabled one vsync later,assumming
			 * all registers are ready from RDMA.
			 */
			vd_layer[0].onoff_state = VIDEO_ENABLE_STATE_ON_PENDING;
		} else if (vd_layer[0].onoff_state ==
			VIDEO_ENABLE_STATE_ON_PENDING) {
			vd_layer[0].pre_blend_en = 1;
			vd_layer[0].post_blend_en = 1;
			vpp_misc_set |=
				VPP_VD1_PREBLEND |
				VPP_VD1_POSTBLEND |
				VPP_POSTBLEND_EN;
			vd_layer[0].onoff_state = VIDEO_ENABLE_STATE_IDLE;
			vd_layer[0].onoff_time = jiffies_to_msecs(jiffies);
			if (vd_layer[0].global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info("VIDEO: VsyncEnableVideoLayer\n");
#ifdef CONFIG_AMLOGIC_VPU
			vpu_delay_work_flag |=
				VPU_VIDEO_LAYER1_CHANGED;
#endif
			force_flush = true;
		} else if (vd_layer[0].onoff_state ==
			VIDEO_ENABLE_STATE_OFF_REQ) {
			vd_layer[0].pre_blend_en = 0;
			vd_layer[0].post_blend_en = 0;
			vpp_misc_set &= ~(VPP_VD1_PREBLEND |
				  VPP_VD1_POSTBLEND);
			if (mode & COMPOSE_MODE_3D)
				vpp_misc_set &= ~(VPP_VD2_PREBLEND |
					VPP_PREBLEND_EN);
			vd_layer[0].onoff_state = VIDEO_ENABLE_STATE_IDLE;
			vd_layer[0].onoff_time = jiffies_to_msecs(jiffies);
#ifdef CONFIG_AMLOGIC_VPU
			vpu_delay_work_flag |=
				VPU_VIDEO_LAYER1_CHANGED;
#endif
			if (vd_layer[0].global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info("VIDEO: VsyncDisableVideoLayer\n");
			video1_off_req = 1;
			force_flush = true;
		}
		spin_unlock_irqrestore(&video_onoff_lock, flags);
	} else if (vd_layer[0].enabled) {
		vd_layer[0].pre_blend_en = 1;
		vd_layer[0].post_blend_en = 1;
		vpp_misc_set |=
			VPP_VD1_PREBLEND |
			VPP_VD1_POSTBLEND |
			VPP_POSTBLEND_EN;
	}

	if (likely(vd_layer[1].onoff_state != VIDEO_ENABLE_STATE_IDLE)) {
		/* state change for video layer2 enable/disable */
		spin_lock_irqsave(&video2_onoff_lock, flags);
		if (vd_layer[1].onoff_state == VIDEO_ENABLE_STATE_ON_REQ) {
			/*
			 * the video layer 2
			 * is enabled one vsync later, assumming
			 * all registers are ready from RDMA.
			 */
			vd_layer[1].onoff_state = VIDEO_ENABLE_STATE_ON_PENDING;
		} else if (vd_layer[1].onoff_state ==
			VIDEO_ENABLE_STATE_ON_PENDING) {
			if (mode & COMPOSE_MODE_3D) {
				vd_layer[1].pre_blend_en = 1;
				vd_layer[1].post_blend_en = 0;
				vpp_misc_set |= VPP_VD2_PREBLEND |
					VPP_PREBLEND_EN;
				vpp_misc_set &= ~VPP_VD2_POSTBLEND;
			} else if (vd_layer[0].enabled &&
				(mode & COMPOSE_MODE_DV)) {
				vd_layer[1].pre_blend_en = 0;
				vd_layer[1].post_blend_en = 0;
				/* DV el case */
				vpp_misc_set &= ~(VPP_VD2_PREBLEND |
					VPP_VD2_POSTBLEND | VPP_PREBLEND_EN);
			} else if (vd_layer[1].dispbuf) {
				vd_layer[1].pre_blend_en = 0;
				vd_layer[1].post_blend_en = 1;
				vpp_misc_set &=
					~(VPP_PREBLEND_EN | VPP_VD2_PREBLEND);
				vpp_misc_set |= VPP_VD2_POSTBLEND |
					VPP_POSTBLEND_EN;
			} else {
				vd_layer[1].pre_blend_en = 1;
				vpp_misc_set |= VPP_VD2_PREBLEND |
					VPP_PREBLEND_EN;
			}
			vpp_misc_set |= (vd_layer[1].layer_alpha
				<< VPP_VD2_ALPHA_BIT);
			vd_layer[1].onoff_state = VIDEO_ENABLE_STATE_IDLE;
			vd_layer[1].onoff_time = jiffies_to_msecs(jiffies);
#ifdef CONFIG_AMLOGIC_VPU
			vpu_delay_work_flag |=
				VPU_VIDEO_LAYER2_CHANGED;
#endif
			if (vd_layer[1].global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info("VIDEO: VsyncEnableVideoLayer2\n");
			force_flush = true;
		} else if (vd_layer[1].onoff_state ==
			VIDEO_ENABLE_STATE_OFF_REQ) {
			vd_layer[1].pre_blend_en = 0;
			vd_layer[1].post_blend_en = 0;
			vpp_misc_set &= ~(VPP_VD2_PREBLEND |
				VPP_VD2_POSTBLEND | VPP_PREBLEND_EN
				| (0x1ff << VPP_VD2_ALPHA_BIT));
			vd_layer[1].onoff_state = VIDEO_ENABLE_STATE_IDLE;
			vd_layer[1].onoff_time = jiffies_to_msecs(jiffies);
#ifdef CONFIG_AMLOGIC_VPU
			vpu_delay_work_flag |=
				VPU_VIDEO_LAYER2_CHANGED;
#endif
			if (vd_layer[1].global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info("VIDEO: VsyncDisableVideoLayer2\n");
			video2_off_req = 1;
			force_flush = true;
		}
		spin_unlock_irqrestore(&video2_onoff_lock, flags);
	} else if (vd_layer[1].enabled) {
		if (mode & COMPOSE_MODE_3D) {
			vd_layer[1].pre_blend_en = 1;
			vd_layer[1].post_blend_en = 0;
			vpp_misc_set |= VPP_VD2_PREBLEND |
				VPP_PREBLEND_EN;
			vpp_misc_set &= ~VPP_VD2_POSTBLEND;
		} else if (vd_layer[0].enabled &&
			(mode & COMPOSE_MODE_DV)) {
			vd_layer[1].pre_blend_en = 0;
			vd_layer[1].post_blend_en = 0;
			/* DV el case */
			vpp_misc_set &= ~(VPP_VD2_PREBLEND |
				VPP_VD2_POSTBLEND | VPP_PREBLEND_EN);
		} else if (vd_layer[1].dispbuf) {
			vd_layer[1].pre_blend_en = 0;
			vd_layer[1].post_blend_en = 1;
			vpp_misc_set &=
				~(VPP_PREBLEND_EN | VPP_VD2_PREBLEND);
			vpp_misc_set |= VPP_VD2_POSTBLEND |
				VPP_POSTBLEND_EN;
		} else {
			vd_layer[1].pre_blend_en = 1;
			vpp_misc_set |= VPP_VD2_PREBLEND |
				VPP_PREBLEND_EN;
		}
		vpp_misc_set |= (vd_layer[1].layer_alpha
			<< VPP_VD2_ALPHA_BIT);
	}

	if ((vd_layer[0].global_output == 0 && !vd_layer[0].force_black) ||
	    vd_layer[0].force_disable ||
	    black_threshold_check(0)) {
		if (vd_layer[0].enabled)
			force_flush = true;
		vd_layer[0].enabled = 0;
		/* preblend need disable together */
		vpp_misc_set &= ~(VPP_VD1_PREBLEND |
			VPP_VD2_PREBLEND |
			VPP_VD1_POSTBLEND |
			VPP_PREBLEND_EN);
		vd_layer[0].pre_blend_en = 0;
		vd_layer[0].post_blend_en = 0;
	} else {
		if (vd_layer[0].enabled != vd_layer[0].enabled_status_saved)
			force_flush = true;
		vd_layer[0].enabled = vd_layer[0].enabled_status_saved;
		if (vd_layer[0].enabled) {
			vd_layer[0].pre_blend_en = 1;
			vd_layer[0].post_blend_en = 1;
		} else {
			vd_layer[0].pre_blend_en = 0;
			vd_layer[0].post_blend_en = 0;
		}
	}
	if (!vd_layer[0].enabled &&
	    ((vpp_misc_set & VPP_VD1_POSTBLEND) ||
	     (vpp_misc_set & VPP_VD1_PREBLEND)))
		vpp_misc_set &= ~(VPP_VD1_PREBLEND |
			VPP_VD1_POSTBLEND |
			VPP_PREBLEND_EN);

	if (vd1_enabled != vd_layer[0].enabled) {
		if (vd_layer[0].enabled) {
			video_prop_status &= ~VIDEO_PROP_CHANGE_DISABLE;
			video_prop_status |= VIDEO_PROP_CHANGE_ENABLE;
		} else {
			video_prop_status &= ~VIDEO_PROP_CHANGE_ENABLE;
			video_prop_status |= VIDEO_PROP_CHANGE_DISABLE;
		}
		if (vd_layer[0].global_debug & DEBUG_FLAG_TRACE_EVENT)
			pr_info("VD1 enable/disable status changed: %s->%s.\n",
				vd1_enabled ? "enable" : "disable",
				vd_layer[0].enabled ? "enable" : "disable");
		vd1_enabled = vd_layer[0].enabled;
	}

	if (!(mode & COMPOSE_MODE_3D) &&
	    (vd_layer[1].global_output == 0 ||
	     vd_layer[1].force_disable ||
	     black_threshold_check(1))) {
		if (vd_layer[1].enabled)
			force_flush = true;
		vd_layer[1].enabled = 0;
		vpp_misc_set &= ~(VPP_VD2_PREBLEND |
			VPP_VD2_POSTBLEND);
		vd_layer[1].pre_blend_en = 0;
		vd_layer[1].post_blend_en = 0;
	} else {
		if (vd_layer[1].enabled != vd_layer[1].enabled_status_saved)
			force_flush = true;
		vd_layer[1].enabled = vd_layer[1].enabled_status_saved;
		if (vd_layer[1].enabled) {
			if (mode & COMPOSE_MODE_3D) {
				vd_layer[1].pre_blend_en = 1;
				vd_layer[1].post_blend_en = 0;
			} else if (vd_layer[0].enabled &&
				(mode & COMPOSE_MODE_DV)) {
				vd_layer[1].pre_blend_en = 0;
				vd_layer[1].post_blend_en = 0;
			} else if (vd_layer[1].dispbuf) {
				vd_layer[1].pre_blend_en = 0;
				vd_layer[1].post_blend_en = 1;
			} else {
				vd_layer[1].pre_blend_en = 1;
			}
		} else {
			vd_layer[1].pre_blend_en = 0;
			vd_layer[1].post_blend_en = 0;
		}
	}
	if (!vd_layer[1].enabled &&
	    ((vpp_misc_set & VPP_VD2_POSTBLEND) ||
	     (vpp_misc_set & VPP_VD2_PREBLEND)))
		vpp_misc_set &= ~(VPP_VD2_PREBLEND |
			VPP_VD2_POSTBLEND);

	if (!vd_layer[0].enabled && is_local_vf(vd_layer[0].dispbuf)) {
		safe_switch_videolayer(0, false, true);
		video_keeper_new_frame_notify(vd_layer[0].keep_frame_id);
	}

	if (!vd_layer[1].enabled && is_local_vf(vd_layer[1].dispbuf)) {
		safe_switch_videolayer(1, false, true);
		video_keeper_new_frame_notify(vd_layer[0].keep_frame_id);
	}

	force_flush |= vpp_zorder_check();

	if (!legacy_vpp) {
		u32 set_value = 0;

		/* for sr core0, put it between prebld & pps as default */
		if (vd1_frame_par &&
		    (vd1_frame_par->sr_core_support & SUPER_CORE0_SUPPORT))
			if (vd1_frame_par->sr0_position)
				vpp_misc_set |=
					PREBLD_SR0_VD1_SCALER;
			else /* SR0_AFTER_DNLP */
				vpp_misc_set &=
					~PREBLD_SR0_VD1_SCALER;
		else
			vpp_misc_set |=
				PREBLD_SR0_VD1_SCALER;
		/* for sr core1, put it before post blend as default */
		if (vd1_frame_par &&
		    (vd1_frame_par->sr_core_support & SUPER_CORE1_SUPPORT))
			if (vd1_frame_par->sr1_position)
				vpp_misc_set |=
					DNLP_SR1_CM;
			else /* SR1_AFTER_POSTBLEN */
				vpp_misc_set &=
					~DNLP_SR1_CM;
		else
			vpp_misc_set |=
				DNLP_SR1_CM;

		vpp_misc_set &=
			((1 << 29) | VPP_CM_ENABLE |
			(0x1ff << VPP_VD2_ALPHA_BIT) |
			VPP_PREBLEND_EN |
			VPP_POSTBLEND_EN |
			0xf);

		/* if vd2 is bottom layer, need remove alpha for vd2 */
		/* 3d case vd2 preblend, need remove alpha for vd2 */
		if ((!vd_layer[0].post_blend_en &&
		    vd_layer[1].post_blend_en) ||
		    vd_layer[1].pre_blend_en) {
			vpp_misc_set &= ~(0x1ff << VPP_VD2_ALPHA_BIT);
			vpp_misc_set |= (vd_layer[1].layer_alpha
				<< VPP_VD2_ALPHA_BIT);
		}

		vpp_misc_save &=
			((1 << 29) | VPP_CM_ENABLE |
			(0x1ff << VPP_VD2_ALPHA_BIT) |
			VPP_PREBLEND_EN |
			VPP_POSTBLEND_EN |
			0xf);
		if (vpp_misc_set != vpp_misc_set_save || force_flush) {
			u32 port_val[3] = {0, 0, 0};
			u32 vd1_port, vd2_port, icnt;
			u32 post_blend_reg[3] = {
				VD1_BLEND_SRC_CTRL,
				VD2_BLEND_SRC_CTRL,
				OSD2_BLEND_SRC_CTRL
			};

			/* just reset the select port */
			if (glayer_info[0].cur_sel_port > 2 ||
			    glayer_info[1].cur_sel_port > 2) {
				glayer_info[0].cur_sel_port = 0;
				glayer_info[1].cur_sel_port = 1;
			}

			vd1_port = glayer_info[0].cur_sel_port;
			vd2_port = glayer_info[1].cur_sel_port;

			/* post bld premult*/
			port_val[0] |= (1 << 16);

			/* vd2 path sel */
			if (vd_layer[1].post_blend_en)
				port_val[1] |= (1 << 20);
			else
				port_val[1] &= ~(1 << 20);

			/* osd2 path sel */
			port_val[2] |= (1 << 20);

			if (vd_layer[0].post_blend_en) {
				 /* post src */
				port_val[vd1_port] |= (1 << 8);
				port_val[0] |=
					((1 << 4) | /* pre bld premult*/
					(1 << 0)); /* pre bld src 1 */
			} else {
				port_val[0] &= ~0xff;
			}

			if (vd_layer[1].post_blend_en)
				 /* post src */
				port_val[vd2_port] |= (2 << 8);
			else if (vd_layer[1].pre_blend_en)
				port_val[1] |=
					((1 << 4) | /* pre bld premult*/
					(2 << 0)); /* pre bld src 1 */

			for (icnt = 0; icnt < 3; icnt++)
				cur_dev->rdma_func[vpp_index].rdma_wr
					(post_blend_reg[icnt]
					+ vpp_off,
					port_val[icnt]);

			set_value = vpp_misc_set;
			set_value &=
				((1 << 29) | VPP_CM_ENABLE |
				(0x1ff << VPP_VD2_ALPHA_BIT) |
				VPP_POSTBLEND_EN |
				VPP_PREBLEND_EN |
				0xf);
#ifdef CONFIG_AMLOGIC_MEDIA_ENHANCEMENT_DOLBYVISION
			if (get_force_bypass_from_prebld_to_vadj1() &&
			    for_amdv_certification()) {
				/* t3/t5w, 1d93 bit0 -> 1d26 bit8*/
				set_value |= (1 << 8);
			}
#endif
			if (vd_layer[0].pre_blend_en ||
			    vd_layer[1].pre_blend_en ||
			    vd_layer[2].pre_blend_en)
				set_value |= VPP_PREBLEND_EN;
			if (vd_layer[0].post_blend_en ||
			    vd_layer[1].post_blend_en ||
			    vd_layer[2].post_blend_en)
				set_value |= VPP_POSTBLEND_EN;

			if (mode & COMPOSE_MODE_BYPASS_CM)
				set_value &= ~VPP_CM_ENABLE;
			set_value |= VPP_POSTBLEND_EN;
			set_value |= VPP_PREBLEND_EN;
			if (osd_preblend_en) {
				set_value |= VPP_VD1_POSTBLEND;
				set_value |= VPP_VD1_PREBLEND;
			}
			/* t5d bit 9:11 used by wm ctrl, chip after g12 not used bit 9:11, mask it*/
			set_value &= 0xfffff1ff;
			set_value |= (vpp_misc_save & 0xe00);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(VPP_MISC + vpp_off,
				set_value);
		}
	} else if (vpp_misc_save != vpp_misc_set) {
		cur_dev->rdma_func[vpp_index].rdma_wr
			(VPP_MISC + vpp_off,
			vpp_misc_set);
	}
	vpp_misc_set_save = vpp_misc_set;

	post_blend_dummy_data_update(vpp_index);

	if ((vd_layer[1].dispbuf && video2_off_req) ||
	    (!vd_layer[1].dispbuf &&
	     (video1_off_req || video2_off_req)))
		disable_vd2_blend(&vd_layer[1]);

	if (video1_off_req)
		disable_vd1_blend(&vd_layer[0]);
}

/*********************************************************
 * frame canvas APIs
 *********************************************************/
static bool is_vframe_changed
	(u8 layer_id,
	struct vframe_s *cur_vf,
	struct vframe_s *new_vf)
{
	u32 old_flag, new_flag;

	if (cur_vf && new_vf &&
	    (cur_vf->ratio_control & DISP_RATIO_ADAPTED_PICMODE) &&
	    cur_vf->ratio_control == new_vf->ratio_control &&
	    memcmp(&cur_vf->pic_mode, &new_vf->pic_mode,
		   sizeof(struct vframe_pic_mode_s)))
		return true;
	else if (cur_vf &&
		 memcmp(&cur_vf->pic_mode, &gpic_info[layer_id],
			sizeof(struct vframe_pic_mode_s)))
		return true;

	if (glayer_info[layer_id].reverse != reverse ||
	    glayer_info[layer_id].proc_3d_type != process_3d_type)
		return true;
	if (cur_vf && new_vf &&
	    (((cur_vf->type & VIDTYPE_COMPRESS) &&
	      (cur_vf->compWidth != new_vf->compWidth ||
	       cur_vf->compHeight != new_vf->compHeight)) ||
	     cur_vf->bufWidth != new_vf->bufWidth ||
	     cur_vf->width != new_vf->width ||
	     cur_vf->height != new_vf->height ||
	     cur_vf->sar_width != new_vf->sar_width ||
	     cur_vf->sar_height != new_vf->sar_height ||
	     cur_vf->bitdepth != new_vf->bitdepth ||
#ifdef CONFIG_AMLOGIC_MEDIA_TVIN
	     cur_vf->trans_fmt != new_vf->trans_fmt ||
#endif
	     cur_vf->ratio_control != new_vf->ratio_control ||
	     ((cur_vf->type_backup & VIDTYPE_INTERLACE) !=
	      (new_vf->type_backup & VIDTYPE_INTERLACE)) ||
	     cur_vf->type != new_vf->type))
		return true;

	if (cur_vf && new_vf) {
		old_flag = cur_vf->flag & VFRAME_FLAG_DISP_ATTR_MASK;
		new_flag = new_vf->flag & VFRAME_FLAG_DISP_ATTR_MASK;
		if (old_flag != new_flag)
			return true;
	}
	return false;
}

bool is_picmode_changed(u8 layer_id, struct vframe_s *vf)
{
	if (vf &&
	    memcmp(&vf->pic_mode, &gpic_info[layer_id],
			sizeof(struct vframe_pic_mode_s)))
		return true;
	return false;
}

static bool is_sr_phase_changed(void)
{
	struct sr_info_s *sr;
	u32 sr0_sharp_sr2_ctrl2;
	u32 sr1_sharp_sr2_ctrl2;
	static u32 sr0_sharp_sr2_ctrl2_pre;
	static u32 sr1_sharp_sr2_ctrl2_pre;
	bool changed = false;

	if (!cur_dev->aisr_support ||
	    !cur_dev->pps_auto_calc)
		return false;

	sr = &sr_info;
	sr0_sharp_sr2_ctrl2 =
		cur_dev->rdma_func[VPP0].rdma_rd
			(SRSHARP0_SHARP_SR2_CTRL2 + sr->sr_reg_offt);
	sr1_sharp_sr2_ctrl2 =
		cur_dev->rdma_func[VPP0].rdma_rd
			(SRSHARP1_SHARP_SR2_CTRL2 + sr->sr_reg_offt2);
	sr0_sharp_sr2_ctrl2 &= 0x7fff;
	sr1_sharp_sr2_ctrl2 &= 0x7fff;
	if (sr0_sharp_sr2_ctrl2 != sr0_sharp_sr2_ctrl2_pre ||
	    sr1_sharp_sr2_ctrl2 != sr1_sharp_sr2_ctrl2_pre)
		changed = true;
	sr0_sharp_sr2_ctrl2_pre = sr0_sharp_sr2_ctrl2;
	sr1_sharp_sr2_ctrl2_pre = sr1_sharp_sr2_ctrl2;
	return changed;
}

int get_layer_display_canvas(u8 layer_id)
{
	int ret = -1;
	struct video_layer_s *layer = NULL;

	if (layer_id >= MAX_VD_LAYER)
		return -1;
	layer = &vd_layer[layer_id];
	ret = READ_VCBUS_REG(layer->vd_mif_reg.vd_if0_canvas0);
	return ret;
}

static void canvas_update_for_mif(struct video_layer_s *layer,
			     struct vframe_s *vf)
{
	struct canvas_s cs0[2], cs1[2], cs2[2];
	u32 *cur_canvas_tbl;
	u8 cur_canvas_id;

	cur_canvas_id = layer->cur_canvas_id;
	cur_canvas_tbl =
		&layer->canvas_tbl[cur_canvas_id][0];

	if (vf->canvas0Addr != (u32)-1) {
		canvas_copy(vf->canvas0Addr & 0xff,
			    cur_canvas_tbl[0]);
		canvas_copy((vf->canvas0Addr >> 8) & 0xff,
			    cur_canvas_tbl[1]);
		canvas_copy((vf->canvas0Addr >> 16) & 0xff,
			    cur_canvas_tbl[2]);
		if (cur_dev->mif_linear) {
			canvas_read(cur_canvas_tbl[0], &cs0[0]);
			canvas_read(cur_canvas_tbl[1], &cs1[0]);
			canvas_read(cur_canvas_tbl[2], &cs2[0]);
			set_vd_mif_linear_cs(layer,
				&cs0[0], &cs1[0], &cs2[0],
				vf,
				0);
			if (layer->mif_setting.block_mode != cs0[0].blkmode) {
				layer->mif_setting.block_mode = cs0[0].blkmode;
				vd_set_blk_mode(layer,
					layer->mif_setting.block_mode);
			}
		}
	} else {
		vframe_canvas_set
			(&vf->canvas0_config[0],
			vf->plane_num,
			&cur_canvas_tbl[0]);
		if (cur_dev->mif_linear) {
			set_vd_mif_linear(layer,
				&vf->canvas0_config[0],
				vf->plane_num,
				vf,
				0);
			if (layer->mif_setting.block_mode !=
				vf->canvas0_config[0].block_mode) {
				layer->mif_setting.block_mode =
					vf->canvas0_config[0].block_mode;
				vd_set_blk_mode(layer,
					layer->mif_setting.block_mode);
			}
		}
	}
}

static void canvas_update_for_3d(struct video_layer_s *layer,
			     struct vframe_s *vf,
			     struct vpp_frame_par_s *cur_frame_par,
			     struct disp_info_s *disp_info,
			     bool is_mvc)
{
	struct canvas_s cs0[2], cs1[2], cs2[2];
	u32 *cur_canvas_tbl;
	u8 cur_canvas_id;

	cur_canvas_id = layer->cur_canvas_id;
	cur_canvas_tbl =
		&layer->canvas_tbl[cur_canvas_id][0];

	if (vf->canvas1Addr != (u32)-1) {
		canvas_copy
			(vf->canvas1Addr & 0xff,
			cur_canvas_tbl[3]);
		canvas_copy
			((vf->canvas1Addr >> 8) & 0xff,
			cur_canvas_tbl[4]);
		canvas_copy
			((vf->canvas1Addr >> 16) & 0xff,
			cur_canvas_tbl[5]);
		canvas_read(cur_canvas_tbl[3], &cs0[1]);
		canvas_read(cur_canvas_tbl[4], &cs1[1]);
		canvas_read(cur_canvas_tbl[5], &cs2[1]);
		if (cur_dev->mif_linear) {
			if (is_mvc) {
				set_vd_mif_linear_cs(&vd_layer[1],
					&cs0[1], &cs1[1], &cs2[1],
					vf,
					0);
				if (vd_layer[1].mif_setting.block_mode !=
				    cs0[1].blkmode) {
					vd_layer[1].mif_setting.block_mode =
						cs0[1].blkmode;
					vd_set_blk_mode(&vd_layer[1],
						vd_layer[1].mif_setting.block_mode);
				}
			}
			set_vd_mif_linear_cs(&vd_layer[0],
				&cs0[0], &cs1[0], &cs2[0],
				vf,
				1);
			if (is_mvc)
				set_vd_mif_linear_cs(&vd_layer[1],
					&cs0[0], &cs1[0], &cs2[0],
					vf,
					1);
			if (cur_frame_par && disp_info &&
			    (disp_info->proc_3d_type & MODE_3D_ENABLE) &&
			    (disp_info->proc_3d_type & MODE_3D_TO_2D_R) &&
			    cur_frame_par->vpp_2pic_mode == VPP_SELECT_PIC1) {
				set_vd_mif_linear_cs(&vd_layer[0],
					&cs0[1], &cs1[1], &cs2[1],
					vf,
					0);
				set_vd_mif_linear_cs(&vd_layer[0],
					&cs0[1], &cs1[1], &cs2[1],
					vf,
					1);
				if (is_mvc) {
					set_vd_mif_linear_cs(&vd_layer[1],
						&cs0[1], &cs1[1], &cs2[1],
						vf,
						0);
					set_vd_mif_linear_cs(&vd_layer[1],
						&cs0[1], &cs1[1], &cs2[1],
						vf,
						1);
				}
			}
		}
	} else {
		vframe_canvas_set
			(&vf->canvas1_config[0],
			vf->plane_num,
			&cur_canvas_tbl[3]);
		if (cur_dev->mif_linear) {
			if (is_mvc) {
				set_vd_mif_linear(&vd_layer[1],
					&vf->canvas1_config[0],
					vf->plane_num,
					vf,
					0);
				if (vd_layer[1].mif_setting.block_mode !=
					vf->canvas1_config[0].block_mode) {
					vd_layer[1].mif_setting.block_mode =
						vf->canvas1_config[0].block_mode;
					vd_set_blk_mode(&vd_layer[1],
						vd_layer[1].mif_setting.block_mode);
				}
			}
			set_vd_mif_linear(&vd_layer[0],
				&vf->canvas0_config[0],
				vf->plane_num,
				vf,
				1);
			if (is_mvc)
				set_vd_mif_linear(&vd_layer[1],
					&vf->canvas0_config[0],
					vf->plane_num,
					vf,
					1);
			if (cur_frame_par && disp_info &&
			    (disp_info->proc_3d_type & MODE_3D_ENABLE) &&
			    (disp_info->proc_3d_type & MODE_3D_TO_2D_R) &&
			    cur_frame_par->vpp_2pic_mode == VPP_SELECT_PIC1) {
				set_vd_mif_linear(&vd_layer[0],
					&vf->canvas1_config[0],
					vf->plane_num,
					vf,
					0);
				set_vd_mif_linear(&vd_layer[0],
					&vf->canvas1_config[0],
					vf->plane_num,
					vf,
					1);
				if (is_mvc) {
					set_vd_mif_linear(&vd_layer[1],
						&vf->canvas1_config[0],
						vf->plane_num,
						vf,
						0);
					set_vd_mif_linear(&vd_layer[1],
						&vf->canvas1_config[0],
						vf->plane_num,
						vf,
						1);
				}
			}
		}
	}
}

int set_layer_display_canvas(struct video_layer_s *layer,
			     struct vframe_s *vf,
			     struct vpp_frame_par_s *cur_frame_par,
			     struct disp_info_s *disp_info, u32 line)
{
	u32 *cur_canvas_tbl;
	u8 cur_canvas_id;
	bool is_mvc = false;
	bool update_mif = true;
	u8 vpp_index;
	u8 layer_id;
	struct hw_vd_reg_s *vd_mif_reg;
	struct hw_vd_reg_s *vd_mif_reg_mvc;
	struct hw_afbc_reg_s *vd_afbc_reg;

	layer_id = layer->layer_id;
	vpp_index = layer->vpp_index;

	if ((vf->type & VIDTYPE_MVC) && layer_id == 0)
		is_mvc = true;

	cur_canvas_id = layer->cur_canvas_id;
	cur_canvas_tbl =
		&layer->canvas_tbl[cur_canvas_id][0];
	if (layer_id >= MAX_VD_LAYER)
		return -1;
	vd_mif_reg = &vd_layer[layer_id].vd_mif_reg;
	vd_afbc_reg = &vd_layer[layer_id].vd_afbc_reg;
	vd_mif_reg_mvc = &vd_layer[1].vd_mif_reg;
	if (layer_id == 0 && layer->vd1_vd2_mux) {
		/* vd2 replaced vd1 mif reg */
		vd_mif_reg = &vd_layer[1].vd_mif_reg;
	}

	/* switch buffer */
	if (!glayer_info[layer_id].need_no_compress &&
	    (vf->type & VIDTYPE_COMPRESS)) {
	    /*coverity[overrun-local]*/
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_head_baddr,
			vf->compHeadAddr >> 4);
		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_afbc_reg->afbc_body_baddr,
			vf->compBodyAddr >> 4);
	}

#ifdef CONFIG_AMLOGIC_MEDIA_DEINTERLACE
	if (layer_id == 0) {
		if (is_di_post_mode(vf) &&
		    is_di_post_link_on())
			update_mif = false;

#ifdef ENABLE_PRE_LINK
		if (is_pre_link_on(layer) &&
		    !layer->need_disable_prelink &&
		    (IS_DI_POST(vf->type) || IS_DI_PRELINK(vf->di_flag)))
			update_mif = false;
#endif
	}
#endif
	if (vf->canvas0Addr == 0)
		update_mif = false;

	if (update_mif) {
		canvas_update_for_mif(layer, vf);

		vpp_trace_vframe("swap_vf", (void *)vf, vf->type, vf->flag, layer_id, 0);
		if (layer_id == 0 &&
		    (is_mvc || process_3d_type))
			canvas_update_for_3d(layer, vf,
					    cur_frame_par, disp_info,
					    is_mvc);

		cur_dev->rdma_func[vpp_index].rdma_wr
			(vd_mif_reg->vd_if0_canvas0,
			layer->disp_canvas[cur_canvas_id][0]);

		if (is_mvc)
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg_mvc->vd_if0_canvas0,
				layer->disp_canvas[cur_canvas_id][1]);
		if (cur_frame_par &&
		    cur_frame_par->vpp_2pic_mode == 1) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_canvas1,
				layer->disp_canvas[cur_canvas_id][0]);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_mif_reg_mvc->vd_if0_canvas1,
					layer->disp_canvas[cur_canvas_id][0]);
		} else {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_canvas1,
				layer->disp_canvas[cur_canvas_id][1]);
			if (is_mvc)
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_mif_reg_mvc->vd_if0_canvas1,
					layer->disp_canvas[cur_canvas_id][0]);
		}
		if (cur_frame_par && disp_info &&
		    (disp_info->proc_3d_type & MODE_3D_ENABLE) &&
		    (disp_info->proc_3d_type & MODE_3D_TO_2D_R) &&
		    cur_frame_par->vpp_2pic_mode == VPP_SELECT_PIC1) {
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_canvas0,
				layer->disp_canvas[cur_canvas_id][1]);
			cur_dev->rdma_func[vpp_index].rdma_wr
				(vd_mif_reg->vd_if0_canvas1,
				layer->disp_canvas[cur_canvas_id][1]);
			if (is_mvc) {
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_mif_reg_mvc->vd_if0_canvas0,
					layer->disp_canvas[cur_canvas_id][1]);
				cur_dev->rdma_func[vpp_index].rdma_wr
					(vd_mif_reg_mvc->vd_if0_canvas1,
					layer->disp_canvas[cur_canvas_id][1]);
			}
		}
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		layer->next_canvas_id = layer->cur_canvas_id ? 0 : 1;
#endif
	}
	if (layer->global_debug & DEBUG_FLAG_PRINT_FRAME_DETAIL) {
		struct canvas_s tmp;

		canvas_read(cur_canvas_tbl[0], &tmp);
		pr_info("%s %d: update_mif %d: vf:%p, y:%02x, adr:0x%lx (0x%lx), canvas0:%x, pnum:%d, type:%x, flag:%x, afbc:0x%lx-0x%lx, vf_ext:%px uvm_vf:%px di_flag:%x size:%d %d, vframe size:%d line:%d\n",
			__func__, layer_id, update_mif ? 1 : 0,
			vf, cur_canvas_tbl[0], tmp.addr, vf->canvas0_config[0].phy_addr,
			vf->canvas0Addr, vf->plane_num,
			vf->type, vf->flag,
			vf->compHeadAddr, vf->compBodyAddr,
			vf->vf_ext, vf->uvm_vf, vf->di_flag,
			vf->compWidth, vf->width, (u32)sizeof(struct vframe_s), line);
	}
	return 0;
}

u32 *get_canvase_tbl(u8 layer_id)
{
	struct video_layer_s *layer = NULL;

	if (layer_id >= MAX_VD_LAYER)
		return NULL;

	layer = &vd_layer[layer_id];
	return (u32 *)&layer->canvas_tbl[0][0];
}

static int update_afd_param(u8 id,
	struct vframe_s *vf,
	const struct vinfo_s *vinfo)
{
	struct afd_in_param in_p;
	struct afd_out_param out_p;
	bool is_comp = false;
	struct disp_info_s *layer_info = NULL;
	struct video_layer_s *layer = NULL;
	int ret;
	u32 frame_ar;

	if (id >= MAX_VD_LAYER)
		return -1;

	if (!vf || !vinfo)
		return -1;

	layer = &vd_layer[id];
	layer_info = &glayer_info[id];

	if (vf->type & VIDTYPE_COMPRESS)
		is_comp = true;
	memset(&in_p, 0, sizeof(struct afd_in_param));
	memset(&out_p, 0, sizeof(struct afd_out_param));

	in_p.screen_w = vinfo->width;
	in_p.screen_h = vinfo->height;
	in_p.screen_ar.numerator = vinfo->aspect_ratio_num;
	in_p.screen_ar.denominator = vinfo->aspect_ratio_den;

	in_p.video_w = is_comp ? vf->compWidth : vf->width;
	in_p.video_h = is_comp ? vf->compHeight : vf->height;

	frame_ar = (vf->ratio_control & DISP_RATIO_ASPECT_RATIO_MASK);
	frame_ar = frame_ar >> DISP_RATIO_ASPECT_RATIO_BIT;
	if (frame_ar == DISP_RATIO_ASPECT_RATIO_MAX) {
		in_p.video_ar.numerator = in_p.video_w * vf->sar_width;
		in_p.video_ar.denominator = in_p.video_h * vf->sar_height;
	} else if (frame_ar != 0) {
		/* frame_width/frame_height = 0x100/ar */
		in_p.video_ar.numerator = 0x100;
		in_p.video_ar.denominator = frame_ar;
	} else {
		in_p.video_ar.numerator = in_p.video_w;
		in_p.video_ar.denominator = in_p.video_h;
	}

	if (in_p.video_ar.numerator * 9 == in_p.video_ar.denominator * 16) {
		in_p.video_ar.numerator = 16;
		in_p.video_ar.denominator = 9;
	} else if (in_p.video_ar.numerator * 3 == in_p.video_ar.denominator * 4) {
		in_p.video_ar.numerator = 4;
		in_p.video_ar.denominator = 3;
	}

	if (is_ud_param_valid(vf->vf_ud_param)) {
		in_p.ud_param = (void *)&vf->vf_ud_param.ud_param;
		in_p.ud_param_size = vf->vf_ud_param.ud_param.buf_len; /* data_size? */
	}

	in_p.disp_info.x_start = layer_info->layer_left;
	in_p.disp_info.x_end =
		layer_info->layer_left +
		layer_info->layer_width - 1;
	in_p.disp_info.y_start = layer_info->layer_top;
	in_p.disp_info.y_end =
		layer_info->layer_top +
		layer_info->layer_height - 1;

	ret = afd_process(id, &in_p, &out_p);
	if (ret >= 0) {
		layer_info->afd_enable = out_p.afd_enable;
		memcpy(&layer_info->afd_pos, &out_p.dst_pos, sizeof(struct pos_rect_s));
		memcpy(&layer_info->afd_crop, &out_p.crop_info, sizeof(struct crop_rect_s));
	} else {
		layer_info->afd_enable = false;
	}
	if (layer->global_debug & DEBUG_FLAG_AFD_INFO)
		pr_info("%s: ret:%d; layer%d(%d %d %d %d) afd pos(%d %d %d %d) crop(%d %d %d %d) %s\n",
			__func__, ret, id,
			layer_info->layer_left, layer_info->layer_top,
			layer_info->layer_width, layer_info->layer_height,
			layer_info->afd_pos.x_start, layer_info->afd_pos.y_start,
			layer_info->afd_pos.x_end, layer_info->afd_pos.y_end,
			layer_info->afd_crop.top, layer_info->afd_crop.left,
			layer_info->afd_crop.bottom, layer_info->afd_crop.right,
			layer_info->afd_enable ? "enable" : "disable");
	return ret;
}

#ifdef ENABLE_PRE_LINK
/* return true to switch vf ext */
static bool update_pre_link_state(struct video_layer_s *layer,
		struct vframe_s *vf)
{
	bool ret = false;
	int iret = 0xff;
	struct pvpp_dis_para_in_s di_in_p;

	if (!layer || !vf || layer->layer_id != 0)
		return ret;

	if (!layer->need_disable_prelink &&
	    is_pre_link_on(layer)) {
		if (!layer->dispbuf ||
		    layer->dispbuf->di_instance_id !=
		    vf->di_instance_id ||
		    vf->di_instance_id !=
		    di_api_get_plink_instance_id()) {
			layer->need_disable_prelink = true;
			if (layer->global_debug & DEBUG_FLAG_PRELINK)
				pr_info("pre-link: vf instance_id changed: %d->%d, cur:%d\n",
					layer->dispbuf ? layer->dispbuf->di_instance_id : -1,
					vf->di_instance_id, di_api_get_plink_instance_id());
		}
		if (!IS_DI_POST(vf->type))
			layer->need_disable_prelink = true;
		if (layer->need_disable_prelink && (layer->global_debug & DEBUG_FLAG_PRELINK))
			pr_info("warning: need trigger need_disable_prelink!!, disp:%px id:%d, vf:%px, id:%d type:0x%x\n",
				layer->dispbuf,
				layer->dispbuf ? layer->dispbuf->di_instance_id : -1,
				vf, vf->di_instance_id, vf->type);
	}
	/* for new pipeline and front-end already unreg->reg */
	if (layer->need_disable_prelink &&
	    !is_pre_link_on(layer)) {
		bool trig_flag = false;

		if (!layer->dispbuf ||
		    layer->dispbuf->di_instance_id !=
		    vf->di_instance_id ||
		    layer->last_di_instance != vf->di_instance_id) {
			layer->need_disable_prelink = false;
			layer->last_di_instance = -1;
			trig_flag = true;
		}
		if (trig_flag && (layer->global_debug & DEBUG_FLAG_PRELINK))
			pr_info("trigger need_disable_prelink to false\n");
	}
	if (layer->need_disable_prelink) {
		/* meet non pre-link vf and set need_disable_prelink as 0 */
		if (is_local_vf(vf) || !vf->vf_ext) {
			layer->need_disable_prelink = false;
			layer->last_di_instance = -1;
		} else if (vf->vf_ext) {
			/* is_pre_link_source maybe invalid here */
			/* check if vf is remained and same instance as before */
			if (is_pre_link_on(layer) &&
			    is_pre_link_source(vf) &&
			    layer->dispbuf &&
			    vf->di_instance_id ==
			    layer->dispbuf->di_instance_id) {
				memset(&di_in_p, 0, sizeof(struct pvpp_dis_para_in_s));
				di_in_p.dmode = EPVPP_DISPLAY_MODE_BYPASS;
				di_in_p.unreg_bypass = 0; /* 1? */
				iret = pvpp_display(vf, &di_in_p, NULL);
				if (layer->global_debug & DEBUG_FLAG_PRELINK)
					pr_info("Disable/Bypass pre-link mode ret %d\n", iret);
			}

			/* just bypass prelink and not switch */
			/* TODO: if need change the condition to IS_DI_PROCESSED */
			if (!IS_DI_POST(vf->type) && !is_pre_link_source(vf))
				ret = false;
			else
				ret = true;
			if (layer->global_debug & DEBUG_FLAG_PRELINK_MORE) {
				pr_info("warning: keeping need_disable_prelink: vf %px type:%x flag:%x di_instance_id:%d last_di_instance:%d\n",
					vf, vf->type, vf->flag,
					vf->di_instance_id, layer->last_di_instance);
				if (!IS_DI_POST(vf->type))
					pr_info("is not pre_link_source\n");
				else
					pr_info("is_pre_link_source:%d, is_pre_link_available:%d\n",
						is_pre_link_source(vf) ? 1 : 0,
						is_pre_link_available(vf) ? 1 : 0);
			}
		}
		if (is_pre_link_on(layer)) {
			memset(&di_in_p, 0, sizeof(struct pvpp_dis_para_in_s));
			di_in_p.dmode = EPVPP_DISPLAY_MODE_BYPASS;
			di_in_p.unreg_bypass = 1;
			iret = pvpp_display(NULL, &di_in_p, NULL);
			if (layer->global_debug & DEBUG_FLAG_PRELINK)
				pr_info("%s: unreg_bypass pre-link mode ret %d\n",
					__func__, iret);
			iret = pvpp_sw(false);
			layer->pre_link_en = false;
			layer->prelink_bypass_check = false;
			layer->last_di_instance = vf->di_instance_id;
			layer->prelink_skip_cnt = 0;
			atomic_set(&vd_layer[0].disable_prelink_done, 1);
		}
		if (!layer->dispbuf ||
		    layer->dispbuf->di_instance_id !=
		    vf->di_instance_id ||
		    layer->last_di_instance != vf->di_instance_id) {
			layer->need_disable_prelink = false;
			layer->last_di_instance = -1;
		}
		if (layer->global_debug & DEBUG_FLAG_PRELINK)
			pr_info("Disable pre-link mode. need_disable_prelink:%d, iret:%d ret:%d\n",
				layer->need_disable_prelink ? 1 : 0,
				iret, ret ? 1 : 0);
		if (layer->global_debug & DEBUG_FLAG_PRELINK_MORE)
			pr_info("Disable pre-link mode. dispbuf->di_instance_id:%d, vf->di_instance_id:%d last_di_instance:%d\n",
				layer->dispbuf ? layer->dispbuf->di_instance_id : -2,
				vf->di_instance_id, layer->last_di_instance);
	} else {
		if (layer->next_frame_par->vscale_skip_count > 0 &&
		    is_pre_link_on(layer) &&
		    !is_local_vf(vf) && !vf->vf_ext)
			pr_info("Error, no vf_ext to switch for pre-link\n");

		/* dynamic switch when di is in active */
		if ((layer->next_frame_par->vscale_skip_count > 0 ||
		     layer->prelink_bypass_check) &&
		    is_pre_link_on(layer) && vf->vf_ext) {
			memset(&di_in_p, 0, sizeof(struct pvpp_dis_para_in_s));
			di_in_p.dmode = EPVPP_DISPLAY_MODE_BYPASS;
			di_in_p.unreg_bypass = 0;
			iret = pvpp_display(vf, &di_in_p, NULL);
			if (iret >= 0) {
				iret = pvpp_sw(false);
				if (layer->global_debug & DEBUG_FLAG_PRELINK)
					pr_info("Bypass pre-link mode ret %d\n", iret);
				ret = true;
				layer->pre_link_en = false;
				layer->prelink_bypass_check = false;
				layer->last_di_instance = vf->di_instance_id;
				layer->prelink_skip_cnt = 0;
			} else {
				pr_info("Bypass pre-link fail %d\n", iret);
			}
		} else if (!is_pre_link_on(layer) &&
				!is_local_vf(vf) &&
				is_pre_link_available(vf) &&
				!layer->next_frame_par->vscale_skip_count) {
			/* enable pre-link when it is available and di is in active */
			/* TODO: need update vf->type to check pre-link again */
			/* if only check the dec vf at first time */
			iret = pvpp_sw(true);
			if (iret >= 0) {
				memset(&di_in_p, 0, sizeof(struct pvpp_dis_para_in_s));
				di_in_p.win.x_st =
					layer->next_frame_par->VPP_hd_start_lines_;
				di_in_p.win.x_end =
					layer->next_frame_par->VPP_hd_end_lines_;
				di_in_p.win.y_st =
					layer->next_frame_par->VPP_vd_start_lines_;
				di_in_p.win.y_end =
					layer->next_frame_par->VPP_vd_end_lines_;
				di_in_p.win.x_size =
					di_in_p.win.x_end - di_in_p.win.x_st + 1;
				di_in_p.win.y_size =
					di_in_p.win.y_end - di_in_p.win.y_st + 1;
				di_in_p.dmode = EPVPP_DISPLAY_MODE_NR;
				di_in_p.unreg_bypass = 0;
				iret = pvpp_display(vf, &di_in_p, NULL);
				if (iret > 0) {
					layer->pre_link_en = true;
					layer->prelink_skip_cnt = 1;
					atomic_set(&vd_layer[0].disable_prelink_done, 0);
				} else {
					/* force config in next frame swap */
					if (ret == 0)
						layer->force_config_cnt++;
					layer->prelink_skip_cnt = 0;
					ret = true;
				}
				if (layer->global_debug & DEBUG_FLAG_PRELINK)
					pr_info("Enable pre-link ret %d\n", iret);
			} else {
				ret = true;
				pr_info("Enable pre-link but pvpp_sw fail ret %d\n", iret);
			}
		} else if (!is_pre_link_on(layer) &&
				!is_local_vf(vf) &&
				!layer->next_frame_par->vscale_skip_count &&
				is_pre_link_source(vf)) {
			ret = true;
			if (layer->global_debug & DEBUG_FLAG_PRELINK_MORE)
				pr_info("Do not enable pre-link yet\n");
		} else if (is_pre_link_on(layer) &&
				vf->vf_ext &&
				layer->need_disable_prelink) {
			/* is_pre_link_source maybe invalid here */
			/* should be not run here */
			if (layer->global_debug & DEBUG_FLAG_PRELINK)
				pr_info("Warning: need disable pre-link and switch vf %px vf_ext:%px uvm_vf:%px\n",
					vf, vf->vf_ext, vf->uvm_vf);
			ret = true;
			layer->pre_link_en = false;
			layer->prelink_bypass_check = false;
			layer->last_di_instance = vf->di_instance_id;
			layer->prelink_skip_cnt = 0;
			/* atomic_set(&vd_layer[0].disable_prelink_done, 1); */
		} else if (!is_pre_link_on(layer) &&
				vf->vf_ext &&
				!IS_DI_POSTWRTIE(vf->type)) {
			/* Just test the exception case */
			if (IS_DI_POST(vf->type) &&
			    layer->next_frame_par->vscale_skip_count > 0) {
				ret = true;
			} else if (IS_DI_PRELINK(vf->di_flag)) {
				ret = true;
				if (layer->global_debug & DEBUG_FLAG_PRELINK)
					pr_info("can't enable pre-link, force switch: vf %px vf_ext:%px uvm_vf:%px type:%x flag:%x di_flag:%x\n",
						vf, vf->vf_ext, vf->uvm_vf,
						vf->type, vf->flag, vf->di_flag);
			} else if (layer->global_debug & DEBUG_FLAG_PRELINK) {
				pr_info("pre-link warning: vf %px vf_ext:%px uvm_vf:%px type:%x flag:%x\n",
					vf, vf->vf_ext, vf->uvm_vf,
					vf->type, vf->flag);
			}
		}
	}
	return ret;
}
#endif

s32 layer_swap_frame(struct vframe_s *vf, struct video_layer_s *layer,
		     bool force_toggle,
		     const struct vinfo_s *vinfo, u32 swap_op_flag)
{
	bool first_picture = false;
	bool enable_layer = false;
	bool frame_changed;
	bool sr_phase_changed = false;
	struct disp_info_s *layer_info = NULL;
	int ret = vppfilter_success;
	bool is_mvc = false;
	u8 layer_id;
	struct vframe_s *vf_ext = NULL;

	if (!vf)
		return vppfilter_fail;

	if (IS_DI_PROCESSED(vf->type)) {
		if (vf->uvm_vf)
			vf_ext = vf->uvm_vf;
		else
			vf_ext = (struct vframe_s *)vf->vf_ext;
	}
	layer_id = layer->layer_id;
	layer_info = &glayer_info[layer_id];

	if ((vf->type & VIDTYPE_MVC) && layer_id == 0)
		is_mvc = true;

	if (layer->dispbuf != vf) {
		layer->new_vframe_count++;
		layer->new_frame = true;
		if (!layer->dispbuf ||
		    layer->new_vframe_count == 1 ||
		    (is_local_vf(layer->dispbuf)))
			first_picture = true;
	}

	if (is_afd_available(layer_id)) {
		if (update_afd_param(layer_id, vf, vinfo) > 0)
			layer->property_changed = true;
	}

	if (layer->property_changed) {
		layer->force_config_cnt = 2;
		layer->property_changed = false;
		force_toggle = true;
	}
	if (layer->force_config_cnt > 0) {
		layer->force_config_cnt--;
		force_toggle = true;
	}

	if (!vf_ext) {
		if (layer->switch_vf) {
			force_toggle = true;
			if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
				pr_info
					("layer%d: force disable switch vf: %px\n",
					layer->layer_id, vf);
		}
		layer->switch_vf = false;
	} else if (layer->switch_vf) {
		if (vf_ext)
			memcpy(&vf_ext->pic_mode, &vf->pic_mode,
				sizeof(struct vframe_pic_mode_s));
		if (!layer->vf_ext)
			force_toggle = true;
	}

	if ((layer->global_debug & DEBUG_FLAG_BASIC_INFO) && first_picture)
		pr_info("first swap picture {%d,%d} pts:%x, switch:%d\n",
			vf->width, vf->height, vf->pts,
			layer->switch_vf ? 1 : 0);

	set_layer_display_canvas
		(layer,
		(layer->switch_vf && vf_ext) ? vf_ext : vf,
		layer->cur_frame_par, layer_info, __LINE__);

	if (layer->global_debug & DEBUG_FLAG_TRACE_EVENT) {
		struct vframe_s *dispbuf = NULL;
		u32 adapted_mode, info_frame;
		bool print_info = false;

		if (layer->dispbuf && vf &&
			(layer->dispbuf->ratio_control & DISP_RATIO_ADAPTED_PICMODE) &&
			layer->dispbuf->ratio_control == vf->ratio_control &&
			memcmp(&layer->dispbuf->pic_mode, &vf->pic_mode,
			   sizeof(struct vframe_pic_mode_s)))
			print_info = true;
		else if (layer->dispbuf &&
			 memcmp(&layer->dispbuf->pic_mode, &gpic_info[layer_id],
				sizeof(struct vframe_pic_mode_s)))
			print_info = true;
		else if (!layer->dispbuf)
			print_info = true;

		if (layer->dispbuf && print_info) {
			dispbuf = layer->dispbuf;
			adapted_mode = (dispbuf->ratio_control
				& DISP_RATIO_ADAPTED_PICMODE) ? 1 : 0;
			info_frame = (dispbuf->ratio_control
				& DISP_RATIO_INFOFRAME_AVAIL) ? 1 : 0;

			pr_info("VID%d: dispbuf:%p; ratio_control=0x%x;\n",
				layer_id,
				dispbuf, dispbuf->ratio_control);
			pr_info("VID%d: adapted_mode=%d; info_frame=%d;\n",
				layer_id,
				adapted_mode, info_frame);
			pr_info("VID%d: hs=%d, he=%d, vs=%d, ve=%d;\n",
				layer_id,
				dispbuf->pic_mode.hs,
				dispbuf->pic_mode.he,
				dispbuf->pic_mode.vs,
				dispbuf->pic_mode.ve);
			pr_info("VID%d: screen_mode=%d; custom_ar=%d; AFD_enable=%d\n",
				layer_id,
				dispbuf->pic_mode.screen_mode,
				dispbuf->pic_mode.custom_ar,
				dispbuf->pic_mode.AFD_enable);
		}

		if (vf && print_info) {
			dispbuf = vf;
			adapted_mode = (dispbuf->ratio_control
				& DISP_RATIO_ADAPTED_PICMODE) ? 1 : 0;
			info_frame = (dispbuf->ratio_control
				& DISP_RATIO_INFOFRAME_AVAIL) ? 1 : 0;

			pr_info("VID%d: new vf:%p; ratio_control=0x%x;\n",
				layer_id,
				dispbuf, dispbuf->ratio_control);
			pr_info("VID%d: adapted_mode=%d; info_frame=%d;\n",
				layer_id,
				adapted_mode, info_frame);
			pr_info("VID%d: hs=%d, he=%d, vs=%d, ve=%d;\n",
				layer_id,
				dispbuf->pic_mode.hs,
				dispbuf->pic_mode.he,
				dispbuf->pic_mode.vs,
				dispbuf->pic_mode.ve);
			pr_info("VID%d: screen_mode=%d; custom_ar=%d; AFD_enable=%d\n",
				layer_id,
				dispbuf->pic_mode.screen_mode,
				dispbuf->pic_mode.custom_ar,
				dispbuf->pic_mode.AFD_enable);
		}
	}

	frame_changed = is_vframe_changed
		(layer_id,
		layer->dispbuf, vf);
	sr_phase_changed = is_sr_phase_changed();

	/* enable new config on the new frames */
	if (first_picture || force_toggle || frame_changed ||
	    sr_phase_changed) {
		u32 op_flag = OP_VPP_MORE_LOG | swap_op_flag;

		if (layer->next_frame_par == layer->cur_frame_par)
			layer->next_frame_par =
				(&layer->frame_parms[0] == layer->next_frame_par) ?
				&layer->frame_parms[1] : &layer->frame_parms[0];
		/* FIXME: remove the global variables */
		//glayer_info[layer_id].reverse = reverse;
		//glayer_info[layer_id].mirror = mirror;
		glayer_info[layer_id].proc_3d_type =
			process_3d_type;

		if (layer->force_switch_mode == 1)
			op_flag |= OP_FORCE_SWITCH_VF;
		else if (layer->force_switch_mode == 2)
			op_flag |= OP_FORCE_NOT_SWITCH_VF;

		ret = vpp_set_filters
			(&glayer_info[layer_id], vf,
			layer->next_frame_par, vinfo,
			(is_amdv_on() &&
			is_amdv_stb_mode() &&
			for_amdv_certification()),
			op_flag);
#ifdef ENABLE_PRE_LINK
		if (layer->layer_id == 0 &&
		    update_pre_link_state(layer, vf))
			ret = vppfilter_success_and_switched;
#endif
		memcpy(&gpic_info[layer_id], &vf->pic_mode,
		       sizeof(struct vframe_pic_mode_s));

		if (ret == vppfilter_success_and_changed ||
		    ret == vppfilter_changed_but_hold ||
			ret == vppfilter_changed_but_switch)
			layer->property_changed = true;

		if (ret != vppfilter_changed_but_hold &&
		    ret != vppfilter_changed_but_switch)
			layer->new_vpp_setting = true;

		if (ret == vppfilter_success_and_switched && !vf_ext &&
		    (layer->global_debug & DEBUG_FLAG_BASIC_INFO))
			pr_info
				("Warning: layer%d: pending switch the display to vf_ext %px\n",
				layer->layer_id, vf);
		if (ret == vppfilter_success_and_switched && vf_ext) {
			/* first switch, need re-config vf ext canvas */
			if (!layer->switch_vf) {
				set_layer_display_canvas
					(layer,
					vf_ext,
					layer->cur_frame_par,
					layer_info, __LINE__);
				/* if vskip > 1, need set next_frame_par again with dw buffer */
				if (layer->next_frame_par->vscale_skip_count > 1) {
					ret = vpp_set_filters
						(&glayer_info[layer_id], vf_ext,
						layer->next_frame_par, vinfo,
						(is_amdv_on() &&
						is_amdv_stb_mode() &&
						for_amdv_certification()),
						op_flag);
					if (ret == vppfilter_success_and_changed ||
						ret == vppfilter_changed_but_hold ||
						ret == vppfilter_changed_but_switch)
						layer->property_changed = true;
					if (ret != vppfilter_changed_but_hold &&
						ret != vppfilter_changed_but_switch)
						layer->new_vpp_setting = true;
					if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
						pr_info
						("layer%d: switch and vpp_set_filters ret:%d\n",
						 layer->layer_id, ret);
				}
				if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
					pr_info
						("layer%d: switch to vf_ext %px->%px(%px %px)\n",
						layer->layer_id, vf, vf_ext,
						vf->vf_ext, vf->uvm_vf);
			}
			layer->switch_vf = true;
		} else {
			/* first switch, need re-config orig vf canvas */
			if (layer->switch_vf) {
				set_layer_display_canvas
					(layer,
					vf,
					layer->cur_frame_par,
					layer_info, __LINE__);
				if (layer->global_debug & DEBUG_FLAG_BASIC_INFO)
					pr_info
						("layer%d: switch to orig vf %px(%px %px)->%px\n",
						layer->layer_id, vf_ext,
						vf->vf_ext, vf->uvm_vf, vf);
			}
			layer->switch_vf = false;
		}
#ifdef CONFIG_AMLOGIC_VPU
		if (layer_id == 0) {
			if ((vf->width > 1920 && vf->height > 1088) ||
			    ((vf->type & VIDTYPE_COMPRESS) &&
			     vf->compWidth > 1920 &&
			     vf->compHeight > 1080)) {
				VPU_VD1_CLK_SWITCH(1);
			} else {
				VPU_VD1_CLK_SWITCH(0);
			}
		} else if (layer_id == 1) {
			if ((vf->width > 1920 && vf->height > 1088) ||
			    ((vf->type & VIDTYPE_COMPRESS) &&
			     vf->compWidth > 1920 &&
			     vf->compHeight > 1080)) {
				VPU_VD2_CLK_SWITCH(1);
			} else {
				VPU_VD2_CLK_SWITCH(0);
			}
		}
#endif
	}

	if (((vf->type & VIDTYPE_NO_VIDEO_ENABLE) == 0) &&
	    (!layer->force_config_cnt || vf != layer->dispbuf)) {
		if (layer->disable_video == VIDEO_DISABLE_FORNEXT) {
			enable_layer = true;
			layer->disable_video = VIDEO_DISABLE_NONE;
		}
		if (first_picture &&
		    (layer->disable_video !=
		     VIDEO_DISABLE_NORMAL))
			enable_layer = true;
		if (enable_layer) {
			if (layer_id == 0)
				enable_video_layer();
			if (layer_id == 1 || is_mvc)
				enable_video_layer2();
		}
	}
	if (vd_layer[0].enabled && !vd_layer[1].enabled && is_mvc)
		enable_video_layer2();
	if (first_picture || sr_phase_changed)
		layer->new_vpp_setting = true;
	layer->dispbuf = vf;
	if (layer->switch_vf)
		layer->vf_ext = vf_ext;
	else
		layer->vf_ext = NULL;
	return ret;
}

/* pip apha APIs */
static void vd_set_alpha(struct video_layer_s *layer,
			     struct pip_alpha_scpxn_s *alpha_win)
{
	int i;
	u32 alph_gen_mode = 1;
	/* 0:original, 1:  0.5 alpha 2: 0.25/0.5/0.75 */
	u32 alph_gen_byps = 0;
	u8 vpp_index;
	struct hw_vpp_blend_reg_s *vpp_blend_reg =
		&layer->vpp_blend_reg;
	u32 win_en = layer->alpha_win_en;

	vpp_index = layer->vpp_index;
	if (!win_en)
		alph_gen_byps = 1;
	cur_dev->rdma_func[vpp_index].rdma_wr(vpp_blend_reg->vd_pip_alph_ctrl,
			  ((0 & 0x1) << 28) |
			  ((win_en & 0xffff) << 12) |
			  ((0 & 0x1ff) << 3) |
			  ((alph_gen_mode & 0x3) << 1) |
			  ((alph_gen_byps & 0x1) << 0));
	for (i = 0; i < MAX_PIP_WINDOW; i++) {
		if (win_en & 1) {
			cur_dev->rdma_func[vpp_index].rdma_wr(vpp_blend_reg->vd_pip_alph_scp_h + i,
				(alpha_win->scpxn_end_h[i] & 0x1fff) << 16 |
				(alpha_win->scpxn_bgn_h[i] & 0x1fff));

			cur_dev->rdma_func[vpp_index].rdma_wr(vpp_blend_reg->vd_pip_alph_scp_v + i,
				(alpha_win->scpxn_end_v[i] & 0x1fff) << 16 |
				(alpha_win->scpxn_bgn_v[i] & 0x1fff));
			win_en >>= 1;
		}
	}
}

void alpha_win_set(struct video_layer_s *layer)
{
	u8 layer_id = layer->layer_id;

	if (glayer_info[layer_id].alpha_support)
		vd_set_alpha(layer, &layer->alpha_win);
}

/*********************************************************
 * vout APIs
 *********************************************************/
int detect_vout_type(const struct vinfo_s *vinfo)
{
	int vout_type = VOUT_TYPE_PROG;
	u32 encl_info_reg, encp_info_reg;

	if (cur_dev->display_module == T7_DISPLAY_MODULE ||
		cur_dev->display_module == S5_DISPLAY_MODULE) {
		encl_info_reg = VPU_VENCI_STAT;
		encp_info_reg = VPU_VENCP_STAT;
	} else {
		encl_info_reg = ENCI_INFO_READ;
		encp_info_reg = ENCP_INFO_READ;
	}
	if (vinfo && vinfo->field_height != vinfo->height) {
		if (vinfo->height == 576 || vinfo->height == 480)
			if (cur_dev->display_module == T7_DISPLAY_MODULE ||
				cur_dev->display_module == S5_DISPLAY_MODULE)
				vout_type = (READ_VCBUS_REG(encl_info_reg) &
					(1 << 28)) ?
					VOUT_TYPE_BOT_FIELD : VOUT_TYPE_TOP_FIELD;
			else
				vout_type = (READ_VCBUS_REG(encl_info_reg) &
					(1 << 29)) ?
					VOUT_TYPE_BOT_FIELD : VOUT_TYPE_TOP_FIELD;
		else if (vinfo->height == 1080)
			vout_type = (((READ_VCBUS_REG(encp_info_reg) >> 16) &
				0x1fff) < 562) ?
				VOUT_TYPE_TOP_FIELD : VOUT_TYPE_BOT_FIELD;

#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		if (is_vsync_rdma_enable()) {
			if (vout_type == VOUT_TYPE_TOP_FIELD)
				vout_type = VOUT_TYPE_BOT_FIELD;
			else if (vout_type == VOUT_TYPE_BOT_FIELD)
				vout_type = VOUT_TYPE_TOP_FIELD;
		}
#endif
	}
	return vout_type;
}

int calc_hold_line(void)
{
	if ((READ_VCBUS_REG(ENCI_VIDEO_EN) & 1) == 0)
		return READ_VCBUS_REG(ENCP_VIDEO_VAVON_BLINE) >> 1;
	else
		return READ_VCBUS_REG(ENCP_VFIFO2VD_LINE_TOP_START) >> 1;
}

static int get_venc_type(void)
{
	u32 venc_type = 0;

	if (cur_dev->display_module == T7_DISPLAY_MODULE) {
		u32 venc_mux = 3;
		u32 venc_addr = VPU_VENC_CTRL;

		venc_mux = READ_VCBUS_REG(VPU_VIU_VENC_MUX_CTRL) & 0x3f;
		venc_mux &= 0x3;

		switch (venc_mux) {
		case 0:
			venc_addr = VPU_VENC_CTRL;
			break;
		case 1:
			venc_addr = VPU1_VENC_CTRL;
			break;
		case 2:
			venc_addr = VPU2_VENC_CTRL;
			break;
		}
		venc_type = READ_VCBUS_REG(venc_addr);
	} else {
		venc_type = READ_VCBUS_REG(VPU_VIU_VENC_MUX_CTRL);
	}
	venc_type &= 0x3;

	return venc_type;
}

u32 get_active_start_line(void)
{
	int active_line_begin = 0;
	u32 offset = 0;
	u32 reg = ENCL_VIDEO_VAVON_BLINE;

	if (cur_dev->display_module == T7_DISPLAY_MODULE ||
		cur_dev->display_module == S5_DISPLAY_MODULE) {
		u32 venc_mux = 3;

		venc_mux = READ_VCBUS_REG(VPU_VIU_VENC_MUX_CTRL) & 0x3f;
		venc_mux &= 0x3;

		switch (venc_mux) {
		case 0:
			offset = 0;
			break;
		case 1:
			offset = 0x600;
			break;
		case 2:
			offset = 0x800;
			break;
		}
		switch (get_venc_type()) {
		case 0:
			reg = ENCI_VFIFO2VD_LINE_TOP_START;
			break;
		case 1:
			reg = ENCP_VIDEO_VAVON_BLINE;
			break;
		case 2:
			reg = ENCL_VIDEO_VAVON_BLINE;
			break;
		}

	} else {
		switch (get_venc_type()) {
		case 0:
			reg = ENCL_VIDEO_VAVON_BLINE;
			break;
		case 1:
			reg = ENCI_VFIFO2VD_LINE_TOP_START;
			break;
		case 2:
			reg = ENCP_VIDEO_VAVON_BLINE;
			break;
		case 3:
			reg = ENCT_VIDEO_VAVON_BLINE;
			break;
		}
	}

	active_line_begin = READ_VCBUS_REG(reg + offset);

	return active_line_begin;
}

u32 get_cur_enc_line(void)
{
	int enc_line = 0;
	unsigned int reg = VPU_VENCI_STAT;
	unsigned int reg_val = 0;
	u32 offset = 0;
	u32 venc_type = get_venc_type();

	if (cur_dev->display_module == T7_DISPLAY_MODULE) {
		u32 venc_mux = 3;

		venc_mux = READ_VCBUS_REG(VPU_VIU_VENC_MUX_CTRL) & 0x3f;
		venc_mux &= 0x3;
		switch (venc_mux) {
		case 0:
			offset = 0;
			break;
		case 1:
			offset = 0x600;
			break;
		case 2:
			offset = 0x800;
			break;
		}
		switch (venc_type) {
		case 0:
			reg = VPU_VENCI_STAT;
			break;
		case 1:
			reg = VPU_VENCP_STAT;
			break;
		case 2:
			reg = VPU_VENCL_STAT;
			break;
		}
	} else {
		switch (venc_type) {
		case 0:
			reg = ENCL_INFO_READ;
			break;
		case 1:
			reg = ENCI_INFO_READ;
			break;
		case 2:
			reg = ENCP_INFO_READ;
			break;
		case 3:
			reg = ENCT_INFO_READ;
			break;
		}
	}

	reg_val = READ_VCBUS_REG(reg + offset);

	enc_line = (reg_val >> 16) & 0x1fff;

	return enc_line;
}

u32 get_cur_enc_num(void)
{
	u32 enc_num = 0;
	unsigned int reg = VPU_VENCI_STAT;
	unsigned int reg_val = 0;
	u32 offset = 0;
	u32 venc_type = get_venc_type();
	u32 bit_offest = 0;

	if (cur_dev->display_module == T7_DISPLAY_MODULE) {
		u32 venc_mux = 3;

		bit_offest = 13;
		venc_mux = READ_VCBUS_REG(VPU_VIU_VENC_MUX_CTRL) & 0x3f;
		venc_mux &= 0x3;
		switch (venc_mux) {
		case 0:
			offset = 0;
			break;
		case 1:
			offset = 0x600;
			break;
		case 2:
			offset = 0x800;
			break;
		}
		switch (venc_type) {
		case 0:
			reg = VPU_VENCI_STAT;
			break;
		case 1:
			reg = VPU_VENCP_STAT;
			break;
		case 2:
			reg = VPU_VENCL_STAT;
			break;
		}
	} else {
		bit_offest = 29;
		switch (venc_type) {
		case 0:
			reg = ENCL_INFO_READ;
			break;
		case 1:
			reg = ENCI_INFO_READ;
			break;
		case 2:
			reg = ENCP_INFO_READ;
			break;
		case 3:
			reg = ENCT_INFO_READ;
			break;
		}
	}

	reg_val = READ_VCBUS_REG(reg + offset);

	enc_num = (reg_val >> bit_offest) & 0x7;

	return enc_num;
}

#ifdef CONFIG_AMLOGIC_VPU
static void do_vpu_delay_work(struct work_struct *work)
{
	unsigned long flags;
	unsigned int r;

	if (vpu_delay_work_flag & VPU_VIDEO_LAYER1_CHANGED)
		vpu_delay_work_flag &= ~VPU_VIDEO_LAYER1_CHANGED;

	if (vpu_delay_work_flag & VPU_VIDEO_LAYER2_CHANGED)
		vpu_delay_work_flag &= ~VPU_VIDEO_LAYER2_CHANGED;

	spin_lock_irqsave(&delay_work_lock, flags);

	if (vpu_delay_work_flag & VPU_DELAYWORK_VPU_VD1_CLK) {
		vpu_delay_work_flag &= ~VPU_DELAYWORK_VPU_VD1_CLK;

		spin_unlock_irqrestore(&delay_work_lock, flags);
		if (vpu_vd1_clk_level > 0)
			vpu_dev_clk_request(vd1_vpu_dev, 360000000);
		else
			vpu_dev_clk_release(vd1_vpu_dev);

		spin_lock_irqsave(&delay_work_lock, flags);
	}

	if (vpu_delay_work_flag & VPU_DELAYWORK_VPU_VD2_CLK) {
		vpu_delay_work_flag &= ~VPU_DELAYWORK_VPU_VD2_CLK;

		/* FIXME: check the right vd2 clock */
#ifdef CHECK_LATER
		spin_unlock_irqrestore(&delay_work_lock, flags);

		if (vpu_vd2_clk_level > 0)
			request_vpu_clk_vmod(360000000, VPU_VIU_VD2);
		else
			release_vpu_clk_vmod(VPU_VIU_VD2);

		spin_lock_irqsave(&delay_work_lock, flags);
#endif
	}

	r = READ_VCBUS_REG(VPP_MISC + cur_dev->vpp_off);

	if (vpu_mem_power_off_count > 0) {
		vpu_mem_power_off_count--;

		if (vpu_mem_power_off_count == 0) {
			if ((vpu_delay_work_flag &
			     VPU_DELAYWORK_MEM_POWER_OFF_VD1) &&
			    ((r & VPP_VD1_PREBLEND) == 0)) {
				vpu_delay_work_flag &=
				    ~VPU_DELAYWORK_MEM_POWER_OFF_VD1;
				vpu_dev_mem_power_down(vd1_vpu_dev);
				vpu_dev_mem_power_down(afbc_vpu_dev);
#ifdef DI_POST_PWR
				vpu_dev_mem_power_down(di_post_vpu_dev);
#endif
				if (!legacy_vpp)
					vpu_dev_mem_power_down
					(vd1_scale_vpu_dev);
				if (glayer_info[0].fgrain_support)
					vpu_dev_mem_power_down
						(vpu_fgrain0);
			}

			if ((vpu_delay_work_flag &
			     VPU_DELAYWORK_MEM_POWER_OFF_VD2) &&
			    ((r & VPP_VD2_PREBLEND) == 0)) {
				vpu_delay_work_flag &=
				    ~VPU_DELAYWORK_MEM_POWER_OFF_VD2;
				vpu_dev_mem_power_down(vd2_vpu_dev);
				vpu_dev_mem_power_down(afbc2_vpu_dev);
				if (!legacy_vpp)
					vpu_dev_mem_power_down
					(vd2_scale_vpu_dev);
			}
		}
	}

	if (dv_mem_power_off_count > 0) {
		dv_mem_power_off_count--;
		if (dv_mem_power_off_count == 0) {
			if ((vpu_delay_work_flag &
			     VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0)) {
				vpu_delay_work_flag &=
				    ~VPU_DELAYWORK_MEM_POWER_OFF_DOLBY0;
				vpu_dev_mem_power_down(vpu_dolby0);
			}
			if ((vpu_delay_work_flag &
				VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A)) {
				vpu_delay_work_flag &=
					 ~VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1A;
				vpu_dev_mem_power_down(vpu_dolby1a);
			}
			if ((vpu_delay_work_flag &
				VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B)) {
				vpu_delay_work_flag &=
					~VPU_DELAYWORK_MEM_POWER_OFF_DOLBY1B;
				vpu_dev_mem_power_down(vpu_dolby1b);
			}
			if ((vpu_delay_work_flag &
				VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2)) {
				vpu_delay_work_flag &=
				~VPU_DELAYWORK_MEM_POWER_OFF_DOLBY2;
				vpu_dev_mem_power_down(vpu_dolby2);
			}
			if ((vpu_delay_work_flag &
				VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3)) {
				vpu_delay_work_flag &=
				~VPU_DELAYWORK_MEM_POWER_OFF_DOLBY_CORE3;
				vpu_dev_mem_power_down(vpu_dolby_core3);
			}
			if ((vpu_delay_work_flag &
				VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY)) {
				vpu_delay_work_flag &=
				~VPU_DELAYWORK_MEM_POWER_OFF_PRIME_DOLBY;
				vpu_dev_mem_power_down(vpu_prime_dolby_ram);
			}
		}
	}
	spin_unlock_irqrestore(&delay_work_lock, flags);
}

void vpu_work_process(void)
{
	if (vpu_delay_work_flag)
		schedule_work(&vpu_delay_work);
}
#endif

int vpp_crc_check(u32 vpp_crc_en, u8 vpp_index)
{
	u32 val = 0;
	int vpp_crc_result = 0;
	static u32 val_pre, crc_cnt;

	if (vpp_crc_en && cpu_after_eq(MESON_CPU_MAJOR_ID_SM1)) {
		cur_dev->rdma_func[vpp_index].rdma_wr(VPP_CRC_CHK, 1);
		if (crc_cnt >= 1) {
			val = cur_dev->rdma_func[vpp_index].rdma_rd(VPP_RO_CRCSUM);
			if (val_pre && val != val_pre)
				vpp_crc_result = -1;
			else
				vpp_crc_result = val;
		}
		val_pre = val;
		crc_cnt++;
	} else {
		crc_cnt  = 0;
	}
	return vpp_crc_result;
}

int vpp_crc_viu2_check(u32 vpp_crc_en)
{
	int vpp_crc_result = 0;

	if (vpp_crc_en)
		vpp_crc_result = READ_VCBUS_REG(VPP2_RO_CRCSUM);

	return vpp_crc_result;
}

void enable_vpp_crc_viu2(u32 vpp_crc_en)
{
	if (vpp_crc_en)
		WRITE_VCBUS_REG_BITS(VPP2_CRC_CHK, 1, 0, 1);
}

#ifdef TMP_DISABLE
int get_osd_reverse(void)
{
	u8 vpp_index = VPP0;

	return (cur_dev->rdma_func[vpp_index].rdma_rd
		(VIU_OSD1_BLK0_CFG_W0) >> 28) & 3;
}
EXPORT_SYMBOL(get_osd_reverse);
#endif

void dump_pps_coefs_info(u8 layer_id, u8 bit9_mode, u8 coef_type)
{
	u32 pps_coef_idx_save;
	int i;
	struct hw_pps_reg_s *vd_pps_reg;
	int scaler_sep_coef_en;
	/* bit9_mode : 0 8bit, 1: 9bit*/
	/* coef_type : 0 horz, 1: vert*/
	vd_pps_reg = &vd_layer[layer_id].pps_reg;
	scaler_sep_coef_en = cur_dev->scaler_sep_coef_en;
	if (layer_id == 0xff) {
		vd_pps_reg = &cur_dev->aisr_pps_reg;
		scaler_sep_coef_en = 0;
		layer_id = 0;
	}
	pps_coef_idx_save = READ_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx);
	if (hscaler_8tap_enable[layer_id]) {
		if (bit9_mode) {
			if (coef_type == 0) {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ |
					VPP_COEF_9BIT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8TAP 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8TAP 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));

				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ |
					VPP_COEF_VERT_CHROMA |
					VPP_COEF_9BIT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8TAP 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8TAP 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8TAP 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8TAP 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA_PARTB |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8TAP 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8TAP 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			} else {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_VERT |
					VPP_COEF_9BIT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_VERT_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			}
		} else {
			if (coef_type == 0) {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ);
				pr_info("[0x%x]=0x%lx",
					vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8TAP HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ |
					VPP_COEF_VERT_CHROMA);
				pr_info("[0x%x]=0x%lx",
					vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ |
					VPP_COEF_VERT_CHROMA);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 8 TAP HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF);
					pr_info("[0x%x]=0x%lx",
					vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_SEP_COEF_HORZ_CHROMA |
					VPP_SEP_COEF);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8TAP HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA_PARTB |
						VPP_SEP_COEF);
					pr_info("[0x%x]=0x%lx",
					vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_SEP_COEF_HORZ_CHROMA_PARTB |
					VPP_SEP_COEF);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 8 TAP HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			} else {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_VERT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (cur_dev->scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_VERT_CHROMA |
						VPP_SEP_COEF);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			}
		}
	} else {
		if (bit9_mode) {
			if (coef_type == 0) {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ |
					VPP_COEF_9BIT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			} else {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_VERT |
					VPP_COEF_9BIT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 9bit VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_VERT_CHROMA |
						VPP_SEP_COEF |
						VPP_COEF_9BIT);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 9bit VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			}
		} else {
			if (coef_type == 0) {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_HORZ);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 HORZ coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_HORZ_CHROMA |
						VPP_SEP_COEF);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 HORZ chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			} else {
				WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
					VPP_COEF_RD_CBUS |
					VPP_COEF_VERT);
				READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
				for (i = 0;
					i < VPP_FILER_COEFS_NUM; i++)
					pr_info("VD1 VERT coef[%d]=%x\n",
					i,
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				if (scaler_sep_coef_en) {
					WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
						VPP_COEF_RD_CBUS |
						VPP_SEP_COEF_VERT_CHROMA |
						VPP_SEP_COEF);
					READ_VCBUS_REG(vd_pps_reg->vd_scale_coef);
					for (i = 0;
						i < VPP_FILER_COEFS_NUM; i++)
						pr_info("VD1 VERT chroma coef[%d]=%x\n",
						i,
						READ_VCBUS_REG(vd_pps_reg->vd_scale_coef));
				}
			}
		}
	}
	WRITE_VCBUS_REG(vd_pps_reg->vd_scale_coef_idx,
			pps_coef_idx_save);
}

void vppx_vd_blend_setting(struct video_layer_s *layer, struct blend_setting_s *setting)
{
}

s32 config_aisr_pps(struct video_layer_s *layer,
			 struct scaler_setting_s *aisr_setting)
{
	return 0;
}

s32 config_aisr_position(struct video_layer_s *layer,
			     struct aisr_setting_s *aisr_mif_setting)
{
	return 0;
}

void aisr_reshape_cfg(struct video_layer_s *layer,
		     struct aisr_setting_s *aisr_mif_setting)
{
}

void aisr_scaler_setting(struct video_layer_s *layer,
		struct scaler_setting_s *setting)
{
}

void aisr_demo_axis_set(struct video_layer_s *layer)
{
}

u32 get_slice_num(u32 layer_id)
{
	return 1;
}

u32 get_pi_enabled(u32 layer_id)
{
	return 0;
}

u32 get_vd1s1_vd2_prebld_en(u32 layer_id)
{
	return 0;
}

void vd_s5_hw_set(struct video_layer_s *layer,
	struct vframe_s *dispbuf, struct vpp_frame_par_s *frame_par)
{
}

void set_vd_pi_input_size(void)
{
}

void fgrain_config(struct video_layer_s *layer,
		   struct vpp_frame_par_s *frame_par,
		   struct mif_pos_s *mif_setting,
		   struct fgrain_setting_s *setting,
		   struct vframe_s *vf)
{
}

void fgrain_setting(struct video_layer_s *layer,
		    struct fgrain_setting_s *setting,
		    struct vframe_s *vf)
{
}

void fgrain_update_table(struct video_layer_s *layer,
			 struct vframe_s *vf)

{
}

/*********************************************************
 * Init APIs
 *********************************************************/
static noinline int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc((unsigned long)function_id,
		      (unsigned long)arg0,
		      (unsigned long)arg1,
		      (unsigned long)arg2,
		      0, 0, 0, 0, &res);
	return res.a0;
}

void vpp_probe_en_set(u32 enable)
{
	if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12A)) {
		if (enable)
			__invoke_psci_fn_smc(0x82000080, 1, 0, 0);
		else
			__invoke_psci_fn_smc(0x82000080, 0, 0, 0);
	}
}

#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
void vpp_secure_cb(u32 arg)
{
	pr_debug("%s: arg=%x, reg:%x\n",
		 __func__,
		 arg,
		 READ_VCBUS_REG(VIU_DATA_SEC));
}
#endif

void video_secure_set(u8 vpp_index)
{
#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
	int i;
	u32 secure_src = 0;
	u32 secure_enable = 0;
	struct video_layer_s *layer = NULL;

	for (i = 0; i < MAX_VD_LAYERS; i++) {
		layer = get_layer_by_layer_id(i);

		/* layer is NULL or vpp_index does not match, skip */
		if (!layer || layer->vpp_index != vpp_index)
			continue;
		if (layer->dispbuf &&
		    (layer->dispbuf->flag & VFRAME_FLAG_VIDEO_SECURE))
			secure_enable = 1;
		else
			secure_enable = 0;
		if (layer->dispbuf)
			cur_vf_flag[layer->layer_id] = layer->dispbuf->flag;
		if (layer->dispbuf &&
		    secure_enable) {
			if (layer->layer_id == 0)
				secure_src |= VD1_INPUT_SECURE;
			else if (layer->layer_id == 1)
				secure_src |= VD2_INPUT_SECURE;
			else if (layer->layer_id == 2)
				secure_src |= VD3_INPUT_SECURE;
		}
	}
	secure_config(VIDEO_MODULE, secure_src, vpp_index);
#endif
}

void di_used_vd1_afbc(bool di_used)
{
	if (di_used)
		WRITE_VCBUS_REG_BITS(VD1_AFBCD0_MISC_CTRL, 1, 1, 1);
	else
		WRITE_VCBUS_REG_BITS(VD1_AFBCD0_MISC_CTRL, 0, 1, 1);
}

static void vd_set_go_field_default(void)
{
	if (cur_dev->prevsync_support) {
		/* vd2, vd3 always used post go field if connect to vpp0*/
		if (vd_layer[1].vpp_index == VPP0)
			WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 4, 2);
		if (cur_dev->max_vd_layers == 3)
			if (vd_layer[2].vpp_index == VPP0)
				WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 8, 2);
		if (cur_dev->pre_vsync_enable)
			/* set vd1 vpp0_pre_go_field if it is vpp0 */
			WRITE_VCBUS_REG_BITS(PATH_START_SEL, 0, 0, 2);
		else
			/* set vd1 vpp0_post_go_field if it is vpp0 */
			WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 0, 2);
		/* set osd default post go_field */
		/* set osd1 vpp0_post_go_field */
		WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 16, 2);
		/* set osd2 vpp0_post_go_field */
		WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 20, 2);
		/* set osd3 vpp0_post_go_field */
		WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 24, 2);
	}
}

void vd1_set_go_field(void)
{
	if (cur_dev->prevsync_support) {
		if (cur_dev->pre_vsync_enable)
			/* set vd1 vpp0_pre_go_field if it is vpp0 */
			WRITE_VCBUS_REG_BITS(PATH_START_SEL, 0, 0, 2);
		else
			/* set vd1 vpp0_post_go_field if it is vpp0 */
			WRITE_VCBUS_REG_BITS(PATH_START_SEL, 3, 0, 2);
	}
}

int video_hw_init(void)
{
	u32 cur_hold_line, ofifo_size;
#ifdef CONFIG_AMLOGIC_VPU
	struct vpu_dev_s *arb_vpu_dev;
#endif
#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
	void *video_secure_op[VPP_TOP_MAX] = {VSYNC_WR_MPEG_REG_BITS,
					       VSYNC_WR_MPEG_REG_BITS_VPP1,
					       VSYNC_WR_MPEG_REG_BITS_VPP2,
					       PRE_VSYNC_WR_MPEG_REG_BITS};
#endif

	if (!legacy_vpp) {
		if (vpp_ofifo_size == 0xff)
			ofifo_size = 0x1000;
		else
			ofifo_size = vpp_ofifo_size;
		WRITE_VCBUS_REG_BITS
			(VPP_OFIFO_SIZE, ofifo_size,
			VPP_OFIFO_SIZE_BIT, VPP_OFIFO_SIZE_WID);
		WRITE_VCBUS_REG_BITS
			(VPP_MATRIX_CTRL, 0, 10, 5);
	} else if (cpu_after_eq(MESON_CPU_MAJOR_ID_GXTVBB))
		WRITE_VCBUS_REG_BITS
			(VPP_OFIFO_SIZE, 0xfff,
			VPP_OFIFO_SIZE_BIT, VPP_OFIFO_SIZE_WID);

	WRITE_VCBUS_REG(VPP_PREBLEND_VD1_H_START_END, 4096);
	WRITE_VCBUS_REG(VPP_BLEND_VD2_H_START_END, 4096);

#ifndef CONFIG_AMLOGIC_REMOVE_OLD
	if (is_meson_txl_cpu() || is_meson_txlx_cpu()) {
		/* fifo max size on txl :128*3=384[0x180]  */
		WRITE_VCBUS_REG
			(vd_layer[0].vd_mif_reg.vd_if0_luma_fifo_size,
			0x180);
		WRITE_VCBUS_REG
			(vd_layer[1].vd_mif_reg.vd_if0_luma_fifo_size,
			0x180);
	}
#endif

	/* default 10bit setting for gxm */
	if (is_meson_gxm_cpu()) {
#ifndef CONFIG_AMLOGIC_REMOVE_OLD
		WRITE_VCBUS_REG_BITS(VIU_MISC_CTRL1, 0xff, 16, 8);
		WRITE_VCBUS_REG(VPP_AMDV_CTRL, 0x22000);
		/*
		 *default setting is black for dummy data1& dump data0,
		 *for dummy data1 the y/cb/cr data width is 10bit on gxm,
		 *for dummy data the y/cb/cr data width is 8bit but
		 *vpp_dummy_data will be left shift 2bit auto on gxm!!!
		 */
		WRITE_VCBUS_REG(VPP_DUMMY_DATA1, 0x1020080);
		WRITE_VCBUS_REG(VPP_DUMMY_DATA, 0x42020);
#endif
	} else if (is_meson_txlx_cpu() ||
		cpu_after_eq(MESON_CPU_MAJOR_ID_G12A)) {
		/*black 10bit*/
		WRITE_VCBUS_REG(VPP_DUMMY_DATA, 0x4080200);
		if (cur_dev->display_module == T7_DISPLAY_MODULE) {
			WRITE_VCBUS_REG(T7_VD2_PPS_DUMMY_DATA, 0x4080200);
			WRITE_VCBUS_REG(VD3_PPS_DUMMY_DATA, 0x4080200);
		}
	}
	/* select afbcd output to di pre */
	if (video_is_meson_t5d_revb_cpu() && vd1_vd2_mux_dts) {
		/* default false */
		vd1_vd2_mux = true;
		vd_layer[0].vd1_vd2_mux = true;
		di_used_vd1_afbc(true);
	} else {
		vd1_vd2_mux = false;
		vd_layer[0].vd1_vd2_mux = false;
		di_used_vd1_afbc(false);
	}

	/* temp: enable VPU arb mem */
#ifdef CONFIG_AMLOGIC_VPU
	vd1_vpu_dev = vpu_dev_register(VPU_VIU_VD1, "VD1");
	afbc_vpu_dev = vpu_dev_register(VPU_AFBC_DEC, "VD1_AFBC");
#ifdef DI_POST_PWR
	di_post_vpu_dev = vpu_dev_register(VPU_DI_POST, "DI_POST");
#endif
	vd1_scale_vpu_dev = vpu_dev_register(VPU_VD1_SCALE, "VD1_SCALE");
	if (glayer_info[0].fgrain_support)
		vpu_fgrain0 = vpu_dev_register(VPU_FGRAIN0, "VPU_FGRAIN0");
	vd2_vpu_dev = vpu_dev_register(VPU_VIU_VD2, "VD2");
	afbc2_vpu_dev = vpu_dev_register(VPU_AFBC_DEC1, "VD2_AFBC");
	vd2_scale_vpu_dev = vpu_dev_register(VPU_VD2_SCALE, "VD2_SCALE");
	vpu_dolby0 = vpu_dev_register(VPU_DOLBY0, "DOLBY0");
	vpu_dolby1a = vpu_dev_register(VPU_DOLBY1A, "DOLBY1A");
	vpu_dolby1b = vpu_dev_register(VPU_DOLBY1B, "DOLBY1B");
	vpu_dolby2 = vpu_dev_register(VPU_DOLBY2, "DOLBY2");
	vpu_dolby_core3 = vpu_dev_register(VPU_DOLBY_CORE3, "DOLBY_CORE3");
	vpu_prime_dolby_ram = vpu_dev_register(VPU_PRIME_DOLBY_RAM, "PRIME_DOLBY_RAM");

	arb_vpu_dev = vpu_dev_register(VPU_VPU_ARB, "ARB");
	vpu_dev_mem_power_on(arb_vpu_dev);
#endif
	/*disable sr default when power up*/
	WRITE_VCBUS_REG(VPP_SRSHARP0_CTRL, 0);
	WRITE_VCBUS_REG(VPP_SRSHARP1_CTRL, 0);
	if (cur_dev->display_module != C3_DISPLAY_MODULE) {
		cur_hold_line = READ_VCBUS_REG(VPP_HOLD_LINES + cur_dev->vpp_off);
		cur_hold_line = cur_hold_line & 0xff;
	} else {
		cur_hold_line = 4;
	}
	if (cur_hold_line > 0x1f)
		vpp_hold_line[0] = 0x1f;
	else
		vpp_hold_line[0] = cur_hold_line;

	/* Temp force set dmc */
	if (!legacy_vpp) {
		if (cur_dev->display_module == OLD_DISPLAY_MODULE ||
			video_is_meson_t5w_cpu())
			WRITE_DMCREG
				(DMC_AM0_CHAN_CTRL,
				0x8ff403cf);
		if (video_is_meson_t5m_cpu())
			WRITE_VCBUS_REG(VPU_RDARB_UGT_L2C1, 0xffff);
		/* for vd1 & vd2 dummy alpha*/
		WRITE_VCBUS_REG
			(VPP_POST_BLEND_DUMMY_ALPHA,
			0x7fffffff);
		WRITE_VCBUS_REG_BITS
			(VPP_MISC1, 0x100, 0, 9);
	}
	if (cpu_after_eq(MESON_CPU_MAJOR_ID_TL1)) {
		/* disable latch for sr core0/1 scaler */
		WRITE_VCBUS_REG_BITS
			(sr_info.sr0_sharp_sync_ctrl,
			1, 0, 1);
		WRITE_VCBUS_REG_BITS
			(sr_info.sr0_sharp_sync_ctrl,
			1, 8, 1);
		WRITE_VCBUS_REG_BITS
			(sr_info.sr1_sharp_sync_ctrl,
			1, 8, 1);
		if (cur_dev->aisr_support)
			WRITE_VCBUS_REG_BITS
			(sr_info.sr1_sharp_sync_ctrl,
			1, 17, 1);
	} else if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12B)) {
		WRITE_VCBUS_REG_BITS
			(sr_info.sr0_sharp_sync_ctrl,
			1, 0, 1);
		WRITE_VCBUS_REG_BITS
			(sr_info.sr0_sharp_sync_ctrl,
			1, 8, 1);
	}
	/* force bypass dolby for TL1/T5, no dolby function */
	if (!glayer_info[0].dv_support && !is_meson_s4d_cpu())
		WRITE_VCBUS_REG_BITS(AMDV_PATH_CTRL, 0xf, 0, 6);

#ifdef CONFIG_AMLOGIC_MEDIA_SECURITY
	secure_register(VIDEO_MODULE, 0, video_secure_op, vpp_secure_cb);
#endif
	vd_set_go_field_default();
	return 0;
}

static int get_sr_core_support(struct amvideo_device_data_s *p_amvideo)
{
	u32 core_support = 0;

	if (p_amvideo->sr0_support == 0xff &&
	    p_amvideo->sr1_support == 0xff) {
		/* chip before tm2 revb, used old logic */
		if (is_meson_tl1_cpu() ||
		    is_meson_tm2_cpu())
			core_support = NEW_CORE0_CORE1;
		else if (is_meson_txhd_cpu() ||
			 is_meson_g12a_cpu() ||
			 is_meson_g12b_cpu() ||
			 is_meson_sm1_cpu())
			core_support = ONLY_CORE0;
		else if (is_meson_gxlx_cpu())
			core_support = ONLY_CORE1;
		else if (is_meson_txlx_cpu() ||
			 is_meson_txl_cpu() ||
			 is_meson_gxtvbb_cpu())
			core_support = OLD_CORE0_CORE1;
		return core_support;
	} else if ((p_amvideo->sr0_support == 1) &&
		(p_amvideo->sr1_support == 1)) {
		core_support = NEW_CORE0_CORE1;
	} else if (p_amvideo->sr0_support == 1) {
		core_support = ONLY_CORE0;
	} else if (p_amvideo->sr1_support == 1) {
		core_support = ONLY_CORE1;
	}
	return core_support;
}

static void vpp_sr_init(struct amvideo_device_data_s *p_amvideo)
{
	struct sr_info_s *sr;

	sr = &sr_info;
	/* sr_info */
	if (is_meson_g12a_cpu() ||
	    is_meson_g12b_cpu() ||
	    is_meson_sm1_cpu()) {
		sr->sr_reg_offt = 0xc00;
		sr->sr_reg_offt2 = 0x00;
	} else if (is_meson_tl1_cpu() ||
		   is_meson_tm2_cpu()) {
		sr->sr_reg_offt = 0xc00;
		sr->sr_reg_offt2 = 0xc80;
	} else {
		sr->sr_reg_offt = 0;
		sr->sr_reg_offt2 = 0x00;
	}

	if (p_amvideo->sr_reg_offt != 0xff) {
		sr->sr_reg_offt = p_amvideo->sr_reg_offt;
		sr->sr0_sharp_sync_ctrl =
			TM2REVB_SRSHARP0_SHARP_SYNC_CTRL;
	} else {
		sr->sr0_sharp_sync_ctrl =
			SRSHARP0_SHARP_SYNC_CTRL;
	}
	if (p_amvideo->sr_reg_offt2 != 0xff) {
		sr->sr_reg_offt2 = p_amvideo->sr_reg_offt2;
		sr->sr1_sharp_sync_ctrl =
			TM2REVB_SRSHARP1_SHARP_SYNC_CTRL;
	} else {
		sr->sr1_sharp_sync_ctrl =
			SRSHARP1_SHARP_SYNC_CTRL;
	}

	if (p_amvideo->sr0_support == 0xff &&
	    p_amvideo->sr1_support == 0xff) {
		if (is_meson_gxlx_cpu()) {
			sr->sr_support &= ~SUPER_CORE0_SUPPORT;
			sr->sr_support |= SUPER_CORE1_SUPPORT;
			sr->core1_v_disable_width_max = 4096;
			sr->core1_v_enable_width_max = 2048;
		} else if (is_meson_txhd_cpu()) {
			/* 2k panel */
			sr->sr_support |= SUPER_CORE0_SUPPORT;
			sr->sr_support &= ~SUPER_CORE1_SUPPORT;
			sr->core0_v_disable_width_max = 2048;
			sr->core0_v_enable_width_max = 1024;
		} else if (is_meson_g12a_cpu() ||
			is_meson_g12b_cpu() ||
			is_meson_sm1_cpu()) {
			sr->sr_support |= SUPER_CORE0_SUPPORT;
			sr->sr_support &= ~SUPER_CORE1_SUPPORT;
			sr->core0_v_disable_width_max = 4096;
			sr->core0_v_enable_width_max = 2048;
		} else if (is_meson_gxtvbb_cpu() ||
			   is_meson_txl_cpu() ||
			   is_meson_txlx_cpu() ||
			   is_meson_tl1_cpu() ||
			   is_meson_tm2_cpu()) {
			sr->sr_support |= SUPER_CORE0_SUPPORT;
			sr->sr_support |= SUPER_CORE1_SUPPORT;
			sr->core0_v_disable_width_max = 2048;
			sr->core0_v_enable_width_max = 1024;
			sr->core1_v_disable_width_max = 4096;
			sr->core1_v_enable_width_max = 2048;
		} else {
			sr->sr_support &= ~SUPER_CORE0_SUPPORT;
			sr->sr_support &= ~SUPER_CORE1_SUPPORT;
		}
	} else {
		if (p_amvideo->sr0_support == 1) {
			sr->sr_support |= SUPER_CORE0_SUPPORT;
			sr->core0_v_disable_width_max =
				p_amvideo->core_v_disable_width_max[0];
			sr->core0_v_enable_width_max =
				p_amvideo->core_v_enable_width_max[0];
		}
		if (p_amvideo->sr1_support == 1) {
			sr->sr_support |= SUPER_CORE1_SUPPORT;
			sr->core1_v_disable_width_max =
				p_amvideo->core_v_disable_width_max[1];
			sr->core1_v_enable_width_max =
				p_amvideo->core_v_enable_width_max[1];
		}
	}
	sr->supscl_path = p_amvideo->supscl_path;
	sr->core_support = get_sr_core_support(p_amvideo);
}

void int_vpu_delay_work(void)
{
	INIT_WORK(&vpu_delay_work, do_vpu_delay_work);
}

int video_early_init(struct amvideo_device_data_s *p_amvideo)
{
	int r = 0, i;

	if (cpu_after_eq(MESON_CPU_MAJOR_ID_G12A))
		legacy_vpp = false;

	/* check super scaler support status */
	vpp_sr_init(p_amvideo);
	/* adaptive config bypass ratio */
	vpp_bypass_ratio_config();

	memset(vd_layer, 0, sizeof(vd_layer));
	for (i = 0; i < MAX_VD_LAYER; i++) {
		vd_layer[i].layer_id = i;
		vd_layer[i].cur_canvas_id = 0;
#ifdef CONFIG_AMLOGIC_MEDIA_VSYNC_RDMA
		vd_layer[i].next_canvas_id = 1;
#endif
		/* vd_layer[i].global_output = 1; */
		vd_layer[i].keep_frame_id = 0xff;
		vd_layer[i].disable_video = VIDEO_DISABLE_FORNEXT;
		vd_layer[i].vpp_index = VPP0;
		vd_layer[i].last_di_instance = -1;

		/* clip config */
		vd_layer[i].clip_setting.id = i;
		vd_layer[i].clip_setting.misc_reg_offt = cur_dev->vpp_off;
		vd_layer[i].clip_setting.clip_max = 0x3fffffff;
		vd_layer[i].clip_setting.clip_min = 0;
		vd_layer[i].clip_setting.clip_done = true;

		vpp_disp_info_init(&glayer_info[i], i);
		memset(&gpic_info[i], 0, sizeof(struct vframe_pic_mode_s));
		glayer_info[i].wide_mode = 1;
		glayer_info[i].zorder = reference_zorder - MAX_VD_LAYER + i;
		glayer_info[i].cur_sel_port = i;
		glayer_info[i].last_sel_port = i;
		glayer_info[i].display_path_id = VFM_PATH_DEF;
		glayer_info[i].need_no_compress = false;
		glayer_info[i].afbc_support = 0;
		if (p_amvideo->layer_support[i] == 0) {
			glayer_info[i].afbc_support = false;
			glayer_info[i].pps_support = false;
			glayer_info[i].dv_support = false;
			glayer_info[i].fgrain_support = false;
			glayer_info[i].fgrain_enable = false;
			glayer_info[i].alpha_support = false;
			hscaler_8tap_enable[i] = false;
			pre_scaler[i].pre_hscaler_ntap_enable = false;
			pre_scaler[i].pre_vscaler_ntap_enable = false;
			pre_scaler[i].pre_hscaler_ntap_set = 0xff;
			pre_scaler[i].pre_vscaler_ntap_set = 0xff;
			continue;
		}
		if (legacy_vpp) {
			glayer_info[i].afbc_support = true;
			glayer_info[i].pps_support =
				(i == 0) ? true : false;
		} else if (is_meson_tl1_cpu()) {
			glayer_info[i].afbc_support =
				(i == 0) ? true : false;
			glayer_info[i].pps_support =
				(i == 0) ? true : false;
			/* force bypass dolby for TL1/T5, no dolby function */
			p_amvideo->dv_support = 0;
		} else if (is_meson_tm2_cpu()) {
			glayer_info[i].afbc_support =
				(i == 0) ? true : false;
			glayer_info[i].pps_support = true;
		} else if (is_meson_g12a_cpu() ||
			is_meson_g12b_cpu() ||
			is_meson_sm1_cpu()) {
			glayer_info[i].afbc_support = true;
			glayer_info[i].pps_support = true;
		} else {
			/* after tm2 revb */
			glayer_info[i].afbc_support =
				p_amvideo->afbc_support[i];
			glayer_info[i].pps_support =
				p_amvideo->pps_support[i];
		}
		if (p_amvideo->dv_support)
			glayer_info[i].dv_support = true;
		else
			glayer_info[i].dv_support = false;
		if (p_amvideo->fgrain_support[i]) {
			glayer_info[i].fgrain_support = true;
			glayer_info[i].fgrain_enable = true;
		} else {
			glayer_info[i].fgrain_support = false;
			glayer_info[i].fgrain_enable = false;
		}
		vd_layer[i].layer_support = p_amvideo->layer_support[i];
		glayer_info[i].layer_support = p_amvideo->layer_support[i];
		glayer_info[i].alpha_support = p_amvideo->alpha_support[i];
		hscaler_8tap_enable[i] = has_hscaler_8tap(i);
		pre_scaler[i].force_pre_scaler = 0;
		pre_scaler[i].pre_hscaler_ntap_enable =
			has_pre_hscaler_ntap(i);
		pre_scaler[i].pre_vscaler_ntap_enable =
			has_pre_vscaler_ntap(i);
		pre_scaler[i].pre_hscaler_ntap_set = 0xff;
		pre_scaler[i].pre_vscaler_ntap_set = 0xff;
		pre_scaler[i].pre_hscaler_ntap = PRE_HSCALER_4TAP;
		if (has_pre_vscaler_ntap(i))
			pre_scaler[i].pre_vscaler_ntap = PRE_VSCALER_4TAP;
		else
			pre_scaler[i].pre_vscaler_ntap = PRE_VSCALER_2TAP;
		pre_scaler[i].pre_hscaler_rate = 1;
		pre_scaler[i].pre_vscaler_rate = 1;
		pre_scaler[i].pre_hscaler_coef_set = 0;
		pre_scaler[i].pre_vscaler_coef_set = 0;
		if (p_amvideo->src_width_max[i] != 0xff)
			glayer_info[i].src_width_max =
				p_amvideo->src_width_max[i];
		else
			glayer_info[i].src_width_max = 4096;
		if (p_amvideo->src_height_max[i] != 0xff)
			glayer_info[i].src_height_max =
				p_amvideo->src_height_max[i];
		else
			glayer_info[i].src_height_max = 2160;
		glayer_info[i].afd_enable = false;
	}

	/* only enable vd1 as default */
	vd_layer[0].global_output = 1;
	vd_layer[0].misc_reg_offt = 0 + cur_dev->vpp_off;
	vd_layer[1].misc_reg_offt = 0 + cur_dev->vpp_off;
	vd_layer[2].misc_reg_offt = 0 + cur_dev->vpp_off;
	vd_layer[0].dummy_alpha = 0x7fffffff;
	cur_dev->is_tv_panel = p_amvideo->is_tv_panel;
	cur_dev->mif_linear = p_amvideo->mif_linear;
	cur_dev->display_module = p_amvideo->display_module;
	cur_dev->max_vd_layers = p_amvideo->max_vd_layers;
	cur_dev->vd2_independ_blend_ctrl =
		p_amvideo->dev_property.vd2_independ_blend_ctrl;
	cur_dev->aisr_support = p_amvideo->dev_property.aisr_support;
	cur_dev->di_hf_y_reverse = p_amvideo->dev_property.di_hf_y_reverse;
	cur_dev->sr_in_size = p_amvideo->dev_property.sr_in_size;
	cur_dev->prevsync_support = p_amvideo->dev_property.prevsync_support;
	pr_err("cur_dev->prevsync_support=%d\n",
		cur_dev->prevsync_support);
	if (cur_dev->aisr_support)
		cur_dev->pps_auto_calc = 1;
	if (video_is_meson_s4_cpu() ||
		video_is_meson_s1a_cpu()) {
		for (i = 0; i < cur_dev->max_vd_layers; i++) {
			memcpy(&vd_layer[i].vd_afbc_reg,
			       &vd_afbc_reg_sc2_array[i],
			       sizeof(struct hw_afbc_reg_s));
			memcpy(&vd_layer[i].vd_mif_reg,
			       &vd_mif_reg_sc2_array[i],
			       sizeof(struct hw_vd_reg_s));
			memcpy(&vd_layer[i].fg_reg,
			       &fg_reg_sc2_array[i],
			       sizeof(struct hw_fg_reg_s));
			if (video_is_meson_s4_cpu() ||
				video_is_meson_s1a_cpu())
				memcpy(&vd_layer[i].pps_reg,
				       &pps_reg_array_t5d[i],
				       sizeof(struct hw_pps_reg_s));
			memcpy(&vd_layer[i].vpp_blend_reg,
			       &vpp_blend_reg_array[i],
			       sizeof(struct hw_vpp_blend_reg_s));
		}
	}
	vd_layer[0].layer_alpha = 0x100;

	/* g12a has no alpha overflow check in hardware */
	vd_layer[1].layer_alpha = legacy_vpp ? 0x1ff : 0x100;
	vd_layer[2].layer_alpha = legacy_vpp ? 0x1ff : 0x100;
	vpp_ofifo_size = p_amvideo->ofifo_size;
	memcpy(conv_lbuf_len, p_amvideo->afbc_conv_lbuf_len,
	       sizeof(u32) * MAX_VD_LAYER);

#ifdef CONFIG_AMLOGIC_VPU
	if (cur_dev->display_module != C3_DISPLAY_MODULE)
		int_vpu_delay_work();
#endif

	init_layer_canvas(&vd_layer[0], LAYER1_CANVAS_BASE_INDEX);
	init_layer_canvas(&vd_layer[1], LAYER2_CANVAS_BASE_INDEX);
	init_layer_canvas(&vd_layer[2], LAYER3_CANVAS_BASE_INDEX);

	return r;
}

int video_late_uninit(void)
{
	return 0;
}

MODULE_PARM_DESC(vpp_hold_line, "\n vpp_hold_line\n");
module_param_array(vpp_hold_line, uint, &param_vpp_num, 0664);

MODULE_PARM_DESC(bypass_cm, "\n bypass_cm\n");
module_param(bypass_cm, bool, 0664);

MODULE_PARM_DESC(reference_zorder, "\n reference_zorder\n");
module_param(reference_zorder, uint, 0664);

MODULE_PARM_DESC(cur_vf_flag, "cur_vf_flag");
module_param_array(cur_vf_flag, uint, &cur_vpp_num, 0444);

MODULE_PARM_DESC(debug_flag_3d, "\n debug_flag_3d\n");
module_param(debug_flag_3d, uint, 0664);

MODULE_PARM_DESC(vd1_matrix, "\n vd1_matrix\n");
module_param(vd1_matrix, uint, 0664);

