/*
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Description:
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/amlogic/media/frame_sync/ptsserv.h>

#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>

#include "../../frame_provider/decoder/utils/vdec.h"
#include "../amports/streambuf_reg.h"
#include "../amports/streambuf.h"
#include "esparser.h"
#include "../amports/amports_priv.h"
#include "../amports/thread_rw.h"

#include <linux/amlogic/media/codec_mm/codec_mm.h>



#define SAVE_SCR 0

#define ES_START_CODE_PATTERN 0x00000100
#define ES_START_CODE_MASK    0xffffff00
#define SEARCH_PATTERN_LEN   512
#define ES_PARSER_POP      READ_PARSER_REG(PFIFO_DATA)

#define PARSER_WRITE        (ES_WRITE | ES_PARSER_START)
#define PARSER_VIDEO        (ES_TYPE_VIDEO)
#define PARSER_AUDIO        (ES_TYPE_AUDIO)
#define PARSER_SUBPIC       (ES_TYPE_SUBTITLE)
#define PARSER_PASSTHROUGH  (ES_PASSTHROUGH | ES_PARSER_START)
#define PARSER_AUTOSEARCH   (ES_SEARCH | ES_PARSER_START)
#define PARSER_DISCARD      (ES_DISCARD | ES_PARSER_START)
#define PARSER_BUSY         (ES_PARSER_BUSY)

#define MAX_DRM_PACKAGE_SIZE 0x500000


static unsigned char *search_pattern;
static dma_addr_t search_pattern_map;
static u32 audio_real_wp;
static u32 audio_buf_start;
static u32 audio_buf_end;

static const char esparser_id[] = "esparser-id";

static DECLARE_WAIT_QUEUE_HEAD(wq);


static u32 search_done;
static u32 video_data_parsed;
static u32 audio_data_parsed;
static atomic_t esparser_use_count = ATOMIC_INIT(0);
static DEFINE_MUTEX(esparser_mutex);

static inline u32 get_buf_wp(u32 type)
{
	if (type == BUF_TYPE_AUDIO)
		return audio_real_wp;
	else
		return 0;
}
static inline u32 get_buf_start(u32 type)
{
	if (type == BUF_TYPE_AUDIO)
		return audio_buf_start;
	else
		return 0;
}
static inline u32 get_buf_end(u32 type)
{
	if (type == BUF_TYPE_AUDIO)
		return audio_buf_end;
	else
		return 0;
}
static void set_buf_wp(u32 type, u32 wp)
{
	if (type == BUF_TYPE_AUDIO) {
		audio_real_wp = wp;
		WRITE_AIU_REG(AIU_MEM_AIFIFO_MAN_WP, wp/* & 0xffffff00*/);
	}
	return;
}

static irqreturn_t esparser_isr(int irq, void *dev_id)
{
	u32 int_status = READ_PARSER_REG(PARSER_INT_STATUS);

	WRITE_PARSER_REG(PARSER_INT_STATUS, int_status);

	if (int_status & PARSER_INTSTAT_SC_FOUND) {
		WRITE_PARSER_REG(PFIFO_RD_PTR, 0);
		WRITE_PARSER_REG(PFIFO_WR_PTR, 0);
		search_done = 1;
		wake_up_interruptible(&wq);
	}
	return IRQ_HANDLED;
}

static inline u32 buf_wp(u32 type)
{
	u32 wp;

	if ((READ_PARSER_REG(PARSER_ES_CONTROL) & ES_VID_MAN_RD_PTR) == 0) {
		wp =
		/* MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 */
		(type == BUF_TYPE_HEVC) ? READ_VREG(HEVC_STREAM_WR_PTR) :
		(type == BUF_TYPE_VIDEO) ? READ_VREG(VLD_MEM_VIFIFO_WP) :
		(type == BUF_TYPE_AUDIO) ?
		READ_AIU_REG(AIU_MEM_AIFIFO_MAN_WP) :
		READ_PARSER_REG(PARSER_SUB_START_PTR);
	} else {
		wp =
		/* MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 */
		(type == BUF_TYPE_HEVC) ? READ_PARSER_REG(PARSER_VIDEO_WP) :
		(type == BUF_TYPE_VIDEO) ? READ_PARSER_REG(PARSER_VIDEO_WP) :
		(type == BUF_TYPE_AUDIO) ?
			READ_AIU_REG(AIU_MEM_AIFIFO_MAN_WP) :
			READ_PARSER_REG(PARSER_SUB_START_PTR);
	}

	return wp;
}

static int esparser_stbuf_write(struct stream_buf_s *stbuf, const u8 *buf, u32 count)
{
	size_t r = count;
	const char __user *p = buf;

	u32 len = 0;
	u32 parser_type;
	int ret;
	u32 wp;
	u32 type = stbuf->type;

	VDEC_PRINT_FUN_LINENO(__func__, __LINE__);
	if (type == BUF_TYPE_HEVC)
		parser_type = PARSER_VIDEO;
	else if (type == BUF_TYPE_VIDEO)
		parser_type = PARSER_VIDEO;
	else if (type == BUF_TYPE_AUDIO)
		parser_type = PARSER_AUDIO;
	else
		parser_type = PARSER_SUBPIC;

	wp = buf_wp(type);

	if (r > 0) {
		if (stbuf->is_phybuf)
			len = count;
		else {
			len = min_t(size_t, r, (size_t) FETCHBUF_SIZE);

			if (copy_from_user(fetchbuf.vaddr, p, len))
				return -EFAULT;
			codec_mm_dma_flush(fetchbuf.vaddr,
								fetchbuf.size,
								DMA_TO_DEVICE);
		}

		/* wmb(); don't need */
		/* reset the Write and read pointer to zero again */
		WRITE_PARSER_REG(PFIFO_RD_PTR, 0);
		WRITE_PARSER_REG(PFIFO_WR_PTR, 0);

		WRITE_PARSER_REG_BITS(PARSER_CONTROL, len, ES_PACK_SIZE_BIT,
							ES_PACK_SIZE_WID);
		WRITE_PARSER_REG_BITS(PARSER_CONTROL,
				parser_type | PARSER_WRITE |
				PARSER_AUTOSEARCH, ES_CTRL_BIT,
				ES_CTRL_WID);

		if (stbuf->is_phybuf) {
			u32 buf_32 = (unsigned long)buf & 0xffffffff;
			WRITE_PARSER_REG(PARSER_FETCH_ADDR, buf_32);
		} else {
			WRITE_PARSER_REG(PARSER_FETCH_ADDR, fetchbuf.paddr);
		}

		search_done = 0;
		if (!(stbuf->drm_flag & TYPE_PATTERN)) {
			WRITE_PARSER_REG(PARSER_FETCH_CMD,
				(7 << FETCH_ENDIAN) | len);
			WRITE_PARSER_REG(PARSER_FETCH_ADDR, search_pattern_map);
			WRITE_PARSER_REG(PARSER_FETCH_CMD,
				(7 << FETCH_ENDIAN) | SEARCH_PATTERN_LEN);
		} else {
			WRITE_PARSER_REG(PARSER_FETCH_CMD,
				(7 << FETCH_ENDIAN) | (len + 512));
		}

		ret = wait_event_interruptible_timeout(wq, search_done != 0,
			HZ / 5);
		if (ret == 0) {
			WRITE_PARSER_REG(PARSER_FETCH_CMD, 0);

			if (wp == buf_wp(type)) {
				/*no data fetched */
				return -EAGAIN;
			} else {
				pr_info("W Timeout, but fetch ok,");
				pr_info("type %d len=%d,wpdiff=%d, isphy %x\n",
				 type, len, wp - buf_wp(type), stbuf->is_phybuf);
			}
		} else if (ret < 0)
			return -ERESTARTSYS;
	}

	if ((type == BUF_TYPE_VIDEO)
		|| (has_hevc_vdec() && (type == BUF_TYPE_HEVC)))
		video_data_parsed += len;
	else if (type == BUF_TYPE_AUDIO)
		audio_data_parsed += len;

	threadrw_update_buffer_level(stbuf, len);
	VDEC_PRINT_FUN_LINENO(__func__, __LINE__);

	return len;
}

static ssize_t _esparser_write(const char __user *buf,
		size_t count, struct stream_buf_s *stbuf, int isphybuf)
{
	return esparser_stbuf_write(stbuf, buf, count);
}

static ssize_t _esparser_write_s(const char __user *buf,
			size_t count, struct stream_buf_s *stbuf)
{
	size_t r = count;
	const char __user *p = buf;
	u32 len = 0;
	int ret;
	u32 wp, buf_start, buf_end;
	u32 type = stbuf->type;
	void *vaddr = NULL;

	if (type != BUF_TYPE_AUDIO)
		BUG();
	wp = get_buf_wp(type);
	buf_end = get_buf_end(type) + 8;
	buf_start = get_buf_start(type);

	if (wp + count > buf_end) {
		if (wp == buf_end) {
			wp = buf_start;
			set_buf_wp(type, wp);
			return -EAGAIN;
		}
		vaddr = codec_mm_phys_to_virt(wp);
		ret = copy_from_user(vaddr, p, buf_end - wp);
		if (ret > 0) {
			len +=  buf_end - wp - ret;
			codec_mm_dma_flush(vaddr, len, DMA_TO_DEVICE);
			wp += len;
			pr_info("copy from user not finished\n");
			set_buf_wp(type, wp);
			goto end_write;
		} else if (ret == 0) {
			len += buf_end - wp;
			codec_mm_dma_flush(vaddr, len, DMA_TO_DEVICE);
			wp = buf_start;
			r = count - len;
			set_buf_wp(type, wp);
		} else {
			pr_info("copy from user failed 1\n");
			pr_info("w wp 0x%x, count %d, start 0x%x end 0x%x\n",
				 wp, (u32)count, buf_start, buf_end);
			return -EAGAIN;
		}
	}

	vaddr = codec_mm_phys_to_virt(wp);
	ret = copy_from_user(vaddr, p + len, r);
	if (ret >= 0) {
		len += r - ret;
		codec_mm_dma_flush(vaddr, r - ret, DMA_TO_DEVICE);
		if (ret > 0)
			pr_info("copy from user not finished 2\n");
		wp += r - ret;
		set_buf_wp(type, wp);
	} else {
		pr_info("copy from user failed 2\n");
		return -EAGAIN;
	}

end_write:
	if (type == BUF_TYPE_AUDIO)
	{
		audio_data_parsed += len;
		threadrw_update_buffer_level(stbuf, len);
	}

	return len;
}

s32 es_vpts_checkin_us64(struct stream_buf_s *buf, u64 us64)
{
	u32 passed;

	if (buf->write_thread)
		passed = threadrw_dataoffset(buf);
	else
		passed = video_data_parsed;
	return pts_checkin_offset_us64(PTS_TYPE_VIDEO, passed, us64);

}

s32 es_apts_checkin_us64(struct stream_buf_s *buf, u64 us64)
{
	u32 passed;

	if (buf->write_thread)
		passed = threadrw_dataoffset(buf);
	else
		passed = audio_data_parsed;
	return pts_checkin_offset_us64(PTS_TYPE_AUDIO, passed, us64);
}

s32 es_vpts_checkin(struct stream_buf_s *buf, u32 pts)
{
	u32 passed = 0;

	mutex_lock(&esparser_mutex);
	passed = video_data_parsed + threadrw_buffer_level(buf);
	mutex_unlock(&esparser_mutex);

	return pts_checkin_offset(PTS_TYPE_VIDEO, passed, pts);

}

s32 es_apts_checkin(struct stream_buf_s *buf, u32 pts)
{
	u32 passed = 0;
	mutex_lock(&esparser_mutex);
	passed = audio_data_parsed + threadrw_buffer_level(buf);
	mutex_unlock(&esparser_mutex);

	return pts_checkin_offset(PTS_TYPE_AUDIO, passed, pts);
}

s32 esparser_init(struct stream_buf_s *buf, struct vdec_s *vdec)
{
	s32 r = 0;
	u32 pts_type;
	u32 parser_sub_start_ptr;
	u32 parser_sub_end_ptr;
	u32 parser_sub_rp;
	bool first_use = false;
	VDEC_PRINT_FUN_LINENO(__func__, __LINE__);

	if (has_hevc_vdec() && (buf->type == BUF_TYPE_HEVC))
		pts_type = PTS_TYPE_HEVC;
	else
		if (buf->type == BUF_TYPE_VIDEO)
			pts_type = PTS_TYPE_VIDEO;
		else if (buf->type == BUF_TYPE_AUDIO)
			pts_type = PTS_TYPE_AUDIO;
		else if (buf->type == BUF_TYPE_SUBTITLE)
			pts_type = PTS_TYPE_MAX;
		else
			return -EINVAL;
	mutex_lock(&esparser_mutex);
	parser_sub_start_ptr = READ_PARSER_REG(PARSER_SUB_START_PTR);
	parser_sub_end_ptr = READ_PARSER_REG(PARSER_SUB_END_PTR);
	parser_sub_rp = READ_PARSER_REG(PARSER_SUB_RP);

	buf->flag |= BUF_FLAG_PARSER;

	if (atomic_add_return(1, &esparser_use_count) == 1) {
		first_use = true;

		if (stbuf_fetch_init()) {
			pr_info("%s: no fetchbuf\n", __func__);
			r = -ENOMEM;
			goto Err_1;
		}

		if (search_pattern == NULL) {
			search_pattern = kcalloc(1,
				SEARCH_PATTERN_LEN,
				GFP_KERNEL | GFP_DMA32);

			if (search_pattern == NULL) {
				pr_err("%s: no search_pattern\n", __func__);
				r = -ENOMEM;
				goto Err_1;
			}

			/* build a fake start code to get parser interrupt */
			search_pattern[0] = 0x00;
			search_pattern[1] = 0x00;
			search_pattern[2] = 0x01;
			search_pattern[3] = 0xff;

			search_pattern_map = dma_map_single(
					amports_get_dma_device(),
					search_pattern,
					SEARCH_PATTERN_LEN,
					DMA_TO_DEVICE);
		}

		/* reset PARSER with first esparser_init() call */
		WRITE_RESET_REG(RESET1_REGISTER, RESET_PARSER);

		CLEAR_DEMUX_REG_MASK(TS_HIU_CTL, 1 << USE_HI_BSF_INTERFACE);
		CLEAR_DEMUX_REG_MASK(TS_HIU_CTL_2, 1 << USE_HI_BSF_INTERFACE);
		CLEAR_DEMUX_REG_MASK(TS_HIU_CTL_3, 1 << USE_HI_BSF_INTERFACE);

		CLEAR_DEMUX_REG_MASK(TS_FILE_CONFIG, (1 << TS_HIU_ENABLE));

		WRITE_PARSER_REG(PARSER_CONFIG,
					   (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
					   (1 << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
					   (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

		WRITE_PARSER_REG(PFIFO_RD_PTR, 0);
		WRITE_PARSER_REG(PFIFO_WR_PTR, 0);

		WRITE_PARSER_REG(PARSER_SEARCH_PATTERN, ES_START_CODE_PATTERN);
		WRITE_PARSER_REG(PARSER_SEARCH_MASK, ES_START_CODE_MASK);

		WRITE_PARSER_REG(PARSER_CONFIG,
					   (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
					   (1 << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
					   PS_CFG_STARTCODE_WID_24 |
					   PS_CFG_PFIFO_ACCESS_WID_8 |
					   /* single byte pop */
					   (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

		WRITE_PARSER_REG(PARSER_CONTROL, PARSER_AUTOSEARCH);

	}

	/* hook stream buffer with PARSER */
	if (has_hevc_vdec() && (pts_type == PTS_TYPE_HEVC)) {
		WRITE_PARSER_REG(PARSER_VIDEO_START_PTR, vdec->input.start);
		WRITE_PARSER_REG(PARSER_VIDEO_END_PTR, vdec->input.start
			+ vdec->input.size - 8);

		if (vdec_single(vdec)) {
			CLEAR_PARSER_REG_MASK(PARSER_ES_CONTROL,
				ES_VID_MAN_RD_PTR);

			/* set vififo_vbuf_rp_sel=>hevc */
			WRITE_VREG(DOS_GEN_CTRL0, 3 << 1);

			/* set use_parser_vbuf_wp */
			SET_VREG_MASK(HEVC_STREAM_CONTROL,
					(1 << 3) | (0 << 4));
			/* set stream_fetch_enable */
			SET_VREG_MASK(HEVC_STREAM_CONTROL, 1);

			if (buf->no_parser) {
				/*set endian for non-parser mode */
				SET_VREG_MASK(HEVC_STREAM_CONTROL, 7 << 4);
			}

			/* set stream_buffer_hole with 256 bytes */
			SET_VREG_MASK(HEVC_STREAM_FIFO_CTL, (1 << 29));
		} else {
			SET_PARSER_REG_MASK(PARSER_ES_CONTROL,
					ES_VID_MAN_RD_PTR);
			WRITE_PARSER_REG(PARSER_VIDEO_WP, vdec->input.start);
			WRITE_PARSER_REG(PARSER_VIDEO_RP, vdec->input.start);
		}
		video_data_parsed = 0;
	} else if (pts_type == PTS_TYPE_VIDEO) {
		WRITE_PARSER_REG(PARSER_VIDEO_START_PTR,
			vdec->input.start);
		WRITE_PARSER_REG(PARSER_VIDEO_END_PTR,
			vdec->input.start + vdec->input.size - 8);

		if (vdec_single(vdec) || (vdec_get_debug_flags() & 0x2)) {
			if (vdec_get_debug_flags() & 0x2)
				pr_info("%s %d\n", __func__, __LINE__);
			CLEAR_PARSER_REG_MASK(PARSER_ES_CONTROL,
				ES_VID_MAN_RD_PTR);

			WRITE_VREG(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
			CLEAR_VREG_MASK(VLD_MEM_VIFIFO_BUF_CNTL,
				MEM_BUFCTRL_INIT);

			if (has_hevc_vdec()) {
				/* set vififo_vbuf_rp_sel=>vdec */
				WRITE_VREG(DOS_GEN_CTRL0, 0);
			}
		} else {
			SET_PARSER_REG_MASK(PARSER_ES_CONTROL,
					ES_VID_MAN_RD_PTR);
			WRITE_PARSER_REG(PARSER_VIDEO_WP,
					vdec->input.start);
			WRITE_PARSER_REG(PARSER_VIDEO_RP,
					vdec->input.start);
		}
		video_data_parsed = 0;
	} else if (pts_type == PTS_TYPE_AUDIO) {
		/* set wp as buffer start */
		SET_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL,
			MEM_BUFCTRL_MANUAL);
		WRITE_AIU_REG(AIU_MEM_AIFIFO_MAN_RP,
			READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));
		WRITE_AIU_REG_BITS(AIU_MEM_AIFIFO_CONTROL, 7, 3, 3);
		SET_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL,
			MEM_BUFCTRL_INIT);
		CLEAR_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL,
			MEM_BUFCTRL_INIT);
		WRITE_AIU_REG(AIU_MEM_AIFIFO_MAN_WP,
			READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));
		audio_data_parsed = 0;
		audio_buf_start =
			READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR);
		audio_real_wp = audio_buf_start;
		audio_buf_end = READ_AIU_REG(AIU_MEM_AIFIFO_END_PTR);
	} else if (buf->type == BUF_TYPE_SUBTITLE) {
		WRITE_PARSER_REG(PARSER_SUB_START_PTR,
			parser_sub_start_ptr);
		WRITE_PARSER_REG(PARSER_SUB_END_PTR,
			parser_sub_end_ptr);
		WRITE_PARSER_REG(PARSER_SUB_RP, parser_sub_rp);
		SET_PARSER_REG_MASK(PARSER_ES_CONTROL,
			(7 << ES_SUB_WR_ENDIAN_BIT) |
			ES_SUB_MAN_RD_PTR);
	}

	if (pts_type < PTS_TYPE_MAX) {
		r = pts_start(pts_type);

		if (r < 0) {
			pr_info("esparser_init: pts_start failed\n");
			goto Err_1;
		}
	}

	if (first_use) {
		/*TODO irq */
		r = vdec_request_irq(PARSER_IRQ, esparser_isr,
			"parser", (void *)esparser_id);

		if (r) {
			pr_info("esparser_init: irq register failed.\n");
			goto Err_2;
		}
		VDEC_PRINT_FUN_LINENO(__func__, __LINE__);

		WRITE_PARSER_REG(PARSER_INT_STATUS, 0xffff);
		WRITE_PARSER_REG(PARSER_INT_ENABLE,
					   PARSER_INTSTAT_SC_FOUND <<
					   PARSER_INT_HOST_EN_BIT);
	}
	mutex_unlock(&esparser_mutex);

	if (!(vdec_get_debug_flags() & 1) &&
		!codec_mm_video_tvp_enabled()) {
		int block_size = (buf->type == BUF_TYPE_AUDIO) ?
			PAGE_SIZE : PAGE_SIZE << 4;
		int buf_num = (buf->type == BUF_TYPE_AUDIO) ?
			20 : (2 * SZ_1M)/(PAGE_SIZE << 4);
		if (!(buf->type == BUF_TYPE_SUBTITLE))
			buf->write_thread = threadrw_alloc(buf_num,
				block_size,
				esparser_write_ex,
			(buf->type == BUF_TYPE_AUDIO) ? 1 : 0);
			/*manul mode for audio*/
	}

	return 0;

Err_2:
	pts_stop(pts_type);

Err_1:
	atomic_dec(&esparser_use_count);
	buf->flag &= ~BUF_FLAG_PARSER;
	mutex_unlock(&esparser_mutex);
	return r;
}
EXPORT_SYMBOL(esparser_init);

void esparser_audio_reset_s(struct stream_buf_s *buf)
{
	ulong flags;
	DEFINE_SPINLOCK(lock);

	spin_lock_irqsave(&lock, flags);

	SET_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_MANUAL);
	WRITE_AIU_REG(AIU_MEM_AIFIFO_MAN_RP,
			READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));
	WRITE_AIU_REG_BITS(AIU_MEM_AIFIFO_CONTROL, 7, 3, 3);
	SET_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
	CLEAR_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
	WRITE_AIU_REG(AIU_MEM_AIFIFO_MAN_WP,
			READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));

	buf->flag |= BUF_FLAG_PARSER;

	audio_data_parsed = 0;
	audio_real_wp = READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR);
	audio_buf_start = READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR);
	audio_buf_end = READ_AIU_REG(AIU_MEM_AIFIFO_END_PTR);
	spin_unlock_irqrestore(&lock, flags);

	return;
}

void esparser_audio_reset(struct stream_buf_s *buf)
{
	ulong flags;
	DEFINE_SPINLOCK(lock);

	spin_lock_irqsave(&lock, flags);

	WRITE_PARSER_REG(PARSER_AUDIO_WP,
				   READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));
	WRITE_PARSER_REG(PARSER_AUDIO_RP,
				   READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));

	WRITE_PARSER_REG(PARSER_AUDIO_START_PTR,
				   READ_AIU_REG(AIU_MEM_AIFIFO_START_PTR));
	WRITE_PARSER_REG(PARSER_AUDIO_END_PTR,
				   READ_AIU_REG(AIU_MEM_AIFIFO_END_PTR));
	CLEAR_PARSER_REG_MASK(PARSER_ES_CONTROL, ES_AUD_MAN_RD_PTR);

	WRITE_AIU_REG(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
	CLEAR_AIU_REG_MASK(AIU_MEM_AIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

	buf->flag |= BUF_FLAG_PARSER;

	audio_data_parsed = 0;
	audio_real_wp = 0;
	audio_buf_start = 0;
	audio_buf_end = 0;
	spin_unlock_irqrestore(&lock, flags);

}

void esparser_release(struct stream_buf_s *buf)
{
	u32 pts_type;

	/* check if esparser_init() is ever called */
	if ((buf->flag & BUF_FLAG_PARSER) == 0)
		return;

	if (atomic_read(&esparser_use_count) == 0) {
		pr_info
		("[%s:%d]###warning, esparser has been released already\n",
		 __func__, __LINE__);
		return;
	}
	if (buf->write_thread)
		threadrw_release(buf);
	if (atomic_dec_and_test(&esparser_use_count)) {
		WRITE_PARSER_REG(PARSER_INT_ENABLE, 0);
		/*TODO irq */

		vdec_free_irq(PARSER_IRQ, (void *)esparser_id);

		if (search_pattern) {
			dma_unmap_single(amports_get_dma_device(),
				search_pattern_map,
				SEARCH_PATTERN_LEN, DMA_TO_DEVICE);
			kfree(search_pattern);
			search_pattern = NULL;
		}
	}

	if (has_hevc_vdec() && (buf->type == BUF_TYPE_HEVC))
		pts_type = PTS_TYPE_VIDEO;
	else if (buf->type == BUF_TYPE_VIDEO)
		pts_type = PTS_TYPE_VIDEO;
	else if (buf->type == BUF_TYPE_AUDIO)
		pts_type = PTS_TYPE_AUDIO;
	else if (buf->type == BUF_TYPE_SUBTITLE) {
		buf->flag &= ~BUF_FLAG_PARSER;
		return;
	} else
		return;

	buf->flag &= ~BUF_FLAG_PARSER;
	pts_stop(pts_type);
	stbuf_fetch_release();
}
EXPORT_SYMBOL(esparser_release);

ssize_t drm_write(struct file *file, struct stream_buf_s *stbuf,
				  const char __user *buf, size_t count)
{
	s32 r;
	u32 len;
	u32 realcount, totalcount;
	u32 havewritebytes = 0;
	u32 leftcount = 0;

	struct drm_info tmpmm;
	struct drm_info *drm = &tmpmm;
	u32 res = 0;
	int drm_flag = 0;
	unsigned long realbuf;

	if (buf == NULL || count == 0)
		return -EINVAL;
	if (stbuf->write_thread) {
		r = threadrw_flush_buffers(stbuf);
		if (r < 0)
			pr_info("Warning. drm flush threadrw failed[%d]\n", r);
	}
	res = copy_from_user(drm, buf, sizeof(struct drm_info));
	if (res) {
		pr_info("drm kmalloc failed res[%d]\n", res);
		return -EFAULT;
	}

	if ((drm->drm_flag & TYPE_DRMINFO) && (drm->drm_hasesdata == 0)) {
		if (drm->drm_pktsize > MAX_DRM_PACKAGE_SIZE) {
			pr_err("drm package size is error, size is %u\n", drm->drm_pktsize);
			return -EINVAL;
		}
		/* buf only has drminfo not have esdata; */
		realbuf = drm->drm_phy;
		realcount = drm->drm_pktsize;
		drm_flag = drm->drm_flag;
	} else if (drm->drm_hasesdata == 1) {	/* buf is drminfo+es; */
		if (drm->drm_pktsize > MAX_DRM_PACKAGE_SIZE) {
			pr_err("drm package size is error, size is %u\n", drm->drm_pktsize);
			return -EINVAL;
		}
		realcount = drm->drm_pktsize;
		realbuf = (unsigned long)buf + sizeof(struct drm_info);
		drm_flag = 0;
	} else {		/* buf is hwhead; */
		realcount = count;
		drm_flag = 0;
		realbuf = (unsigned long)buf;
	}

	len = realcount;
	count = realcount;
	totalcount = realcount;
	stbuf->drm_flag = drm_flag;
	stbuf->is_phybuf = drm_flag ? 1 : 0;

	while (len > 0) {
		if (stbuf->type != BUF_TYPE_SUBTITLE
			&& stbuf_space(stbuf) < count) {
			/*should not write partial data in drm mode*/
			r = stbuf_wait_space(stbuf, count);
			if (r < 0)
				return r;
			if (stbuf_space(stbuf) < count)
				return -EAGAIN;
		}
		len = min_t(u32, len, count);

		mutex_lock(&esparser_mutex);

		if (stbuf->type != BUF_TYPE_AUDIO)
			r = _esparser_write((const char __user *)realbuf, len,
					stbuf, drm_flag);
		else
			r = _esparser_write_s((const char __user *)realbuf, len,
					stbuf);
		if (r < 0) {
			pr_info("drm_write _esparser_write failed [%d]\n", r);
			mutex_unlock(&esparser_mutex);
			return r;
		}
		havewritebytes += r;
		leftcount = totalcount - havewritebytes;
		if (havewritebytes == totalcount) {

			mutex_unlock(&esparser_mutex);
			break;	/* write ok; */
		} else if ((len > 0) && (havewritebytes < totalcount)) {
			DRM_PRNT
			("d writebytes[%d] want[%d] total[%d] real[%d]\n",
			 havewritebytes, len, totalcount, realcount);
			len = len - r;	/* write again; */
			realbuf = realbuf + r;
		} else {
			pr_info
			("e writebytes[%d] want[%d] total[%d] real[%d]\n",
			 havewritebytes, len, totalcount, realcount);
		}
		mutex_unlock(&esparser_mutex);
	}

	if ((drm->drm_flag & TYPE_DRMINFO) && (drm->drm_hasesdata == 0)) {
		havewritebytes = sizeof(struct drm_info);
	} else if (drm->drm_hasesdata == 1) {
		havewritebytes += sizeof(struct drm_info);
	}
	return havewritebytes;
}
EXPORT_SYMBOL(drm_write);

/*
 *flags:
 *1:phy
 *2:noblock
 */
ssize_t esparser_write_ex(struct file *file,
			struct stream_buf_s *stbuf,
			const char __user *buf, size_t count,
			int flags)
{

	s32 r;
	u32 len = count;

	if (buf == NULL || count == 0)
		return -EINVAL;

	/*subtitle have no level to check, */
	if (stbuf->type != BUF_TYPE_SUBTITLE && stbuf_space(stbuf) < count) {
		if ((flags & 2) || ((file != NULL) &&
				(file->f_flags & O_NONBLOCK))) {
			len = stbuf_space(stbuf);

			if (len < 256)	/* <1k.do eagain, */
				return -EAGAIN;
		} else {
			len = min(stbuf_canusesize(stbuf) / 8, len);

			if (stbuf_space(stbuf) < len) {
				r = stbuf_wait_space(stbuf, len);
				if (r < 0)
					return r;
			}
		}
	}

	stbuf->last_write_jiffies64 = jiffies_64;

	len = min_t(u32, len, count);

	mutex_lock(&esparser_mutex);

	if (flags & 1)
		stbuf->is_phybuf = true;

	if (stbuf->type == BUF_TYPE_AUDIO)
		r = _esparser_write_s(buf, len, stbuf);
	else
		r = _esparser_write(buf, len, stbuf, flags & 1);

	mutex_unlock(&esparser_mutex);

	return r;
}
ssize_t esparser_write(struct file *file,
			struct stream_buf_s *stbuf,
			const char __user *buf, size_t count)
{
	if (stbuf->write_thread) {
		ssize_t ret;

		ret = threadrw_write(file, stbuf, buf, count);
		if (ret == -EAGAIN) {
			u32 a, b;
			int vdelay, adelay;

			if ((stbuf->type != BUF_TYPE_VIDEO) &&
				(stbuf->type != BUF_TYPE_HEVC))
				return ret;
			if (stbuf->buf_size > (SZ_1M * 30) ||
				(threadrw_buffer_size(stbuf) > SZ_1M * 10) ||
				!threadrw_support_more_buffers(stbuf))
				return ret;
			/*only chang buffer for video.*/
			vdelay = calculation_stream_delayed_ms(
					PTS_TYPE_VIDEO, &a, &b);
			adelay = calculation_stream_delayed_ms(
					PTS_TYPE_AUDIO, &a, &b);
			if ((vdelay > 100 && vdelay < 2000) && /*vdelay valid.*/
				((vdelay < 500) ||/*video delay is short!*/
				(adelay > 0 && adelay < 1000))/*audio is low.*/
				) {
				/*on buffer fulled.
				 *if delay is less than 100ms we think errors,
				 *And we add more buffer on delay < 2s.
				 */
				int new_size = 2 * 1024 * 1024;

				threadrw_alloc_more_buffer_size(
						stbuf, new_size);
			}
		}
		return ret;
	}
	return esparser_write_ex(file, stbuf, buf, count, 0);
}
EXPORT_SYMBOL(esparser_write);

void esparser_sub_reset(void)
{
	ulong flags;
	DEFINE_SPINLOCK(lock);
	u32 parser_sub_start_ptr;
	u32 parser_sub_end_ptr;

	spin_lock_irqsave(&lock, flags);

	parser_sub_start_ptr = READ_PARSER_REG(PARSER_SUB_START_PTR);
	parser_sub_end_ptr = READ_PARSER_REG(PARSER_SUB_END_PTR);

	WRITE_PARSER_REG(PARSER_SUB_START_PTR, parser_sub_start_ptr);
	WRITE_PARSER_REG(PARSER_SUB_END_PTR, parser_sub_end_ptr);
	WRITE_PARSER_REG(PARSER_SUB_RP, parser_sub_start_ptr);
	WRITE_PARSER_REG(PARSER_SUB_WP, parser_sub_start_ptr);
	SET_PARSER_REG_MASK(PARSER_ES_CONTROL,
		(7 << ES_SUB_WR_ENDIAN_BIT) | ES_SUB_MAN_RD_PTR);

	spin_unlock_irqrestore(&lock, flags);
}

static int esparser_stbuf_init(struct stream_buf_s *stbuf,
			       struct vdec_s *vdec)
{
	int ret = -1;

	ret = stbuf_init(stbuf, vdec);
	if (ret)
		goto out;

	ret = esparser_init(stbuf, vdec);
	if (!ret)
		stbuf->flag |= BUF_FLAG_IN_USE;
out:
	return ret;
}

static void esparser_stbuf_release(struct stream_buf_s *stbuf)
{
	esparser_release(stbuf);

	stbuf_release(stbuf);
}

static struct stream_buf_ops esparser_stbuf_ops = {
	.init	= esparser_stbuf_init,
	.release = esparser_stbuf_release,
	.write	= esparser_stbuf_write,
	.get_wp	= parser_get_wp,
	.set_wp	= parser_set_wp,
	.get_rp	= parser_get_rp,
	.set_rp	= parser_set_rp,
};

struct stream_buf_ops *get_esparser_stbuf_ops(void)
{
	return &esparser_stbuf_ops;
}
EXPORT_SYMBOL(get_esparser_stbuf_ops);

