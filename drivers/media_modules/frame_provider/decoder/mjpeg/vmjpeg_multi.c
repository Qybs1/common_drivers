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
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/amlogic/media/frame_sync/ptsserv.h>
#include <linux/amlogic/media/utils/amstream.h>
#include <linux/amlogic/media/canvas/canvas.h>
#include <linux/amlogic/media/vfm/vframe.h>
#include <linux/amlogic/media/vfm/vframe_provider.h>
#include <linux/amlogic/media/vfm/vframe_receiver.h>
#include <linux/amlogic/media/registers/register.h>
#include <linux/amlogic/media/codec_mm/codec_mm.h>
#include <linux/amlogic/media/codec_mm/configs.h>
#include <uapi/linux/tee.h>
#include <media/v4l2-mem2mem.h>

#include "../../../stream_input/amports/amports_priv.h"
#include "../../../common/chips/decoder_cpu_ver_info.h"
#include "../utils/vdec_input.h"
#include "../utils/vdec.h"
#include "../utils/amvdec.h"
#include "../utils/decoder_mmu_box.h"
#include "../utils/decoder_bmmu_box.h"
#include "../utils/firmware.h"
#include "../utils/vdec_v4l2_buffer_ops.h"
#include "../utils/config_parser.h"
#include "../utils/vdec_feature.h"
#include "../../../media_sync/pts_server/pts_server_core.h"
#include "../../decoder/utils/vdec_profile.h"

#define MEM_NAME "codec_mmjpeg"

#define DRIVER_NAME "ammvdec_mjpeg"
#define CHECK_INTERVAL        (HZ/100)

/* protocol register usage
 *    AV_SCRATCH_4 : decode buffer spec
 *    AV_SCRATCH_5 : decode buffer index
 */

#define MREG_DECODE_PARAM   AV_SCRATCH_2	/* bit 0-3: pico_addr_mode */
/* bit 15-4: reference height */
#define MREG_TO_AMRISC      AV_SCRATCH_8
#define MREG_FROM_AMRISC    AV_SCRATCH_9
#define MREG_FRAME_OFFSET   AV_SCRATCH_A
#define DEC_STATUS_REG      AV_SCRATCH_F
#define MREG_PIC_WIDTH      AV_SCRATCH_B
#define MREG_PIC_HEIGHT     AV_SCRATCH_C
#define DECODE_STOP_POS     AV_SCRATCH_K

#define PICINFO_BUF_IDX_MASK        0x0007
#define PICINFO_AVI1                0x0080
#define PICINFO_INTERLACE           0x0020
#define PICINFO_INTERLACE_AVI1_BOT  0x0010
#define PICINFO_INTERLACE_FIRST     0x0010

#define VF_POOL_SIZE          64
#define DECODE_BUFFER_NUM_MAX		16
#define DECODE_BUFFER_NUM_DEF		1
#define MAX_BMMU_BUFFER_NUM		DECODE_BUFFER_NUM_MAX

#define DEFAULT_MEM_SIZE	(32*SZ_1M)
static int debug_enable;
static u32 udebug_flag;
#define DECODE_ID(hw) (hw_to_vdec(hw)->id)

static unsigned int radr;
static unsigned int rval;
static unsigned int max_decode_instance_num = MAX_INSTANCE_MUN;
static unsigned int max_process_time[MAX_INSTANCE_MUN];
static unsigned int decode_timeout_val = 200;
static struct vframe_s *vmjpeg_vf_peek(void *);
static struct vframe_s *vmjpeg_vf_get(void *);
static void vmjpeg_vf_put(struct vframe_s *, void *);
static int vmjpeg_vf_states(struct vframe_states *states, void *);
static int vmjpeg_event_cb(int type, void *data, void *private_data);
static void vmjpeg_work(struct work_struct *work);
static int notify_v4l_eos(struct vdec_s *vdec);
static int pre_decode_buf_level = 0x800;
static int start_decode_buf_level = 0x2000;
static u32 without_display_mode;
static u32 dynamic_buf_num_margin = 6;
#undef pr_info
#define pr_info pr_cont
unsigned int mmjpeg_debug_mask = 0xff;
#define PRINT_FLAG_ERROR              0x0
#define PRINT_FLAG_RUN_FLOW           0X0001
#define PRINT_FLAG_TIMEINFO           0x0002
#define PRINT_FLAG_UCODE_DETAIL		  0x0004
#define PRINT_FLAG_VLD_DETAIL         0x0008
#define PRINT_FLAG_DEC_DETAIL         0x0010
#define PRINT_FLAG_BUFFER_DETAIL      0x0020
#define PRINT_FLAG_RESTORE            0x0040
#define PRINT_FRAME_NUM               0x0080
#define PRINT_FLAG_FORCE_DONE         0x0100
#define PRINT_FRAMEBASE_DATA          0x0400
#define PRINT_FLAG_TIMEOUT_STATUS     0x1000
#define PRINT_FLAG_V4L_DETAIL         0x8000
#define IGNORE_PARAM_FROM_CONFIG      0x8000000

int mmjpeg_debug_print(int index, int debug_flag, const char *fmt, ...)
{
	if (((debug_enable & debug_flag) &&
		((1 << index) & mmjpeg_debug_mask))
		|| (debug_flag == PRINT_FLAG_ERROR)) {
		unsigned char *buf = kzalloc(512, GFP_ATOMIC);
		int len = 0;
		va_list args;

		if (!buf)
			return 0;

		va_start(args, fmt);
		len = sprintf(buf, "%d: ", index);
		vsnprintf(buf + len, 512-len, fmt, args);
		pr_info("%s", buf);
		va_end(args);
		kfree(buf);
	}
	return 0;
}

static const char vmjpeg_dec_id[] = "vmmjpeg-dev";

#define PROVIDER_NAME   "vdec.mjpeg"
static const struct vframe_operations_s vf_provider_ops = {
	.peek = vmjpeg_vf_peek,
	.get = vmjpeg_vf_get,
	.put = vmjpeg_vf_put,
	.event_cb = vmjpeg_event_cb,
	.vf_states = vmjpeg_vf_states,
};

#define DEC_RESULT_NONE             0
#define DEC_RESULT_DONE             1
#define DEC_RESULT_AGAIN            2
#define DEC_RESULT_ERROR            3
#define DEC_RESULT_FORCE_EXIT       4
#define DEC_RESULT_EOS              5
#define DEC_DECODE_TIMEOUT         0x21

/*Send by DEC_STATUS_REG*/
#define MJPEG_CONFIG_REQUEST   1
#define MJPEG_DATA_EMPTY      2

struct buffer_spec_s {
	unsigned int y_addr;
	unsigned int u_addr;
	unsigned int v_addr;

	int y_canvas_index;
	int u_canvas_index;
	int v_canvas_index;

	struct canvas_config_s canvas_config[3];
	unsigned long cma_alloc_addr;
	int cma_alloc_count;
	unsigned int buf_adr;
	ulong v4l_ref_buf_addr;
};

#define spec2canvas(x)  \
	(((x)->v_canvas_index << 16) | \
	 ((x)->u_canvas_index << 8)  | \
	 ((x)->y_canvas_index << 0))

struct vdec_mjpeg_hw_s {
	spinlock_t lock;
	struct mutex vmjpeg_mutex;

	struct platform_device *platform_dev;
	DECLARE_KFIFO(newframe_q, struct vframe_s *, VF_POOL_SIZE);
	DECLARE_KFIFO(display_q, struct vframe_s *, VF_POOL_SIZE);

	struct vframe_s vfpool[VF_POOL_SIZE];
	struct buffer_spec_s buffer_spec[DECODE_BUFFER_NUM_MAX];
	s32 vfbuf_use[DECODE_BUFFER_NUM_MAX];

	u32 frame_width;
	u32 frame_height;
	u32 frame_dur;
	u32 saved_resolution;
	u8 init_flag;
	u32 stat;
	u32 dec_result;
	unsigned long buf_start;
	u32 buf_size;
	void *mm_blk_handle;
	struct dec_sysinfo vmjpeg_amstream_dec_info;

	struct vframe_chunk_s *chunk;
	struct work_struct work;
	void (*vdec_cb)(struct vdec_s *, void *, int);
	void *vdec_cb_arg;
	struct firmware_s *fw;
	struct timer_list check_timer;
	u32 decode_timeout_count;
	ulong start_process_time;
	u32 last_vld_level;
	u8 eos;
	u32 frame_num;
	u32 put_num;
	u32 run_count;
	u32	not_run_ready;
	u32 buffer_not_ready;
	u32	input_empty;
	u32 peek_num;
	u32 get_num;
	bool is_used_v4l;
	void *v4l2_ctx;
	bool v4l_params_parsed;
	int buf_num;
	int dynamic_buf_num_margin;
	int sidebind_type;
	int sidebind_channel_id;
	u32 res_ch_flag;
	u32 canvas_mode;
	u32 canvas_endian;
	ulong fb_token;
	char vdec_name[32];
	char pts_name[32];
	char new_q_name[32];
	char disp_q_name[32];
	bool run_flag;
};

static void reset_process_time(struct vdec_mjpeg_hw_s *hw);
static int notify_v4l_eos(struct vdec_s *vdec);

static void set_frame_info(struct vdec_mjpeg_hw_s *hw, struct vframe_s *vf)
{
	u32 width, height;
	width = READ_VREG(MREG_PIC_WIDTH);
	height = READ_VREG(MREG_PIC_HEIGHT);

	vf->width = hw->frame_width = ((width > 4096) ? 4096 : width);
	vf->height = hw->frame_height = ((height > 2304) ? 2304 : height);

	if (hw->vmjpeg_amstream_dec_info.width < hw->vmjpeg_amstream_dec_info.height) {
		vf->width = hw->frame_width = ((width > 2304) ? 2304 : width);
		vf->height = hw->frame_height = ((height > 4096) ? 4096 : height);
	}
	vf->duration = hw->frame_dur;
	vf->ratio_control = DISP_RATIO_ASPECT_RATIO_MAX << DISP_RATIO_ASPECT_RATIO_BIT;
	vf->sar_width = 1;
	vf->sar_height = 1;
	vf->duration_pulldown = 0;
	vf->flag = 0;

	vf->canvas0Addr = vf->canvas1Addr = -1;
	vf->plane_num = 3;

	vf->canvas0_config[0] = hw->buffer_spec[vf->index].canvas_config[0];
	vf->canvas0_config[1] = hw->buffer_spec[vf->index].canvas_config[1];
	vf->canvas0_config[2] = hw->buffer_spec[vf->index].canvas_config[2];

	vf->canvas1_config[0] = hw->buffer_spec[vf->index].canvas_config[0];
	vf->canvas1_config[1] = hw->buffer_spec[vf->index].canvas_config[1];
	vf->canvas1_config[2] = hw->buffer_spec[vf->index].canvas_config[2];

	vf->sidebind_type = hw->sidebind_type;
	vf->sidebind_channel_id = hw->sidebind_channel_id;
	vf->codec_vfmt = VFORMAT_MJPEG;
}

static irqreturn_t vmjpeg_isr(struct vdec_s *vdec, int irq)
{
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)(vdec->private);

	if (!hw)
		return IRQ_HANDLED;

	if (hw->eos)
		return IRQ_HANDLED;

	WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vmjpeg_isr_thread_fn(struct vdec_s *vdec, int irq)
{
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)(vdec->private);

	u32 reg;
	struct vframe_s *vf = NULL;
	u32 index, offset = 0, pts;
	u64 pts_us64;
	u32 frame_size;
	unsigned int dec_status = READ_VREG(DEC_STATUS_REG);

	if (READ_VREG(AV_SCRATCH_D) != 0 &&
		(debug_enable & PRINT_FLAG_UCODE_DETAIL)) {
		pr_info("dbg%x: %x\n", READ_VREG(AV_SCRATCH_D),
			READ_VREG(AV_SCRATCH_E));
		WRITE_VREG(AV_SCRATCH_D, 0);
		return IRQ_HANDLED;
	}

	if (dec_status == MJPEG_CONFIG_REQUEST) {
		WRITE_VREG(DEC_STATUS_REG, 0);
		return IRQ_HANDLED;
	} else if (dec_status == MJPEG_DATA_EMPTY) {
		/*timeout when decoding next frame*/
		mmjpeg_debug_print(DECODE_ID(hw), PRINT_FRAME_NUM,
			"%s: Insufficient data, lvl=%x ctrl=%x bcnt=%x\n",
			__func__,
			READ_VREG(VLD_MEM_VIFIFO_LEVEL),
			READ_VREG(VLD_MEM_VIFIFO_CONTROL),
			READ_VREG(VIFF_BIT_CNT));

		if (vdec_frame_based(vdec)) {
			hw->dec_result = DEC_RESULT_DONE;
			vdec_schedule_work(&hw->work);
		} else {
			hw->dec_result = DEC_RESULT_AGAIN;
			vdec_schedule_work(&hw->work);
			reset_process_time(hw);
		}
		return IRQ_HANDLED;
	}
	reset_process_time(hw);

	reg = READ_VREG(MREG_FROM_AMRISC);
	index = READ_VREG(AV_SCRATCH_5) & 0xffffff;

	if (index >= hw->buf_num) {
		pr_err("fatal error, invalid buffer index.");
		return IRQ_HANDLED;
	}

	if (kfifo_get(&hw->newframe_q, &vf) == 0) {
		pr_info("fatal error, no available buffer slot.");
		return IRQ_HANDLED;
	}
	vdec_profile(vdec, VDEC_PROFILE_DECODED_FRAME, CORE_MASK_VDEC_1);
	vf->index = index;
	set_frame_info(hw, vf);

	vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;

	if (hw->chunk) {
		vf->pts = hw->chunk->pts;
		vf->pts_us64 = hw->chunk->pts64;
		vf->timestamp = hw->chunk->timestamp;
	} else {
		offset = READ_VREG(MREG_FRAME_OFFSET);
		if (vdec->vbuf.use_ptsserv == SINGLE_PTS_SERVER_DECODER_LOOKUP) {
			if (pts_lookup_offset_us64(PTS_TYPE_VIDEO, offset, &pts,
				&frame_size, 3000, &pts_us64) == 0) {
				vf->pts = pts;
				vf->pts_us64 = pts_us64;
			} else {
				vf->pts = 0;
				vf->pts_us64 = 0;
			}
		}
		if ((vdec->vbuf.use_ptsserv == MULTI_PTS_SERVER_UPPER_LOOKUP) && vdec_stream_based(vdec)) {
			u64 frame_type = KEYFRAME_FLAG;
			vf->pts_us64 = (((u64)vf->duration << 32 | (frame_type << 62)) & 0xffffffff00000000)
				| offset;
			vf->pts = 0;
		}
		if (vdec->vbuf.use_ptsserv == MULTI_PTS_SERVER_DECODER_LOOKUP) {
			u64 frame_type = KEYFRAME_FLAG;
			checkout_pts_offset pts_info;
			pts_info.offset = (((u64)vf->duration << 32 | (frame_type << 62)) & 0xffffffff00000000)
				| offset;
			if (!ptsserver_checkout_pts_offset((vdec->pts_server_id & 0xff), &pts_info)) {
				vf->pts = pts_info.pts;
				vf->pts_us64 = pts_info.pts_64;
			} else {
				vf->pts = 0;
				vf->pts_us64 = 0;
			}
		}
	}
	vf->orientation = 0;
	hw->vfbuf_use[index]++;

	vf->mem_handle =
		decoder_bmmu_box_get_mem_handle(hw->mm_blk_handle, index);
	decoder_do_frame_check(vdec, vf);
	vdec_vframe_ready(vdec, vf);
	kfifo_put(&hw->display_q, (const struct vframe_s *)vf);
	ATRACE_COUNTER(hw->pts_name, vf->pts);
	ATRACE_COUNTER(hw->new_q_name, kfifo_len(&hw->newframe_q));
	ATRACE_COUNTER(hw->disp_q_name, kfifo_len(&hw->display_q));
	hw->frame_num++;
	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FRAME_NUM,
		"%s:frame num:%d,pts=%d,pts64=%lld(0x%llx). dur=%d\n",
		__func__, hw->frame_num,
		vf->pts, vf->pts_us64, vf->pts_us64, vf->duration);
	vdec->vdec_fps_detec(vdec->id);
	if (without_display_mode == 0) {
		vf_notify_receiver(vdec->vf_provider_name,
				VFRAME_EVENT_PROVIDER_VFRAME_READY,
				NULL);
	} else
		vmjpeg_vf_put(vmjpeg_vf_get(vdec), vdec);

	hw->dec_result = DEC_RESULT_DONE;

	vdec_schedule_work(&hw->work);

	return IRQ_HANDLED;
}

static int valid_vf_check(struct vframe_s *vf, struct vdec_mjpeg_hw_s *hw)
{
	int i;

	if (!vf || (vf->index == -1))
		return 0;

	for (i = 0; i < VF_POOL_SIZE; i++) {
		if (vf == &hw->vfpool[i])
			return 1;
	}

	return 0;
}

static struct vframe_s *vmjpeg_vf_peek(void *op_arg)
{
	struct vframe_s *vf;
	struct vdec_s *vdec = op_arg;
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	if (!hw)
		return NULL;
	hw->peek_num++;

	if (kfifo_len(&hw->display_q) > VF_POOL_SIZE) {
		mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_RUN_FLOW,
			"kfifo len:%d invalid, peek error\n",
			kfifo_len(&hw->display_q));
		return NULL;
	}

	if (kfifo_peek(&hw->display_q, &vf))
		return vf;

	return NULL;
}

static struct vframe_s *vmjpeg_vf_get(void *op_arg)
{
	struct vframe_s *vf;
	struct vdec_s *vdec = op_arg;
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	if (!hw)
		return NULL;
	hw->get_num++;
	if (kfifo_get(&hw->display_q, &vf)) {
		ATRACE_COUNTER(hw->disp_q_name, kfifo_len(&hw->display_q));

		vf->vf_ud_param.magic_code = UD_MAGIC_CODE;
		vf->vf_ud_param.ud_param.buf_len = 0;
		vf->vf_ud_param.ud_param.pbuf_addr = NULL;
		vf->vf_ud_param.ud_param.instance_id = vdec->afd_video_id;

		vf->vf_ud_param.ud_param.meta_info.duration = vf->duration;
		vf->vf_ud_param.ud_param.meta_info.flags = (VFORMAT_MJPEG << 3);
		vf->vf_ud_param.ud_param.meta_info.vpts = vf->pts;
		if (vf->pts)
			vf->vf_ud_param.ud_param.meta_info.vpts_valid = 1;

		return vf;
	}
	return NULL;
}

static void vmjpeg_vf_put(struct vframe_s *vf, void *op_arg)
{
	struct vdec_s *vdec = op_arg;
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	if (!valid_vf_check(vf, hw)) {
		mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_ERROR,
			"invalid vf: %lx\n", (ulong)vf);
		return ;
	}

	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FRAME_NUM,
		"%s:put_num:%d\n", __func__, hw->put_num);
	hw->vfbuf_use[vf->index]--;
	kfifo_put(&hw->newframe_q, (const struct vframe_s *)vf);
	ATRACE_COUNTER(hw->new_q_name, kfifo_len(&hw->newframe_q));
	hw->put_num++;
	vdec_up(vdec);
}

static int vmjpeg_event_cb(int type, void *data, void *op_arg)
{
	struct vdec_s *vdec = op_arg;

	if (type & VFRAME_EVENT_RECEIVER_REQ_STATE) {
		struct provider_state_req_s *req =
			(struct provider_state_req_s *)data;
		if (req->req_type == REQ_STATE_SECURE)
			req->req_result[0] = vdec_secure(vdec);
		else
			req->req_result[0] = 0xffffffff;
	}

	return 0;
}

static int vmjpeg_vf_states(struct vframe_states *states, void *op_arg)
{
	unsigned long flags;
	struct vdec_s *vdec = op_arg;
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	spin_lock_irqsave(&hw->lock, flags);

	states->vf_pool_size = VF_POOL_SIZE;
	states->buf_free_num = kfifo_len(&hw->newframe_q);
	states->buf_avail_num = kfifo_len(&hw->display_q);
	states->buf_recycle_num = 0;

	spin_unlock_irqrestore(&hw->lock, flags);

	return 0;
}

static int vmjpeg_dec_status(struct vdec_s *vdec, struct vdec_info *vstatus)
{
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	if (!hw)
		return -1;

	vstatus->frame_width = hw->frame_width;
	vstatus->frame_height = hw->frame_height;
	if (0 != hw->frame_dur)
		vstatus->frame_rate = 96000 / hw->frame_dur;
	else
		vstatus->frame_rate = 96000;
	vstatus->error_count = 0;
	vstatus->status = hw->stat;

	return 0;
}

/****************************************/
static void vmjpeg_canvas_init(struct vdec_mjpeg_hw_s *hw)
{
	int i, ret;
	u32 canvas_width, canvas_height;
	u32 decbuf_size, decbuf_y_size, decbuf_uv_size;
	unsigned long buf_start, addr;
	u32 endian;
	struct vdec_s *vdec = hw_to_vdec(hw);

	endian = (vdec->canvas_mode == CANVAS_BLKMODE_LINEAR) ? 7 : 0;
	canvas_width = 4096;
	canvas_height = 2304;
	if (hw->vmjpeg_amstream_dec_info.width > 0
		&& hw->vmjpeg_amstream_dec_info.height > 0
		&& (hw->vmjpeg_amstream_dec_info.width < hw->vmjpeg_amstream_dec_info.height)) {
		canvas_width = 2304;
		canvas_height = 4096;
	}
	decbuf_y_size  = ALIGN(canvas_width * canvas_height, SZ_64K);        //0x900000
	decbuf_uv_size = ALIGN(canvas_width * canvas_height * 1/4, SZ_64K);  //0x240000
	decbuf_size    = ALIGN(canvas_width * canvas_height * 3/2, SZ_64K);  //0xd80000

	for (i = 0; i < hw->buf_num; i++) {
		int canvas;

		ret = decoder_bmmu_box_alloc_buf_phy(hw->mm_blk_handle, i,
				decbuf_size, DRIVER_NAME, &buf_start);
		if (ret < 0) {
			pr_err("CMA alloc failed! size 0x%d  idx %d\n",
				decbuf_size, i);
			return;
		}
		if (!vdec_secure(vdec))
			codec_mm_memset(buf_start, 0, decbuf_size);

		hw->buffer_spec[i].buf_adr = buf_start;
		addr = hw->buffer_spec[i].buf_adr;

		hw->buffer_spec[i].y_addr = addr;
		addr += decbuf_y_size;
		hw->buffer_spec[i].u_addr = addr;
		addr += decbuf_uv_size;
		hw->buffer_spec[i].v_addr = addr;

		if (vdec->parallel_dec == 1) {
			if (hw->buffer_spec[i].y_canvas_index == -1)
				hw->buffer_spec[i].y_canvas_index = vdec->get_canvas_ex(CORE_MASK_VDEC_1, vdec->id);
			if (hw->buffer_spec[i].u_canvas_index == -1)
				hw->buffer_spec[i].u_canvas_index = vdec->get_canvas_ex(CORE_MASK_VDEC_1, vdec->id);
			if (hw->buffer_spec[i].v_canvas_index == -1)
				hw->buffer_spec[i].v_canvas_index = vdec->get_canvas_ex(CORE_MASK_VDEC_1, vdec->id);
		} else {
			canvas = vdec->get_canvas(i, 3);
			hw->buffer_spec[i].y_canvas_index = canvas_y(canvas);
			hw->buffer_spec[i].u_canvas_index = canvas_u(canvas);
			hw->buffer_spec[i].v_canvas_index = canvas_v(canvas);
		}

		config_cav_lut_ex(hw->buffer_spec[i].y_canvas_index,
			hw->buffer_spec[i].y_addr,
			canvas_width,
			canvas_height,
			CANVAS_ADDR_NOWRAP,
			CANVAS_BLKMODE_LINEAR, endian, VDEC_1);
		hw->buffer_spec[i].canvas_config[0].phy_addr =
			hw->buffer_spec[i].y_addr;
		hw->buffer_spec[i].canvas_config[0].width =
			canvas_width;
		hw->buffer_spec[i].canvas_config[0].height =
			canvas_height;
		hw->buffer_spec[i].canvas_config[0].block_mode =
			CANVAS_BLKMODE_LINEAR;
		hw->buffer_spec[i].canvas_config[0].endian =
			endian;

		config_cav_lut_ex(hw->buffer_spec[i].u_canvas_index,
			hw->buffer_spec[i].u_addr,
			canvas_width / 2,
			canvas_height / 2,
			CANVAS_ADDR_NOWRAP,
			CANVAS_BLKMODE_LINEAR, endian, VDEC_1);
		hw->buffer_spec[i].canvas_config[1].phy_addr =
			hw->buffer_spec[i].u_addr;
		hw->buffer_spec[i].canvas_config[1].width =
			canvas_width / 2;
		hw->buffer_spec[i].canvas_config[1].height =
			canvas_height / 2;
		hw->buffer_spec[i].canvas_config[1].block_mode =
			CANVAS_BLKMODE_LINEAR;
		hw->buffer_spec[i].canvas_config[1].endian =
			endian;

		config_cav_lut_ex(hw->buffer_spec[i].v_canvas_index,
			hw->buffer_spec[i].v_addr,
			canvas_width / 2,
			canvas_height / 2,
			CANVAS_ADDR_NOWRAP,
			CANVAS_BLKMODE_LINEAR, endian, VDEC_1);
		hw->buffer_spec[i].canvas_config[2].phy_addr =
			hw->buffer_spec[i].v_addr;
		hw->buffer_spec[i].canvas_config[2].width =
			canvas_width / 2;
		hw->buffer_spec[i].canvas_config[2].height =
			canvas_height / 2;
		hw->buffer_spec[i].canvas_config[2].block_mode =
			CANVAS_BLKMODE_LINEAR;
		hw->buffer_spec[i].canvas_config[2].endian =
			endian;
	}
}

static void init_scaler(u32 endian)
{
	/* 4 point triangle */
	const unsigned int filt_coef[] = {
		0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
		0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
		0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
		0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
		0x18382808, 0x18382808, 0x17372909, 0x17372909,
		0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
		0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
		0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
		0x10303010
	};
	int i;

	/* pscale enable, PSCALE cbus bmem enable */
	WRITE_VREG(PSCALE_CTRL, 0xc000);

	/* write filter coefs */
	WRITE_VREG(PSCALE_BMEM_ADDR, 0);
	for (i = 0; i < 33; i++) {
		WRITE_VREG(PSCALE_BMEM_DAT, 0);
		WRITE_VREG(PSCALE_BMEM_DAT, filt_coef[i]);
	}

	/* Y horizontal initial info */
	WRITE_VREG(PSCALE_BMEM_ADDR, 37 * 2);
	/* [35]: buf repeat pix0,
	 * [34:29] => buf receive num,
	 * [28:16] => buf blk x,
	 * [15:0] => buf phase
	 */
	WRITE_VREG(PSCALE_BMEM_DAT, 0x0008);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x60000000);

	/* C horizontal initial info */
	WRITE_VREG(PSCALE_BMEM_ADDR, 41 * 2);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x0008);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x60000000);

	/* Y vertical initial info */
	WRITE_VREG(PSCALE_BMEM_ADDR, 39 * 2);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x0008);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x60000000);

	/* C vertical initial info */
	WRITE_VREG(PSCALE_BMEM_ADDR, 43 * 2);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x0008);
	WRITE_VREG(PSCALE_BMEM_DAT, 0x60000000);

	/* Y horizontal phase step */
	WRITE_VREG(PSCALE_BMEM_ADDR, 36 * 2 + 1);
	/* [19:0] => Y horizontal phase step */
	WRITE_VREG(PSCALE_BMEM_DAT, 0x10000);
	/* C horizontal phase step */
	WRITE_VREG(PSCALE_BMEM_ADDR, 40 * 2 + 1);
	/* [19:0] => C horizontal phase step */
	WRITE_VREG(PSCALE_BMEM_DAT, 0x10000);

	/* Y vertical phase step */
	WRITE_VREG(PSCALE_BMEM_ADDR, 38 * 2 + 1);
	/* [19:0] => Y vertical phase step */
	WRITE_VREG(PSCALE_BMEM_DAT, 0x10000);
	/* C vertical phase step */
	WRITE_VREG(PSCALE_BMEM_ADDR, 42 * 2 + 1);
	/* [19:0] => C horizontal phase step */
	WRITE_VREG(PSCALE_BMEM_DAT, 0x10000);

	/* reset pscaler */
	WRITE_VREG(DOS_SW_RESET0, (1 << 10));
	WRITE_VREG(DOS_SW_RESET0, 0);

	if (is_mjpeg_endian_rematch()) {
		if (endian == 7)
			WRITE_VREG(PSCALE_CTRL2, (0x1ff << 16) | READ_VREG(PSCALE_CTRL2));
		else
			WRITE_VREG(PSCALE_CTRL2, (0 << 16) | READ_VREG(PSCALE_CTRL2));
	}

	if (get_cpu_major_id() < AM_MESON_CPU_MAJOR_ID_SC2) {
		READ_RESET_REG(RESET2_REGISTER);
		READ_RESET_REG(RESET2_REGISTER);
		READ_RESET_REG(RESET2_REGISTER);
	}
	WRITE_VREG(PSCALE_RST, 0x7);
	WRITE_VREG(PSCALE_RST, 0x0);
}

static void vmjpeg_dump_state(struct vdec_s *vdec)
{
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)(vdec->private);
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"====== %s\n", __func__);
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"width/height (%d/%d) buf_num %d\n",
		hw->frame_width,
		hw->frame_height,
		hw->buf_num);
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"is_framebase(%d), eos %d, state 0x%x, dec_result 0x%x"
		"dec_frm %d put_frm %d run %d not_run_ready %d input_empty %d run_flag %d\n",
		input_frame_based(vdec),
		hw->eos,
		hw->stat,
		hw->dec_result,
		hw->frame_num,
		hw->put_num,
		hw->run_count,
		hw->not_run_ready,
		hw->input_empty,
		hw->run_flag
		);
	if (vf_get_receiver(vdec->vf_provider_name)) {
		enum receiver_start_e state =
		vf_notify_receiver(vdec->vf_provider_name,
			VFRAME_EVENT_PROVIDER_QUREY_STATE,
			NULL);
		mmjpeg_debug_print(DECODE_ID(hw), 0,
			"\nreceiver(%s) state %d\n",
			vdec->vf_provider_name,
			state);
	}
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"%s, newq(%d/%d), dispq(%d/%d) vf peek/get/put (%d/%d/%d)\n",
		__func__,
		kfifo_len(&hw->newframe_q),
		VF_POOL_SIZE,
		kfifo_len(&hw->display_q),
		VF_POOL_SIZE,
		hw->peek_num,
		hw->get_num,
		hw->put_num);
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"VIFF_BIT_CNT=0x%x\n",
		READ_VREG(VIFF_BIT_CNT));
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"VLD_MEM_VIFIFO_LEVEL=0x%x\n",
		READ_VREG(VLD_MEM_VIFIFO_LEVEL));
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"VLD_MEM_VIFIFO_WP=0x%x\n",
		READ_VREG(VLD_MEM_VIFIFO_WP));
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"VLD_MEM_VIFIFO_RP=0x%x\n",
		READ_VREG(VLD_MEM_VIFIFO_RP));
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"PARSER_VIDEO_RP=0x%x\n",
		STBUF_READ(&vdec->vbuf, get_rp));
	mmjpeg_debug_print(DECODE_ID(hw), 0,
		"PARSER_VIDEO_WP=0x%x\n",
		STBUF_READ(&vdec->vbuf, get_wp));
	if (input_frame_based(vdec) &&
		debug_enable & PRINT_FRAMEBASE_DATA) {
		int jj;
		if (hw->chunk && hw->chunk->block &&
			hw->chunk->size > 0) {
			u8 *data = NULL;

			if (!hw->chunk->block->is_mapped)
				data = codec_mm_vmap(hw->chunk->block->start +
					hw->chunk->offset, hw->chunk->size);
			else
				data = ((u8 *)hw->chunk->block->start_virt) +
					hw->chunk->offset;

			mmjpeg_debug_print(DECODE_ID(hw), 0,
				"frame data size 0x%x\n",
				hw->chunk->size);
			for (jj = 0; jj < hw->chunk->size; jj++) {
				if ((jj & 0xf) == 0)
					mmjpeg_debug_print(DECODE_ID(hw),
						PRINT_FRAMEBASE_DATA, "%06x:", jj);
				mmjpeg_debug_print(DECODE_ID(hw),
					PRINT_FRAMEBASE_DATA, "%02x ", data[jj]);
				if (((jj + 1) & 0xf) == 0)
					mmjpeg_debug_print(DECODE_ID(hw),
						PRINT_FRAMEBASE_DATA, "\n");
			}

			if (!hw->chunk->block->is_mapped)
				codec_mm_unmap_phyaddr(data);
		}
	}
}
static void reset_process_time(struct vdec_mjpeg_hw_s *hw)
{
	if (hw->start_process_time) {
		unsigned process_time =
			1000 * (jiffies - hw->start_process_time) / HZ;
		hw->start_process_time = 0;
		if (process_time > max_process_time[DECODE_ID(hw)])
			max_process_time[DECODE_ID(hw)] = process_time;
	}
}

static void start_process_time(struct vdec_mjpeg_hw_s *hw)
{
	hw->decode_timeout_count = 2;
	hw->start_process_time = jiffies;
}

static void timeout_process(struct vdec_mjpeg_hw_s *hw)
{
	amvdec_stop();
	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_ERROR,
		"%s decoder timeout\n", __func__);
	hw->dec_result = DEC_RESULT_DONE;
	reset_process_time(hw);
	vdec_schedule_work(&hw->work);
}

static void check_timer_func(struct timer_list *timer)
{
	struct vdec_mjpeg_hw_s *hw = container_of(timer,
		struct vdec_mjpeg_hw_s, check_timer);
	struct vdec_s *vdec = hw_to_vdec(hw);
	int timeout_val = decode_timeout_val;

	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_VLD_DETAIL,
		"%s: status:nstatus=%d:%d\n",
		__func__, vdec->status, vdec->next_status);
	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_VLD_DETAIL,
		"%s: %d,buftl=%x:%x:%x:%x\n",
		__func__, __LINE__,
		READ_VREG(VLD_MEM_VIFIFO_BUF_CNTL),
		STBUF_READ(&vdec->vbuf, get_wp),
		READ_VREG(VLD_MEM_VIFIFO_LEVEL),
		READ_VREG(VLD_MEM_VIFIFO_WP));

	if (radr != 0) {
		if (rval != 0) {
			WRITE_VREG(radr, rval);
			pr_info("WRITE_VREG(%x,%x)\n", radr, rval);
		} else
			pr_info("READ_VREG(%x)=%x\n", radr, READ_VREG(radr));
		rval = 0;
		radr = 0;
	}

	if (((debug_enable & PRINT_FLAG_TIMEOUT_STATUS) == 0) &&
		(timeout_val > 0) &&
		(hw->start_process_time > 0) &&
		((1000 * (jiffies - hw->start_process_time) / HZ) > timeout_val)) {
		if (hw->last_vld_level == READ_VREG(VLD_MEM_VIFIFO_LEVEL)) {
			if (hw->decode_timeout_count > 0)
				hw->decode_timeout_count--;
			if (hw->decode_timeout_count == 0)
				timeout_process(hw);
		}
		hw->last_vld_level = READ_VREG(VLD_MEM_VIFIFO_LEVEL);
	}

	if (READ_VREG(DEC_STATUS_REG) == DEC_DECODE_TIMEOUT) {
		pr_info("ucode DEC_DECODE_TIMEOUT\n");
		if (hw->decode_timeout_count > 0)
			hw->decode_timeout_count--;
		if (hw->decode_timeout_count == 0)
			timeout_process(hw);
		WRITE_VREG(DEC_STATUS_REG, 0);
	}

	mod_timer(&hw->check_timer, jiffies + CHECK_INTERVAL);
}

static int get_dynamic_buf_num_margin(struct vdec_mjpeg_hw_s *hw)
{
	return((dynamic_buf_num_margin & 0x80000000) == 0) ?
		hw->dynamic_buf_num_margin :
		(dynamic_buf_num_margin & 0x7fffffff);
}

static int vmjpeg_get_buf_num(struct vdec_mjpeg_hw_s *hw)
{
	int buf_num = DECODE_BUFFER_NUM_DEF;

	buf_num += hw->dynamic_buf_num_margin;

	if (buf_num > DECODE_BUFFER_NUM_MAX)
		buf_num = DECODE_BUFFER_NUM_MAX;

	return buf_num;
}

static bool is_enough_free_buffer(struct vdec_mjpeg_hw_s *hw)
{
	int i;

	for (i = 0; i < hw->buf_num; i++) {
		if (hw->vfbuf_use[i] == 0)
			break;
	}

	return i == hw->buf_num ? false : true;
}

static int find_free_buffer(struct vdec_mjpeg_hw_s *hw)
{
	int i;

	for (i = 0; i < hw->buf_num; i++) {
		if (hw->vfbuf_use[i] == 0)
			break;
	}

	if (i == hw->buf_num)
		return -1;

	return i;
}

static int vmjpeg_hw_ctx_restore(struct vdec_mjpeg_hw_s *hw)
{
	struct buffer_spec_s *buff_spec;
	int index, i;
	u32 endian;

	index = find_free_buffer(hw);
	if (index < 0)
		return -1;

	WRITE_VREG(DOS_SW_RESET0, (1 << 7) | (1 << 6));
	WRITE_VREG(DOS_SW_RESET0, 0);

	if (!hw->init_flag) {
		vmjpeg_canvas_init(hw);
	} else {
		for (i = 0; i < hw->buf_num; i++) {
			buff_spec = &hw->buffer_spec[i];
			config_cav_lut(buff_spec->y_canvas_index,
						&buff_spec->canvas_config[0], VDEC_1);
			config_cav_lut(buff_spec->u_canvas_index,
						&buff_spec->canvas_config[1], VDEC_1);
			config_cav_lut(buff_spec->v_canvas_index,
						&buff_spec->canvas_config[2], VDEC_1);
		}
	}

	/* find next decode buffer index */
	WRITE_VREG(AV_SCRATCH_4, spec2canvas(&hw->buffer_spec[index]));
	WRITE_VREG(AV_SCRATCH_5, index | 1 << 24);

	endian = (hw_to_vdec(hw)->canvas_mode == CANVAS_BLKMODE_LINEAR) ? 7 : 0;
	init_scaler(endian);

	/* clear buffer IN/OUT registers */
	WRITE_VREG(MREG_TO_AMRISC, 0);
	WRITE_VREG(MREG_FROM_AMRISC, 0);

	WRITE_VREG(MCPU_INTR_MSK, 0xffff);
	WRITE_VREG(MREG_DECODE_PARAM, (hw->frame_height << 4) | 0x8000);

	/* clear mailbox interrupt */
	WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	WRITE_VREG(ASSIST_MBOX1_MASK, 1);
	/* set interrupt mapping for vld */
	WRITE_VREG(ASSIST_AMR1_INT8, 8);

	CLEAR_VREG_MASK(MDEC_PIC_DC_CTRL, 1 << 17);

	return 0;
}

static s32 vmjpeg_init(struct vdec_s *vdec)
{
	int i;
	int size = -1, fw_size = 0x1000 * 16;
	struct firmware_s *fw = NULL;
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)vdec->private;

	fw = fw_firmare_s_creat(fw_size);
	if (IS_ERR_OR_NULL(fw))
		return -ENOMEM;

	size = get_firmware_data(VIDEO_DEC_MJPEG_MULTI, fw->data);
	if (size < 0) {
		pr_err("get firmware fail.");
		vfree(fw);
		return -1;
	}

	fw->len = size;
	hw->fw = fw;

	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_RUN_FLOW,
		"%s, hw->vmjpeg_amstream_dec_info.width:%d, hw->vmjpeg_amstream_dec_info.height:%d\n",
		__func__, hw->vmjpeg_amstream_dec_info.width, hw->vmjpeg_amstream_dec_info.height);
	hw->frame_width = hw->vmjpeg_amstream_dec_info.width;
	hw->frame_height = hw->vmjpeg_amstream_dec_info.height;

	hw->frame_dur = ((hw->vmjpeg_amstream_dec_info.rate) ?
	hw->vmjpeg_amstream_dec_info.rate : 3840);
	hw->saved_resolution = 0;
	hw->eos = 0;
	hw->init_flag = 0;
	hw->frame_num = 0;
	hw->put_num = 0;
	hw->run_count = 0;
	hw->not_run_ready = 0;
	hw->input_empty = 0;
	hw->peek_num = 0;
	hw->get_num = 0;
	for (i = 0; i < DECODE_BUFFER_NUM_MAX; i++)
		hw->vfbuf_use[i] = 0;

	INIT_KFIFO(hw->display_q);
	INIT_KFIFO(hw->newframe_q);

	for (i = 0; i < VF_POOL_SIZE; i++) {
		const struct vframe_s *vf = &hw->vfpool[i];

		hw->vfpool[i].index = -1;
		kfifo_put(&hw->newframe_q, vf);
	}

	if (hw->mm_blk_handle) {
		decoder_bmmu_box_free(hw->mm_blk_handle);
		hw->mm_blk_handle = NULL;
	}

	hw->mm_blk_handle = decoder_bmmu_box_alloc_box(
		DRIVER_NAME,
		0,
		MAX_BMMU_BUFFER_NUM,
		4 + PAGE_SHIFT,
		CODEC_MM_FLAGS_CMA_CLEAR |
		CODEC_MM_FLAGS_FOR_VDECODER,
		BMMU_ALLOC_FLAGS_WAITCLEAR);

	timer_setup(&hw->check_timer, check_timer_func, 0);
	hw->check_timer.expires = jiffies + CHECK_INTERVAL;
	/*add_timer(&hw->check_timer);*/
	hw->stat |= STAT_TIMER_ARM;
	hw->stat |= STAT_ISR_REG;

	WRITE_VREG(DECODE_STOP_POS, udebug_flag);
	INIT_WORK(&hw->work, vmjpeg_work);
	pr_info("w:h=%d:%d\n", hw->frame_width, hw->frame_height);
	return 0;
}

static unsigned long run_ready(struct vdec_s *vdec,
	unsigned long mask)
{
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)vdec->private;
	hw->not_run_ready++;
	if (hw->eos)
		return 0;
	if (vdec_stream_based(vdec) && (hw->init_flag == 0)
		&& pre_decode_buf_level != 0) {
		u32 rp, wp, level;

		rp = STBUF_READ(&vdec->vbuf, get_rp);
		wp = STBUF_READ(&vdec->vbuf, get_wp);
		if (wp < rp)
			level = vdec->input.size + wp - rp;
		else
			level = wp - rp;

		if (level < pre_decode_buf_level)
			return PRE_LEVEL_NOT_ENOUGH;
	}

	if (!is_enough_free_buffer(hw)) {
		hw->buffer_not_ready++;
		return 0;
	}

	hw->not_run_ready = 0;
	hw->buffer_not_ready = 0;
	if (vdec->parallel_dec == 1)
		return CORE_MASK_VDEC_1;
	else
		return CORE_MASK_VDEC_1 | CORE_MASK_HEVC;
}

static void run(struct vdec_s *vdec, unsigned long mask,
	void (*callback)(struct vdec_s *, void *, int), void *arg)
{
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)vdec->private;
	int i, ret;

	hw->run_flag = 1;
	hw->vdec_cb_arg = arg;
	hw->vdec_cb = callback;

	hw->run_count++;
	vdec_reset_core(vdec);
	for (i = 0; i < hw->buf_num; i++) {
		if (hw->vfbuf_use[i] == 0)
			break;
	}

	if (i == hw->buf_num) {
		hw->dec_result = DEC_RESULT_AGAIN;
		vdec_schedule_work(&hw->work);
		hw->run_flag = 0;
		return;
	}

	ret = vdec_prepare_input(vdec, &hw->chunk);
	if (ret <= 0) {
		hw->input_empty++;
		mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_RUN_FLOW,
			"%s: %d,r=%d,buftl=%x:%x:%x\n",
			__func__, __LINE__, ret,
			READ_VREG(VLD_MEM_VIFIFO_BUF_CNTL),
			STBUF_READ(&vdec->vbuf, get_rp),
			READ_VREG(VLD_MEM_VIFIFO_WP));

		hw->dec_result = DEC_RESULT_AGAIN;
		vdec_schedule_work(&hw->work);
		hw->run_flag = 0;
		return;
	}
	hw->input_empty = 0;
	hw->dec_result = DEC_RESULT_NONE;
	if (vdec->mc_loaded) {
	/*firmware have load before,
	  and not changes to another.
	  ignore reload.
	*/
	} else {
		ret = amvdec_vdec_loadmc_ex(VFORMAT_MJPEG, "mmjpeg", vdec, hw->fw->data);
		if (ret < 0) {
			pr_err("[%d] MMJPEG: the %s fw loading failed, err: %x\n",
				vdec->id, fw_tee_enabled() ? "TEE" : "local", ret);
			hw->dec_result = DEC_RESULT_FORCE_EXIT;
			vdec_schedule_work(&hw->work);
			hw->run_flag = 0;
			return;
		}
		vdec->mc_loaded = 1;
		vdec->mc_type = VFORMAT_MJPEG;
	}

	if (vmjpeg_hw_ctx_restore(hw) < 0) {
		hw->dec_result = DEC_RESULT_ERROR;
		mmjpeg_debug_print(DECODE_ID(hw), 0,
			"amvdec_mmjpeg: error HW context restore\n");
		vdec_schedule_work(&hw->work);
		hw->run_flag = 0;
		return;
	}

	hw->stat |= STAT_MC_LOAD;
	start_process_time(hw);
	hw->last_vld_level = 0;
	mod_timer(&hw->check_timer, jiffies + CHECK_INTERVAL);
	amvdec_start();
	vdec_enable_input(vdec);
	hw->stat |= STAT_VDEC_RUN;
	hw->init_flag = 1;

	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_RUN_FLOW,
		"%s (0x%x 0x%x 0x%x) vldcrl 0x%x bitcnt 0x%x powerctl 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__,
		READ_VREG(VLD_MEM_VIFIFO_LEVEL),
		READ_VREG(VLD_MEM_VIFIFO_WP),
		READ_VREG(VLD_MEM_VIFIFO_RP),
		READ_VREG(VLD_DECODE_CONTROL),
		READ_VREG(VIFF_BIT_CNT),
		READ_VREG(POWER_CTL_VLD),
		READ_VREG(VLD_MEM_VIFIFO_START_PTR),
		READ_VREG(VLD_MEM_VIFIFO_CURR_PTR),
		READ_VREG(VLD_MEM_VIFIFO_CONTROL),
		READ_VREG(VLD_MEM_VIFIFO_BUF_CNTL),
		READ_VREG(VLD_MEM_VIFIFO_END_PTR));
	hw->run_flag = 0;
}
static void wait_vmjpeg_search_done(struct vdec_mjpeg_hw_s *hw)
{
	u32 vld_rp = READ_VREG(VLD_MEM_VIFIFO_RP);
	int count = 0;

	do {
		usleep_range(100, 500);
		if (vld_rp == READ_VREG(VLD_MEM_VIFIFO_RP))
			break;
		if (count > 1000) {
			mmjpeg_debug_print(DECODE_ID(hw), 0,
				"%s, count %d  vld_rp 0x%x VLD_MEM_VIFIFO_RP 0x%x\n",
				__func__, count, vld_rp, READ_VREG(VLD_MEM_VIFIFO_RP));
			break;
		} else
			vld_rp = READ_VREG(VLD_MEM_VIFIFO_RP);
		count++;
	} while (1);
}

static int notify_v4l_eos(struct vdec_s *vdec)
{
	struct vdec_mjpeg_hw_s *hw = (struct vdec_mjpeg_hw_s *)vdec->private;

	struct vframe_s *vf = NULL;
	struct aml_buf *aml_buf = NULL;
	int index = -1;

	if (hw->eos) {
		if (kfifo_get(&hw->newframe_q, &vf) == 0 || vf == NULL) {
			mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_ERROR,
				"%s fatal error, no available buffer slot.\n",
				__func__);
			return -1;
		}

		vf->type |= VIDTYPE_V4L_EOS;
		vf->timestamp = ULONG_MAX;
		vf->v4l_mem_handle = (index == -1) ? (ulong)aml_buf :
			hw->buffer_spec[index].v4l_ref_buf_addr;
		vf->flag = VFRAME_FLAG_EMPTY_FRAME_V4L;
		aml_buf = (struct aml_buf *)vf->v4l_mem_handle;

		vdec_vframe_ready(vdec, vf);
		kfifo_put(&hw->display_q, (const struct vframe_s *)vf);

		vf_notify_receiver(vdec->vf_provider_name,
			VFRAME_EVENT_PROVIDER_VFRAME_READY, NULL);

		pr_info("[%d] mjpeg EOS notify.\n", vdec->id);
	}

	return 0;
}

static void vmjpeg_work(struct work_struct *work)
{
	struct vdec_mjpeg_hw_s *hw = container_of(work,
	struct vdec_mjpeg_hw_s, work);
	struct vdec_s *vdec = hw_to_vdec(hw);

	mmjpeg_debug_print(DECODE_ID(hw), PRINT_FLAG_BUFFER_DETAIL,
		"%s: result=%d,len=%d:%d\n",
		__func__, hw->dec_result,
		kfifo_len(&hw->newframe_q),
		kfifo_len(&hw->display_q));
	if (hw->dec_result == DEC_RESULT_DONE) {
		vdec_vframe_dirty(hw_to_vdec(hw), hw->chunk);
		hw->chunk = NULL;
	} else if (hw->dec_result == DEC_RESULT_AGAIN) {
		/*
			stream base: stream buf empty or timeout
			frame base: vdec_prepare_input fail
		*/
		if (!vdec_has_more_input(hw_to_vdec(hw))) {
			hw->dec_result = DEC_RESULT_EOS;
			vdec_schedule_work(&hw->work);
			return;
		}
	} else if (hw->dec_result == DEC_RESULT_FORCE_EXIT) {
		pr_info("%s: force exit\n", __func__);
		if (hw->stat & STAT_ISR_REG) {
			amvdec_stop();
			vdec_free_irq(VDEC_IRQ_1, (void *)hw);
			hw->stat &= ~STAT_ISR_REG;
		}
	} else if (hw->dec_result == DEC_RESULT_EOS) {
		pr_info("%s: end of stream\n", __func__);
		if (hw->stat & STAT_VDEC_RUN) {
			amvdec_stop();
			hw->stat &= ~STAT_VDEC_RUN;
		}
		hw->eos = 1;
		notify_v4l_eos(vdec);

		vdec_vframe_dirty(hw_to_vdec(hw), hw->chunk);
		hw->chunk = NULL;
		vdec_clean_input(hw_to_vdec(hw));
	}
	if (hw->stat & STAT_VDEC_RUN) {
		amvdec_stop();
		hw->stat &= ~STAT_VDEC_RUN;
	}
	/*disable mbox interrupt */
	WRITE_VREG(ASSIST_MBOX1_MASK, 0);
	wait_vmjpeg_search_done(hw);

	/* mark itself has all HW resource released and input released */
	if (vdec->parallel_dec == 1)
		vdec_core_finish_run(hw_to_vdec(hw), CORE_MASK_VDEC_1);
	else {
		vdec_core_finish_run(hw_to_vdec(hw), CORE_MASK_VDEC_1
			| CORE_MASK_HEVC);
	}
	del_timer_sync(&hw->check_timer);
	hw->stat &= ~STAT_TIMER_ARM;

	if (hw->vdec_cb)
		hw->vdec_cb(hw_to_vdec(hw), hw->vdec_cb_arg, CORE_MASK_VDEC_1);
}

static int vmjpeg_stop(struct vdec_mjpeg_hw_s *hw)
{
	pr_info("%s ...count = %d\n", __func__, hw->frame_num);

	if (hw->stat & STAT_VDEC_RUN) {
		amvdec_stop();
		pr_info("%s amvdec_stop\n", __func__);
		hw->stat &= ~STAT_VDEC_RUN;
	}

	if (hw->stat & STAT_ISR_REG) {
		vdec_free_irq(VDEC_IRQ_1, (void *)hw);
		hw->stat &= ~STAT_ISR_REG;
	}

	if (hw->stat & STAT_TIMER_ARM) {
		del_timer_sync(&hw->check_timer);
		hw->stat &= ~STAT_TIMER_ARM;
	}
	cancel_work_sync(&hw->work);
	hw->init_flag = 0;

	if (hw->mm_blk_handle) {
		void *bmmu_box_tmp = hw->mm_blk_handle;
		hw->mm_blk_handle = NULL;
		if (hw->run_flag)
			usleep_range(1000, 2000);
		decoder_bmmu_box_free(bmmu_box_tmp);
		bmmu_box_tmp= NULL;
	}

	if (hw->fw) {
		vfree(hw->fw);
		hw->fw = NULL;
	}

	return 0;
}

static int ammvdec_mjpeg_probe(struct platform_device *pdev)
{
	struct vdec_s *pdata = *(struct vdec_s **)pdev->dev.platform_data;
	struct vdec_mjpeg_hw_s *hw = NULL;
	int config_val = 0;
	static struct vframe_operations_s vf_tmp_ops;

	if (pdata == NULL) {
		pr_info("ammvdec_mjpeg memory resource undefined.\n");
		return -EFAULT;
	}

	hw =  vzalloc(sizeof(struct vdec_mjpeg_hw_s));
	if (hw == NULL) {
		pr_info("\nammvdec_mjpeg device data allocation failed\n");
		return -ENOMEM;
	}

	/* the ctx from v4l2 driver. */
	hw->v4l2_ctx = pdata->private;

	pdata->private = hw;
	pdata->dec_status = vmjpeg_dec_status;

	pdata->run = run;
	pdata->run_ready = run_ready;
	pdata->irq_handler = vmjpeg_isr;
	pdata->threaded_irq_handler = vmjpeg_isr_thread_fn;
	pdata->dump_state = vmjpeg_dump_state;

	snprintf(hw->vdec_name, sizeof(hw->vdec_name),
		"vmjpeg-%d", pdev->id);
	snprintf(hw->pts_name, sizeof(hw->pts_name),
		"%s-pts", hw->vdec_name);
	snprintf(hw->new_q_name, sizeof(hw->new_q_name),
		"%s-newframe_q", hw->vdec_name);
	snprintf(hw->disp_q_name, sizeof(hw->disp_q_name),
		"%s-dispframe_q", hw->vdec_name);

	if (pdata->parallel_dec == 1) {
		int i;
		for (i = 0; i < DECODE_BUFFER_NUM_MAX; i++) {
			hw->buffer_spec[i].y_canvas_index = -1;
			hw->buffer_spec[i].u_canvas_index = -1;
			hw->buffer_spec[i].v_canvas_index = -1;
		}
	}

	if (pdata->use_vfm_path)
		snprintf(pdata->vf_provider_name, VDEC_PROVIDER_NAME_SIZE,
			VFM_DEC_PROVIDER_NAME);
	else
		snprintf(pdata->vf_provider_name, VDEC_PROVIDER_NAME_SIZE,
			PROVIDER_NAME ".%02x", pdev->id & 0xff);

	platform_set_drvdata(pdev, pdata);
	hw->platform_dev = pdev;

	if (((debug_enable & IGNORE_PARAM_FROM_CONFIG) == 0) && pdata->config_len) {
		mmjpeg_debug_print(DECODE_ID(hw), 0, "pdata->config: %s\n", pdata->config);
		if (get_config_int(pdata->config, "parm_buffer_margin",
			&config_val) == 0)
			hw->dynamic_buf_num_margin = config_val;
		else
			hw->dynamic_buf_num_margin = dynamic_buf_num_margin;

		if (get_config_int(pdata->config,
			"parm_v4l_canvas_mem_mode",
			&config_val) == 0)
			hw->canvas_mode = config_val;

		if (get_config_int(pdata->config,
			"parm_v4l_canvas_mem_endian",
			&config_val) == 0)
			hw->canvas_endian = config_val;

		if (get_config_int(pdata->config, "sidebind_type",
				&config_val) == 0)
			hw->sidebind_type = config_val;

		if (get_config_int(pdata->config, "sidebind_channel_id",
				&config_val) == 0)
			hw->sidebind_channel_id = config_val;

		if (get_config_int(pdata->config,
			"parm_v4l_codec_enable",
			&config_val) == 0)
			hw->is_used_v4l = config_val;
	} else {
		hw->dynamic_buf_num_margin = dynamic_buf_num_margin;
	}

	hw->dynamic_buf_num_margin = get_dynamic_buf_num_margin(hw);
	hw->buf_num = vmjpeg_get_buf_num(hw);

	memcpy(&vf_tmp_ops, &vf_provider_ops, sizeof(struct vframe_operations_s));
	if (without_display_mode == 1) {
		vf_tmp_ops.get = NULL;
	}
	vf_provider_init(&pdata->vframe_provider, pdata->vf_provider_name,
		&vf_tmp_ops, pdata);

	platform_set_drvdata(pdev, pdata);

	hw->platform_dev = pdev;

	vdec_source_changed(VFORMAT_MJPEG, 3840, 2160, 30);
	if (vmjpeg_init(pdata) < 0) {
		pr_info("ammvdec_mjpeg init failed.\n");
		if (hw) {
			vfree(hw);
			hw = NULL;
		}
		pdata->dec_status = NULL;
		return -ENODEV;
	}
	vdec_set_prepare_level(pdata, start_decode_buf_level);

	if (pdata->parallel_dec == 1)
		vdec_core_request(pdata, CORE_MASK_VDEC_1);
	else {
		vdec_core_request(pdata, CORE_MASK_VDEC_1 | CORE_MASK_HEVC
				| CORE_MASK_COMBINE);
	}

	return 0;
}

static int ammvdec_mjpeg_remove(struct platform_device *pdev)
{
	struct vdec_mjpeg_hw_s *hw =
		(struct vdec_mjpeg_hw_s *)
		(((struct vdec_s *)(platform_get_drvdata(pdev)))->private);
	struct vdec_s *vdec;
	int i;

	if (!hw)
		return -1;
	vdec = hw_to_vdec(hw);

	vmjpeg_stop(hw);

	if (vdec->parallel_dec == 1)
		vdec_core_release(hw_to_vdec(hw), CORE_MASK_VDEC_1);
	else
		vdec_core_release(hw_to_vdec(hw), CORE_MASK_VDEC_1 | CORE_MASK_HEVC);
	vdec_set_status(hw_to_vdec(hw), VDEC_STATUS_DISCONNECTED);
	if (vdec->parallel_dec == 1) {
		for (i = 0; i < DECODE_BUFFER_NUM_MAX; i++) {
			vdec->free_canvas_ex(hw->buffer_spec[i].y_canvas_index, vdec->id);
			vdec->free_canvas_ex(hw->buffer_spec[i].u_canvas_index, vdec->id);
			vdec->free_canvas_ex(hw->buffer_spec[i].v_canvas_index, vdec->id);
		}
	}

	vfree(hw);

	pr_info("%s\n", __func__);
	return 0;
}

/****************************************/

static struct platform_driver ammvdec_mjpeg_driver = {
	.probe = ammvdec_mjpeg_probe,
	.remove = ammvdec_mjpeg_remove,
#ifdef CONFIG_PM
	.suspend = amvdec_suspend,
	.resume = amvdec_resume,
#endif
	.driver = {
		.name = DRIVER_NAME,
	}
};

static void set_debug_flag(const char *module, int debug_flags)
{
	debug_enable = debug_flags;
}

static int __init ammvdec_mjpeg_driver_init_module(void)
{
	if (platform_driver_register(&ammvdec_mjpeg_driver)) {
		pr_err("failed to register ammvdec_mjpeg driver\n");
		return -ENODEV;
	}

	vcodec_profile_register_v2("mmjpeg", VFORMAT_MJPEG, 0);
	vcodec_feature_register(VFORMAT_MJPEG, 0);
	register_set_debug_flag_func(DEBUG_AMVDEC_MJPEG, set_debug_flag);

	return 0;
}

static void __exit ammvdec_mjpeg_driver_remove_module(void)
{
	platform_driver_unregister(&ammvdec_mjpeg_driver);
}

/****************************************/
module_param(debug_enable, uint, 0664);
MODULE_PARM_DESC(debug_enable, "\n debug enable\n");
module_param(pre_decode_buf_level, int, 0664);
MODULE_PARM_DESC(pre_decode_buf_level,
		"\n ammvdec_h264 pre_decode_buf_level\n");
module_param(udebug_flag, uint, 0664);
MODULE_PARM_DESC(udebug_flag, "\n amvdec_mmpeg12 udebug_flag\n");

module_param(dynamic_buf_num_margin, uint, 0664);
MODULE_PARM_DESC(dynamic_buf_num_margin, "\n dynamic_buf_num_margin\n");

module_param(decode_timeout_val, uint, 0664);
MODULE_PARM_DESC(decode_timeout_val, "\n ammvdec_mjpeg decode_timeout_val\n");

module_param_array(max_process_time, uint, &max_decode_instance_num, 0664);

module_param(radr, uint, 0664);
MODULE_PARM_DESC(radr, "\nradr\n");

module_param(start_decode_buf_level, uint, 0664);
MODULE_PARM_DESC(start_decode_buf_level, "\nstart_decode_buf_level\n");

module_param(rval, uint, 0664);
MODULE_PARM_DESC(rval, "\nrval\n");

module_param(without_display_mode, uint, 0664);
MODULE_PARM_DESC(without_display_mode, "\n without_display_mode\n");

module_init(ammvdec_mjpeg_driver_init_module);
module_exit(ammvdec_mjpeg_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC MJMPEG Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
