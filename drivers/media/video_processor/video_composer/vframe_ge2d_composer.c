// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * drivers/amlogic/media/video_processor/video_composer/vframe_ge2d_composer.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/amlogic/media/ge2d/ge2d.h>
#include <linux/amlogic/media/canvas/canvas.h>
#include <linux/amlogic/media/canvas/canvas_mgr.h>
#include <linux/amlogic/media/utils/amlog.h>
#include <linux/amlogic/media/ge2d/ge2d_func.h>
#include "vframe_ge2d_composer.h"
#include "vfq.h"
/* media module used media/registers/cpu_version.h since kernel 5.4 */
#include <linux/amlogic/media/registers/cpu_version.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <linux/amlogic/media/codec_mm/codec_mm.h>
#include "vc_util.h"

#define WIDTH_8K 7680
#define HEIGHT_8K 7680
#define IS_DI_PSTLINK(di_flag) ((di_flag) & DI_FLAG_DI_PSTVPPLINK)

#ifdef CONFIG_AMLOGIC_ENABLE_VIDEO_PIPELINE_DUMP_DATA
static int dump_src_count;
static int dump_before_dst_count;
static int dump_dst_count;
static int dump_black_count;
#endif

static unsigned int ge2d_com_debug;
MODULE_PARM_DESC(ge2d_com_debug, "\n ge2d_com_debug\n");
module_param(ge2d_com_debug, uint, 0664);

#ifndef CONFIG_AMLOGIC_MEDIA_GE2D
void stretchblt_noalpha(struct ge2d_context_s *wq,
			int src_x, int src_y, int src_w, int src_h,
			int dst_x, int dst_y, int dst_w, int dst_h)
{
}

int  destroy_ge2d_work_queue(struct ge2d_context_s *ge2d_work_queue)
{
	return 0;
}

void fillrect(struct ge2d_context_s *wq,
	      int x, int y, int w, int h, unsigned int color)
{
}
#endif

#ifndef CONFIG_AMLOGIC_UVM_CORE
struct vframe_s *dmabuf_get_vframe(struct dma_buf *dmabuf)
{
	return NULL;
}
#endif

static int get_source_type(struct src_data_para *src_data)
{
	enum videocom_source_type ret;
	int interlace_mode;

	interlace_mode = src_data->type & VIDTYPE_TYPEMASK;
	if (src_data->source_type == VFRAME_SOURCE_TYPE_HDMI ||
	    src_data->source_type == VFRAME_SOURCE_TYPE_CVBS) {
		if ((src_data->bitdepth & BITDEPTH_Y10)  &&
		    (!(src_data->type & VIDTYPE_COMPRESS)) &&
		    (get_cpu_type() >= MESON_CPU_MAJOR_ID_TXL))
			ret = VDIN_10BIT_NORMAL;
		else
			ret = VDIN_8BIT_NORMAL;
	} else {
		if ((src_data->bitdepth & BITDEPTH_Y10)  &&
		    (!(src_data->type & VIDTYPE_COMPRESS)) &&
		    (get_cpu_type() >= MESON_CPU_MAJOR_ID_TXL)) {
			if (interlace_mode == VIDTYPE_INTERLACE_TOP)
				ret = DECODER_10BIT_TOP;
			else if (interlace_mode == VIDTYPE_INTERLACE_BOTTOM)
				ret = DECODER_10BIT_BOTTOM;
			else
				ret = DECODER_10BIT_NORMAL;
		} else {
			if (interlace_mode == VIDTYPE_INTERLACE_TOP)
				ret = DECODER_8BIT_TOP;
			else if (interlace_mode == VIDTYPE_INTERLACE_BOTTOM)
				ret = DECODER_8BIT_BOTTOM;
			else
				ret = DECODER_8BIT_NORMAL;
		}
	}
	return ret;
}

static int get_input_format(struct src_data_para *src_data)
{
	int format = GE2D_FORMAT_M24_YUV420;
	enum videocom_source_type soure_type;

	soure_type = get_source_type(src_data);
	switch (soure_type) {
	case DECODER_8BIT_NORMAL:
		if (src_data->type & VIDTYPE_VIU_422)
			format = GE2D_FORMAT_S16_YUV422;
		else if (src_data->type & VIDTYPE_VIU_NV21)
			format = GE2D_FORMAT_M24_NV21;
		else if (src_data->type & VIDTYPE_VIU_NV12)
			format = GE2D_FORMAT_M24_NV12;
		else if (src_data->type & VIDTYPE_VIU_444)
			format = GE2D_FORMAT_S24_YUV444;
		else
			format = GE2D_FORMAT_M24_YUV420;
		break;
	case DECODER_8BIT_BOTTOM:
		if (src_data->type & VIDTYPE_VIU_422)
			format = GE2D_FORMAT_S16_YUV422
				| (GE2D_FORMAT_S16_YUV422B & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_NV21)
			format = GE2D_FORMAT_M24_NV21
				| (GE2D_FORMAT_M24_NV21B & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_NV12)
			format = GE2D_FORMAT_M24_NV12
				| (GE2D_FORMAT_M24_NV12B & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_444)
			format = GE2D_FORMAT_S24_YUV444
				| (GE2D_FORMAT_S24_YUV444B & (3 << 3));
		else
			format = GE2D_FORMAT_M24_YUV420
				| (GE2D_FMT_M24_YUV420B & (3 << 3));
		break;
	case DECODER_8BIT_TOP:
		if (src_data->type & VIDTYPE_VIU_422)
			format = GE2D_FORMAT_S16_YUV422
				| (GE2D_FORMAT_S16_YUV422T & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_NV21)
			format = GE2D_FORMAT_M24_NV21
				| (GE2D_FORMAT_M24_NV21T & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_NV12)
			format = GE2D_FORMAT_M24_NV12
				| (GE2D_FORMAT_M24_NV12T & (3 << 3));
		else if (src_data->type & VIDTYPE_VIU_444)
			format = GE2D_FORMAT_S24_YUV444
				| (GE2D_FORMAT_S24_YUV444T & (3 << 3));
		else
			format = GE2D_FORMAT_M24_YUV420
				| (GE2D_FMT_M24_YUV420T & (3 << 3));
		break;
	case DECODER_10BIT_NORMAL:
		if (src_data->type & VIDTYPE_VIU_422) {
			if (src_data->bitdepth & FULL_PACK_422_MODE)
				format = GE2D_FORMAT_S16_10BIT_YUV422;
			else
				format = GE2D_FORMAT_S16_12BIT_YUV422;
		}
		break;
	case DECODER_10BIT_BOTTOM:
		if (src_data->type & VIDTYPE_VIU_422) {
			if (src_data->bitdepth & FULL_PACK_422_MODE)
				format = GE2D_FORMAT_S16_10BIT_YUV422
					| (GE2D_FORMAT_S16_10BIT_YUV422B
					& (3 << 3));
			else
				format = GE2D_FORMAT_S16_12BIT_YUV422
					| (GE2D_FORMAT_S16_12BIT_YUV422B
					& (3 << 3));
		}
		break;
	case DECODER_10BIT_TOP:
		if (src_data->type & VIDTYPE_VIU_422) {
			if (src_data->bitdepth & FULL_PACK_422_MODE)
				format = GE2D_FORMAT_S16_10BIT_YUV422
					| (GE2D_FORMAT_S16_10BIT_YUV422T
					& (3 << 3));
			else
				format = GE2D_FORMAT_S16_12BIT_YUV422
					| (GE2D_FORMAT_S16_12BIT_YUV422T
					& (3 << 3));
		}
		break;
	case VDIN_8BIT_NORMAL:
		if (src_data->type & VIDTYPE_VIU_422) {
			format = GE2D_FORMAT_S16_YUV422;
		} else if (src_data->type & VIDTYPE_VIU_NV21) {
			format = GE2D_FORMAT_M24_NV21;
		} else if (src_data->type & VIDTYPE_VIU_NV12) {
			format = GE2D_FORMAT_M24_NV12;
		} else if (src_data->type & VIDTYPE_VIU_444) {
			format = GE2D_FORMAT_S24_YUV444;
		} else if (src_data->type & VIDTYPE_RGB_444) {
			format = GE2D_FORMAT_S24_RGB;
			if (src_data->is_vframe)
				format &= (~GE2D_LITTLE_ENDIAN);
		} else {
			format = GE2D_FORMAT_M24_YUV420;
		}
		break;
	case VDIN_10BIT_NORMAL:
		if (src_data->type & VIDTYPE_VIU_422) {
			if (src_data->bitdepth & FULL_PACK_422_MODE)
				format = GE2D_FORMAT_S16_10BIT_YUV422;
			else
				format = GE2D_FORMAT_S16_12BIT_YUV422;
		} else if (src_data->type & VIDTYPE_VIU_444) {
			format = GE2D_FORMAT_S24_10BIT_YUV444;
		} else if (src_data->type & VIDTYPE_RGB_444) {
			format = GE2D_FORMAT_S24_10BIT_RGB888;
			if (!src_data->is_vframe)
				format |= GE2D_LITTLE_ENDIAN;
		}
		break;
	default:
		format = GE2D_FORMAT_M24_YUV420;
	}
	return format;
}

static enum ge2d_angle_type config_ge2d_rotation(int vf_transform)
{
	int ge2d_angle = 0;

	if (vf_transform == 1)
		ge2d_angle = GE2D_ANGLE_TYPE_FLIP_H;
	else if (vf_transform == 2)
		ge2d_angle = GE2D_ANGLE_TYPE_FLIP_V;
	else if (vf_transform == 4 || vf_transform == 5 || vf_transform == 6)
		ge2d_angle = GE2D_ANGLE_TYPE_ROT_90;
	else if (vf_transform == 3)
		ge2d_angle = GE2D_ANGLE_TYPE_ROT_180;
	else if (vf_transform == 7)
		ge2d_angle = GE2D_ANGLE_TYPE_ROT_270;
	else if (vf_transform != 0)
		VIDEOCOM_INFO("%s: not support transform=%d\n", __func__, vf_transform);

	return ge2d_angle;
}

static int alloc_src_canvas(struct ge2d_composer_para *ge2d_comp_para)
{
	const char *keep_owner = "ge2d_composer";

	if (ge2d_comp_para->canvas_scr[0] < 0)
		ge2d_comp_para->canvas_scr[0] =
			canvas_pool_map_alloc_canvas(keep_owner);
	if (ge2d_comp_para->canvas_scr[1] < 0)
		ge2d_comp_para->canvas_scr[1] =
			canvas_pool_map_alloc_canvas(keep_owner);
	if (ge2d_comp_para->canvas_scr[2] < 0)
		ge2d_comp_para->canvas_scr[2] =
			canvas_pool_map_alloc_canvas(keep_owner);

	if (ge2d_comp_para->canvas_scr[0] < 0 ||
	    ge2d_comp_para->canvas_scr[1] < 0 ||
	    ge2d_comp_para->canvas_scr[2] < 0) {
		VIDEOCOM_INFO("%s failed\n", __func__);
		return -1;
	}
	return 0;
}

static int free_src_canvas(struct ge2d_composer_para *ge2d_comp_para)
{
	if (ge2d_comp_para->canvas_scr[0] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_scr[0]);
		ge2d_comp_para->canvas_scr[0] = -1;
	}

	if (ge2d_comp_para->canvas_scr[1] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_scr[1]);
		ge2d_comp_para->canvas_scr[1] = -1;
	}

	if (ge2d_comp_para->canvas_scr[2] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_scr[2]);
		ge2d_comp_para->canvas_scr[2] = -1;
	}
	if (ge2d_comp_para->canvas_scr[0] >= 0 ||
	    ge2d_comp_para->canvas_scr[1] >= 0 ||
	    ge2d_comp_para->canvas_scr[2] >= 0) {
		VIDEOCOM_INFO("%s failed!\n", __func__);
		return -1;
	}
	return 0;
}

int init_ge2d_composer(struct ge2d_composer_para *ge2d_comp_para)
{
	const char *keep_owner = "ge2d_dest_comp";

	ge2d_comp_para->context = create_ge2d_work_queue();
	if (IS_ERR_OR_NULL(ge2d_comp_para->context)) {
		VIDEOCOM_INFO("creat ge2d work failed\n");
		return -1;
	}

	if (ge2d_comp_para->plane_num > 0) {
		if (ge2d_comp_para->canvas_dst[0] < 0)
			ge2d_comp_para->canvas_dst[0] =
				canvas_pool_map_alloc_canvas(keep_owner);
		if (ge2d_comp_para->canvas_dst[0] < 0) {
			VIDEOCOM_ERR("ge2d init dst canvas_0 failed!\n");
			return -1;
		}
	}

	if (ge2d_comp_para->plane_num > 1) {
		if (ge2d_comp_para->canvas_dst[1] < 0)
			ge2d_comp_para->canvas_dst[1] =
				canvas_pool_map_alloc_canvas(keep_owner);
		if (ge2d_comp_para->canvas_dst[1] < 0) {
			VIDEOCOM_ERR("ge2d init dst canvas_1 failed!\n");
			return -1;
		}
	}

	if (ge2d_comp_para->plane_num > 2) {
		if (ge2d_comp_para->canvas_dst[2] < 0)
			ge2d_comp_para->canvas_dst[2] =
				canvas_pool_map_alloc_canvas(keep_owner);
		if (ge2d_comp_para->canvas_dst[2] < 0) {
			VIDEOCOM_ERR("ge2d init dst canvas_2 failed!\n");
			return -1;
		}
	}
	return 0;
}

int uninit_ge2d_composer(struct ge2d_composer_para *ge2d_comp_para)
{
	destroy_ge2d_work_queue(ge2d_comp_para->context);
	ge2d_comp_para->context = NULL;

	if (free_src_canvas(ge2d_comp_para) < 0) {
		VIDEOCOM_ERR("free src canvas failed!\n");
		return -1;
	}

	if (ge2d_comp_para->canvas_dst[0] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_dst[0]);
		ge2d_comp_para->canvas_dst[0] = -1;
	}

	if (ge2d_comp_para->canvas_dst[1] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_dst[1]);
		ge2d_comp_para->canvas_dst[1] = -1;
	}

	if (ge2d_comp_para->canvas_dst[2] >= 0) {
		canvas_pool_map_free_canvas(ge2d_comp_para->canvas_dst[2]);
		ge2d_comp_para->canvas_dst[2] = -1;
	}

	if (ge2d_comp_para->canvas_dst[0] >= 0 ||
	    ge2d_comp_para->canvas_dst[1] >= 0 ||
	    ge2d_comp_para->canvas_dst[2] >= 0) {
		VIDEOCOM_ERR("free dst canvas failed!\n");
		return -1;
	}
	return 0;
}

#ifdef CONFIG_AMLOGIC_ENABLE_VIDEO_PIPELINE_DUMP_DATA
static int copy_phybuf_to_file(struct canvas_config_s *config,
			       struct file *fp, loff_t pos)
{
	u32 span = SZ_1M;
	u8 *p;
	int remain_size = 0;
	ssize_t ret;
	ulong phys;

	if (IS_ERR_OR_NULL(config))
		return -1;

	phys = config->phy_addr;
	remain_size = config->width * config->height;
	while (remain_size > 0) {
		if (remain_size < span)
			span = remain_size;
		p = codec_mm_vmap(phys, PAGE_ALIGN(span));
		if (!p) {
			VIDEOCOM_ERR("vmap failed\n");
			return -1;
		}
		codec_mm_dma_flush(p, span, DMA_FROM_DEVICE);
		ret = kernel_write(fp, (char *)p, span, &pos);
		if (ret <= 0)
			VIDEOCOM_ERR("vfs write failed!\n");
		phys += span;
		codec_mm_unmap_phyaddr(p);
		remain_size -= span;

		VIDEOCOM_INFO("pos: %lld, phys: %lx, remain_size: %d\n",
			     pos, phys, remain_size);
	}
	return 0;
}
#endif
static bool dump_data(struct dump_param *para, enum buffer_data type)
{
#ifdef CONFIG_AMLOGIC_ENABLE_VIDEO_PIPELINE_DUMP_DATA
	bool ret = false;
	struct file *filp_dst = NULL;
	char dst_path[64] = {'\0'};
	struct canvas_config_s *dump_config;
	int result = 0;
	int offset = 0;

	offset = 0;
	if (type == BLACK_BUFFER)
		sprintf(dst_path, "/data/temp/black_%d", dump_black_count++);
	else if (type == SCR_BUFFER)
		sprintf(dst_path, "/data/temp/scr_%d", dump_src_count++);
	else if (type == DST_EMPTY_BUFFER)
		sprintf(dst_path,
			"/data/temp/before_dst_%d",
			dump_before_dst_count++);
	else if (type == DST_BUFFER_DATA)
		sprintf(dst_path, "/data/temp/dst_%d", dump_dst_count++);
	filp_dst = filp_open(dst_path, O_RDWR | O_CREAT, 0666);
	if (IS_ERR(filp_dst)) {
		VIDEOCOM_ERR("open %s failed\n", dst_path);
		ret = false;
	} else {
		dump_config = &para->canvas0_config[0];
		result = copy_phybuf_to_file(dump_config, filp_dst, 0);
		if (result < 0) {
			VIDEOCOM_ERR("write %s failed\n", dst_path);
			ret = false;
			goto end;
		}
		if (para->plane_num >= 2) {
			offset = dump_config->width *
				dump_config->height;
			dump_config = &para->canvas0_config[1];
			result =
			copy_phybuf_to_file(dump_config,
					    filp_dst, offset);
			if (result < 0) {
				VIDEOCOM_ERR("#1 write %s failed\n",
					     dst_path);
				ret = false;
				goto end;
			}
		}
		if (para->plane_num >= 3) {
			offset += dump_config->width *
				dump_config->height;
			dump_config = &para->canvas0_config[2];
			result =
			copy_phybuf_to_file(dump_config,
					    filp_dst, offset);
			if (result < 0) {
				VIDEOCOM_ERR("#2 write %s failed\n",
					     dst_path);
				ret = false;
				goto end;
			}
		}
		filp_close(filp_dst, NULL);
		ret = true;
	}
end:
	return ret;
#else
	return false;
#endif
}

int fill_vframe_black(struct ge2d_composer_para *ge2d_comp_para)
{
	struct canvas_config_s dst_canvas0_config[3];
	struct config_para_ex_s ge2d_config;
	u32 dst_plane_num;
	struct dump_param para;
	bool ret = false;
	int temp = 0;
	int canvas_index = 0;

	memset(dst_canvas0_config, 0, sizeof(dst_canvas0_config));
	memset(&ge2d_config, 0, sizeof(struct config_para_ex_s));

	if (ge2d_comp_para->format == GE2D_FORMAT_S24_YUV444) {
		dst_canvas0_config[0].phy_addr = ge2d_comp_para->phy_addr[0];
		dst_canvas0_config[0].width = ge2d_comp_para->buffer_w * 3;
		dst_canvas0_config[0].height = ge2d_comp_para->buffer_h;
		dst_canvas0_config[0].block_mode = 0;
		dst_canvas0_config[0].endian = 0;
	} else if (ge2d_comp_para->format == GE2D_FORMAT_M24_NV21) {
		dst_canvas0_config[0].phy_addr = ge2d_comp_para->phy_addr[0];
		dst_canvas0_config[0].width = ge2d_comp_para->buffer_w;
		dst_canvas0_config[0].height = ge2d_comp_para->buffer_h;
		dst_canvas0_config[0].block_mode = 0;
		dst_canvas0_config[0].endian = 0;
		dst_canvas0_config[1].phy_addr = ge2d_comp_para->phy_addr[0]
			+ ge2d_comp_para->buffer_w * ge2d_comp_para->buffer_h;
		dst_canvas0_config[1].width = ge2d_comp_para->buffer_w;
		dst_canvas0_config[1].height = ge2d_comp_para->buffer_h >> 1;
		dst_canvas0_config[1].block_mode = 0;
		dst_canvas0_config[1].endian = 0;
	}
	dst_plane_num = ge2d_comp_para->plane_num;

	if (ge2d_comp_para->canvas0_addr == (u32)-1) {
		canvas_config_config(ge2d_comp_para->canvas_dst[0],
				     &dst_canvas0_config[0]);
		if (dst_plane_num == 2) {
			canvas_config_config(ge2d_comp_para->canvas_dst[1],
					     &dst_canvas0_config[1]);
		} else if (dst_plane_num == 3) {
			canvas_config_config(ge2d_comp_para->canvas_dst[2],
					     &dst_canvas0_config[2]);
		}
		canvas_index = ge2d_comp_para->canvas_dst[0]
						| (ge2d_comp_para->canvas_dst[1] << 8)
						| (ge2d_comp_para->canvas_dst[2] << 16);

	} else {
		canvas_index = ge2d_comp_para->canvas0_addr;
	}

	ge2d_config.src_para.canvas_index = canvas_index;
	ge2d_config.alu_const_color = 0;/*0x000000ff;*/
	ge2d_config.bitmask_en = 0;
	ge2d_config.src1_gb_alpha = 0;/*0xff;*/
	ge2d_config.dst_xy_swap = 0;

	ge2d_config.src_key.key_enable = 0;
	ge2d_config.src_key.key_mask = 0;
	ge2d_config.src_key.key_mode = 0;
	ge2d_config.src_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config.src_para.format = ge2d_comp_para->format;
	ge2d_config.src_para.fill_color_en = 0;
	ge2d_config.src_para.fill_mode = 0;
	ge2d_config.src_para.x_rev = 0;
	ge2d_config.src_para.y_rev = 0;
	ge2d_config.src_para.color = 0;
	ge2d_config.src_para.top = 0;
	ge2d_config.src_para.left = 0;
	ge2d_config.src_para.width = ge2d_comp_para->buffer_w;
	ge2d_config.src_para.height = ge2d_comp_para->buffer_h;
	ge2d_config.src2_para.mem_type = CANVAS_TYPE_INVALID;

	ge2d_config.dst_para.canvas_index = canvas_index;
	ge2d_config.dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config.dst_para.fill_color_en = 0;
	ge2d_config.dst_para.fill_mode = 0;
	ge2d_config.dst_para.x_rev = 0;
	ge2d_config.dst_para.y_rev = 0;
	ge2d_config.dst_para.color = 0;
	ge2d_config.dst_para.top = 0;
	ge2d_config.dst_para.left = 0;
	ge2d_config.dst_para.format = ge2d_comp_para->format;
	if (ge2d_comp_para->format == GE2D_FORMAT_M24_NV21)
		ge2d_config.dst_para.format |= GE2D_LITTLE_ENDIAN;
	ge2d_config.dst_para.width = ge2d_comp_para->buffer_w;
	ge2d_config.dst_para.height = ge2d_comp_para->buffer_h;
	ge2d_config.mem_sec = ge2d_comp_para->is_tvp;

	temp = ge2d_context_config_ex(ge2d_comp_para->context, &ge2d_config);
	if (temp < 0) {
		VIDEOCOM_ERR("++ge2d configing error.\n");
		return -1;
	}

	fillrect(ge2d_comp_para->context, 0, 0,
		 ge2d_comp_para->buffer_w,
		 ge2d_comp_para->buffer_h,
		 0x008080ff);
	if (ge2d_com_debug & 2) {
		para.canvas0_config[0] = dst_canvas0_config[0];
		para.canvas0_config[1] = dst_canvas0_config[1];
		para.canvas0_config[2] = dst_canvas0_config[2];
		para.plane_num = dst_plane_num;
		ret = dump_data(&para, BLACK_BUFFER);
		if (ret)
			VIDEOCOM_INFO("dump black buffer successful.\n");
	}

	return 0;
}

int config_ge2d_data(struct vframe_s *src_vf, unsigned long addr, int buf_w, int buf_h,
	int data_w, int data_h, int crop_x, int crop_y, int crop_w, int crop_h,
	struct src_data_para *data)
{
	struct vframe_s *vf = NULL;
	int vf_height;

	if (IS_ERR_OR_NULL(data)) {
		pr_info("%s: invalid param.\n", __func__);
		return -1;
	}

	if (src_vf) {
		if (src_vf->canvas0_config[0].phy_addr == 0) {
			if ((src_vf->flag &  VFRAME_FLAG_DOUBLE_FRAM) && src_vf->vf_ext) {
				vf = src_vf->vf_ext;
			} else {
				VIDEOCOM_ERR("%s: vf no yuv data, composer fail\n", __func__);
				return -1;
			}
		} else {
			vf = src_vf;
		}

		if (vf && vf->di_flag && IS_DI_PSTLINK(vf->di_flag))
			vf_height = vf->height >> 1;
		else
			vf_height = vf->height;

		data->canvas0Addr = vf->canvas0Addr;
		data->canvas1Addr = vf->canvas1Addr;
		data->canvas0_config[0] = vf->canvas0_config[0];
		data->canvas0_config[1] = vf->canvas0_config[1];
		data->canvas0_config[2] = vf->canvas0_config[2];
		data->canvas1_config[0] = vf->canvas1_config[0];
		data->canvas1_config[1] = vf->canvas1_config[1];
		data->canvas1_config[2] = vf->canvas1_config[2];
		data->bitdepth = vf->bitdepth;
		data->source_type = vf->source_type;
		data->type = vf->type;
		data->plane_num = vf->plane_num;

		if (vf->type & VIDTYPE_COMPRESS) {
			if ((crop_w > WIDTH_8K  || crop_w < 0) ||
				(crop_h > HEIGHT_8K || crop_h < 0)) {
				data->width = vf->width;
				data->height = vf_height;
			} else {
				data->position_x = crop_x * vf->width / vf->compWidth;
				data->position_y = crop_y * vf_height / vf->compHeight;
				data->width = crop_w * vf->width / vf->compWidth;
				data->height = crop_h * vf_height / vf->compHeight;
			}
		} else {
			if ((crop_w > WIDTH_8K  || crop_w < 0) ||
				(crop_h > HEIGHT_8K || crop_h < 0)) {
				data->width = vf->width;
				data->height = vf_height;
			} else {
				data->position_x = crop_x;
				data->position_y = crop_y;
				data->width = crop_w;
				data->height = crop_h;
				if (vf && vf->di_flag && IS_DI_PSTLINK(vf->di_flag)) {
					data->position_y >>= 1;
					data->height >>= 1;
				}
			}
		}

		if (vf->flag & VFRAME_FLAG_VIDEO_LINEAR)
			data->is_vframe = false;
		else
			data->is_vframe = true;
	} else {
		data->canvas0Addr = -1;
		data->canvas1Addr = -1;
		data->canvas0_config[0].phy_addr = addr;

		VIDEOCOM_INFO("buffer_w(%d), data_w(%d)\n", buf_w, data_w);
		if (buf_w > data_w) {
			if (data->is_yuv444)
				data->canvas0_config[0].width = buf_w * 3;
			else
				data->canvas0_config[0].width = buf_w;
		} else {
			if (data->is_yuv444)
				data->canvas0_config[0].width = data_w * 3;
			else
				data->canvas0_config[0].width = data_w;
		}
		VIDEOCOM_INFO("buffer_h(%d), data_h(%d)\n", buf_h, data_h);

		if (buf_h > data_h)
			data->canvas0_config[0].height = buf_h;
		else
			data->canvas0_config[0].height = data_h;

		data->canvas0_config[0].block_mode = CANVAS_BLKMODE_LINEAR;
		data->canvas0_config[0].endian = 0;
		data->canvas0_config[1].phy_addr =
			(u32)(addr + (data->canvas0_config[0].width)
			      * (data->canvas0_config[0].height));
		data->canvas0_config[1].width =
			data->canvas0_config[0].width;
		data->canvas0_config[1].height =
			data->canvas0_config[0].height / 2;
		data->canvas0_config[1].block_mode =
			CANVAS_BLKMODE_LINEAR;
		data->canvas0_config[1].endian = 0;
		data->bitdepth = BITDEPTH_Y8 | BITDEPTH_U8 | BITDEPTH_V8;
		data->source_type = 0;
		if (data->is_yuv444) {
			data->type = VIDTYPE_VIU_SINGLE_PLANE
				| VIDTYPE_VIU_FIELD
				| VIDTYPE_VIU_444;
			data->plane_num = 1;
		} else {
			data->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
			data->plane_num = 2;
		}

		VIDEOCOM_INFO("crop %d %d %d %d\n", crop_x, crop_y, crop_w, crop_h);
		data->position_x = crop_x;
		data->position_y = crop_y;
		data->is_vframe = false;
		if (crop_w <= 0 || crop_h <= 0) {
			data->width = buf_w;
			data->height = buf_h;
		} else {
			data->width = crop_w;
			data->height = crop_h;
		}
	}

	return 0;
}

int ge2d_data_composer(struct src_data_para *scr_data,
		       struct ge2d_composer_para *ge2d_comp_para)
{
	int ret;
	struct canvas_config_s dst_canvas0_config[3];
	struct config_para_ex_s ge2d_config;
	u32 dst_plane_num;
	int src_canvas_id, dst_canvas_id;
	int input_x, input_y, input_width, input_height;
	int position_left, position_top;
	int position_width, position_height;
	struct dump_param para;
	enum ge2d_angle_type ge2d_angle;
	bool result = false;

	memset(&ge2d_config, 0, sizeof(struct config_para_ex_s));
	memset(dst_canvas0_config, 0, sizeof(dst_canvas0_config));

	if (ge2d_comp_para->format == GE2D_FORMAT_S24_YUV444) {
		dst_canvas0_config[0].phy_addr = ge2d_comp_para->phy_addr[0];
		dst_canvas0_config[0].width = ge2d_comp_para->buffer_w * 3;
		dst_canvas0_config[0].height = ge2d_comp_para->buffer_h;
		dst_canvas0_config[0].block_mode = 0;
		dst_canvas0_config[0].endian = 0;
	} else if (ge2d_comp_para->format == GE2D_FORMAT_M24_NV21) {
		dst_canvas0_config[0].phy_addr = ge2d_comp_para->phy_addr[0];
		dst_canvas0_config[0].width = ge2d_comp_para->buffer_w;
		dst_canvas0_config[0].height = ge2d_comp_para->buffer_h;
		dst_canvas0_config[0].block_mode = 0;
		dst_canvas0_config[0].endian = 0;
		dst_canvas0_config[1].phy_addr = ge2d_comp_para->phy_addr[0]
			+ ge2d_comp_para->buffer_w * ge2d_comp_para->buffer_h;
		dst_canvas0_config[1].width = ge2d_comp_para->buffer_w;
		dst_canvas0_config[1].height = ge2d_comp_para->buffer_h >> 1;
		dst_canvas0_config[1].block_mode = 0;
		dst_canvas0_config[1].endian = 0;
	}
	dst_plane_num = ge2d_comp_para->plane_num;

	if (scr_data->canvas0Addr == (u32)-1) {
		ret = alloc_src_canvas(ge2d_comp_para);
		if (ret < 0) {
			VIDEOCOM_ERR("alloc src canvas failed!\n");
			return -1;
		}
		canvas_config_config(ge2d_comp_para->canvas_scr[0],
				     &scr_data->canvas0_config[0]);
		if (scr_data->plane_num == 2) {
			canvas_config_config(ge2d_comp_para->canvas_scr[1],
					     &scr_data->canvas0_config[1]);
		} else if (scr_data->plane_num == 3) {
			canvas_config_config(ge2d_comp_para->canvas_scr[2],
					     &scr_data->canvas0_config[2]);
		}
		src_canvas_id = ge2d_comp_para->canvas_scr[0]
			| (ge2d_comp_para->canvas_scr[1] << 8)
			| (ge2d_comp_para->canvas_scr[2] << 16);
	} else {
		src_canvas_id = scr_data->canvas0Addr;
	}

	if (ge2d_comp_para->canvas0_addr == (u32)-1) {
		canvas_config_config(ge2d_comp_para->canvas_dst[0],
				     &dst_canvas0_config[0]);
		if (dst_plane_num == 2) {
			canvas_config_config(ge2d_comp_para->canvas_dst[1],
					     &dst_canvas0_config[1]);
		} else if (dst_plane_num == 3) {
			canvas_config_config(ge2d_comp_para->canvas_dst[2],
					     &dst_canvas0_config[2]);
		}
		dst_canvas_id = ge2d_comp_para->canvas_dst[0]
			| (ge2d_comp_para->canvas_dst[1] << 8)
			| (ge2d_comp_para->canvas_dst[2] << 16);
	} else {
		dst_canvas_id = ge2d_comp_para->canvas0_addr;
	}
	input_width = scr_data->width;
	input_height = scr_data->height;
	input_x = scr_data->position_x;
	input_y = scr_data->position_y;

	if (scr_data->type & VIDTYPE_INTERLACE)
		input_height = scr_data->height >> 1;
	else
		input_height = scr_data->height;

	ge2d_config.alu_const_color = 0;
	ge2d_config.bitmask_en = 0;
	ge2d_config.src1_gb_alpha = 0;
	ge2d_config.dst_xy_swap = 0;
	ge2d_config.src_key.key_enable = 0;
	ge2d_config.src_key.key_mask = 0;
	ge2d_config.src_key.key_mode = 0;
	ge2d_config.src_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config.src_para.format = get_input_format(scr_data);
	if (!scr_data->is_vframe)
		ge2d_config.src_para.format |= GE2D_LITTLE_ENDIAN;
	ge2d_config.src_para.fill_color_en = 0;
	ge2d_config.src_para.fill_mode = 0;
	ge2d_config.src_para.x_rev = 0;
	ge2d_config.src_para.y_rev = 0;
	ge2d_config.src_para.color = 0xffffffff;
	ge2d_config.src_para.top = input_y;
	ge2d_config.src_para.left = input_x;
	ge2d_config.src_para.width = input_width;
	ge2d_config.src_para.height = input_height;
	ge2d_config.src_para.canvas_index = src_canvas_id;
	ge2d_config.src2_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config.dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config.dst_para.fill_color_en = 0;
	ge2d_config.dst_para.fill_mode = 0;
	ge2d_config.dst_para.x_rev = 0;
	ge2d_config.dst_para.y_rev = 0;
	ge2d_config.dst_xy_swap = 0;
	ge2d_angle = config_ge2d_rotation(ge2d_comp_para->angle);

	if (ge2d_angle == GE2D_ANGLE_TYPE_ROT_90) {
		ge2d_config.dst_xy_swap = 1;
		ge2d_config.dst_para.x_rev = 1;
	} else if (ge2d_angle == GE2D_ANGLE_TYPE_ROT_180) {
		ge2d_config.dst_para.x_rev = 1;
		ge2d_config.dst_para.y_rev = 1;
	} else if (ge2d_angle == GE2D_ANGLE_TYPE_ROT_270) {
		ge2d_config.dst_xy_swap = 1;
		ge2d_config.dst_para.y_rev = 1;
	} else if (ge2d_angle == GE2D_ANGLE_TYPE_FLIP_H) {
		ge2d_config.dst_xy_swap = 0;
		ge2d_config.dst_para.x_rev = 1;
		ge2d_config.dst_para.y_rev = 0;
	} else if (ge2d_angle == GE2D_ANGLE_TYPE_FLIP_V) {
		ge2d_config.dst_xy_swap = 0;
		ge2d_config.dst_para.x_rev = 0;
		ge2d_config.dst_para.y_rev = 1;
	}

	ge2d_config.dst_para.canvas_index = dst_canvas_id;
	ge2d_config.dst_para.color = 0;
	ge2d_config.dst_para.top = 0;
	ge2d_config.dst_para.left = 0;
	ge2d_config.dst_para.format = ge2d_comp_para->format;
	if (ge2d_comp_para->format == GE2D_FORMAT_M24_NV21)
		ge2d_config.dst_para.format |= GE2D_LITTLE_ENDIAN;
	ge2d_config.dst_para.width = ge2d_comp_para->buffer_w;
	ge2d_config.dst_para.height = ge2d_comp_para->buffer_h;
	ge2d_config.mem_sec = ge2d_comp_para->is_tvp;

	ret = ge2d_context_config_ex(ge2d_comp_para->context, &ge2d_config);
	if (ret < 0) {
		VIDEOCOM_ERR("++ge2d configing error.\n");
		return -1;
	}
	if (ge2d_com_debug & 4) {
		para.canvas0_config[0] = scr_data->canvas0_config[0];
		para.canvas0_config[1] = scr_data->canvas0_config[1];
		para.canvas0_config[2] = scr_data->canvas0_config[2];
		para.plane_num = scr_data->plane_num;
		result = dump_data(&para, SCR_BUFFER);
		if (result)
			VIDEOCOM_INFO("dump scr buffer successful.\n");
	}
	if (ge2d_com_debug & 8) {
		para.canvas0_config[0] = dst_canvas0_config[0];
		para.canvas0_config[1] = dst_canvas0_config[1];
		para.canvas0_config[2] = dst_canvas0_config[2];
		para.plane_num = dst_plane_num;
		result = dump_data(&para, DST_EMPTY_BUFFER);
		if (result)
			VIDEOCOM_INFO("dump dst empty buffer successful.\n");
	}

	position_left = ge2d_comp_para->position_left;
	position_top = ge2d_comp_para->position_top;
	position_width = ge2d_comp_para->position_width;
	position_height = ge2d_comp_para->position_height;

	stretchblt_noalpha(ge2d_comp_para->context,
			   input_x,
			   input_y,
			   input_width,
			   input_height,
			   position_left,
			   position_top,
			   position_width,
			   position_height);
	if (ge2d_com_debug & 1)
		VIDEOCOM_INFO("scr %0x,dst: %0x!,%d, %d, %d, %d\n",
			      ge2d_config.src_para.format,
			      ge2d_config.dst_para.format,
			      position_left,
			      position_top,
			      position_width,
			      position_height);
	if (ge2d_com_debug & 16) {
		para.canvas0_config[0] = dst_canvas0_config[0];
		para.canvas0_config[1] = dst_canvas0_config[1];
		para.canvas0_config[2] = dst_canvas0_config[2];
		para.plane_num = dst_plane_num;
		result = dump_data(&para, DST_BUFFER_DATA);
		if (result)
			VIDEOCOM_INFO("dump dst data successful.\n");
	}

	return 0;
}

