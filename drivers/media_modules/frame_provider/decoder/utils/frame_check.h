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
#ifndef __FRAME_CHECK_H__
#define __FRAME_CHECK_H__


#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/amlogic/media/vfm/vframe.h>
#include <linux/kfifo.h>
#include "../../../common/media_utils/media_kernel_version.h"

#define FRAME_CHECK
#define AUX_DATA_CRC


#define YUV_DUMP_NUM  20

#define SIZE_CRC	64
#define SIZE_CHECK_Q 128

#define USER_CMP_POOL_MAX_SIZE (SIZE_CHECK_Q)

struct pic_dump_t{
	struct file *yuv_fp;
	loff_t yuv_pos;
	unsigned int start;
	unsigned int num;
	unsigned int end;
	unsigned int dump_cnt;

	unsigned int buf_size;
	char *buf_addr;
};

struct pic_check_t{
	struct file *check_fp;
	loff_t check_pos;

	struct file *compare_fp;
	loff_t compare_pos;
	unsigned int cmp_crc_cnt;
	void *fbc_planes[4];
	void *check_addr;
	DECLARE_KFIFO(new_chk_q, char *, SIZE_CHECK_Q);
	DECLARE_KFIFO(wr_chk_q, char *, SIZE_CHECK_Q);
};

struct aux_data_check_t{
	struct file *check_fp;
	loff_t check_pos;

	struct file *compare_fp;
	loff_t compare_pos;
	unsigned int cmp_crc_cnt;
	void *check_addr;

	DECLARE_KFIFO(new_chk_q, char *, SIZE_CHECK_Q);
	DECLARE_KFIFO(wr_chk_q, char *, SIZE_CHECK_Q);
};


struct pic_check_mgr_t{
	int id;
	int enable;
	unsigned int frame_cnt;
	/* pic info */
	unsigned int canvas_w;
	unsigned int canvas_h;
	unsigned int size_y;	//real size
	unsigned int size_uv;
	unsigned int size_pic;
	unsigned int last_size_pic;
	void *y_vaddr;
	void *uv_vaddr;
	ulong y_phyaddr;
	ulong uv_phyaddr;
	int err_crc_block;

	int file_cnt;
	atomic_t work_inited;
	struct work_struct frame_check_work;

	struct pic_check_t pic_check;
	struct pic_dump_t  pic_dump;

	struct usr_crc_info_t *cmp_pool;
	int usr_cmp_num;
	int usr_cmp_result;
	/* for mjpeg u different addr with v */
	bool mjpeg_flag;
	void *extra_v_vaddr;
	ulong extra_v_phyaddr;
	int yuvsum;
	u32 width;
	u32 height;
	unsigned char interlace_flag;
};

struct aux_data_check_mgr_t{
	int id;
	int enable;
	unsigned int frame_cnt;
	/* pic info */
	int aux_size;
	char *aux_addr;

	int file_cnt;
	atomic_t work_inited;
	struct work_struct aux_data_check_work;

	struct aux_data_check_t aux_data_check;
};


int dump_yuv_trig(struct pic_check_mgr_t *mgr,
	int id, int start, int num);

int decoder_do_frame_check(struct vdec_s *vdec, struct vframe_s *vf);

int decoder_do_aux_data_check(struct vdec_s *vdec, char *aux_buffer, int size);

int frame_check_init(struct pic_check_mgr_t *mgr, int id);

void frame_check_exit(struct pic_check_mgr_t *mgr);

ssize_t frame_check_show(KV_CLASS_CONST struct class *class,
		KV_CLASS_ATTR_CONST struct class_attribute *attr, char *buf);

ssize_t frame_check_store(KV_CLASS_CONST struct class *class,
		KV_CLASS_ATTR_CONST struct class_attribute *attr,
		const char *buf, size_t size);

ssize_t dump_yuv_show(KV_CLASS_CONST struct class *class,
		KV_CLASS_ATTR_CONST struct class_attribute *attr, char *buf);

ssize_t dump_yuv_store(KV_CLASS_CONST struct class *class,
		KV_CLASS_ATTR_CONST struct class_attribute *attr,
		const char *buf, size_t size);

void vdec_frame_check_exit(struct vdec_s *vdec);
int vdec_frame_check_init(struct vdec_s *vdec);

void vdec_aux_data_check_exit(struct vdec_s *vdec);
int vdec_aux_data_check_init(struct vdec_s *vdec);


#endif /* __FRAME_CHECK_H__ */

