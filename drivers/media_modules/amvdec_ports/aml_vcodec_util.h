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
#ifndef _AML_VCODEC_UTIL_H_
#define _AML_VCODEC_UTIL_H_

#include <linux/types.h>

#ifdef __KERNEL__
#include <uapi/linux/videodev2.h>
#else
#include <uapi/linux/time.h>
#endif
#include <linux/version.h>

#include "aml_buf_core.h"
#include "aml_task_chain.h"

/*
typedef unsigned long long	u64;
typedef signed long long	s64;
typedef unsigned int		u32;
typedef unsigned short int	u16;
typedef short int		s16;
typedef unsigned char		u8;
*/
#define CODEC_MODE(a, b, c, d)\
	(((u8)(a) << 24) | ((u8)(b) << 16) | ((u8)(c) << 8) | (u8)(d))

#define BUFF_IDX(h, i)\
	(((ulong)(h) << 8) | (u8)(i))

struct aml_vcodec_mem {
	int	index;
	ulong	addr;
	u32	size;
	void	*vaddr;
	u32	bytes_used;
	u32	offset;
	u64	timestamp;
	u32	model;
	ulong	meta_ptr;
	struct dma_buf *dbuf;
};

struct aml_vcodec_ctx;
struct aml_vcodec_dev;

extern u32 debug_mode;
extern u32 disable_vpp_dw_mmu;
extern int t3x_tw_output;

#ifdef v4l_dbg
#undef v4l_dbg
#endif

/* v4l debug define. */
#define V4L_DEBUG_CODEC_ERROR	(0)
#define V4L_DEBUG_CODEC_PRINFO	(1 << 0)
#define V4L_DEBUG_CODEC_STATE	(1 << 1)
#define V4L_DEBUG_CODEC_BUFMGR	(1 << 2)
#define V4L_DEBUG_CODEC_INPUT	(1 << 3)
#define V4L_DEBUG_CODEC_OUTPUT	(1 << 4)
#define V4L_DEBUG_CODEC_COUNT	(1 << 5)
#define V4L_DEBUG_CODEC_PARSER	(1 << 6)
#define V4L_DEBUG_CODEC_PROT	(1 << 7)
#define V4L_DEBUG_CODEC_EXINFO	(1 << 8)
#define V4L_DEBUG_VPP_BUFMGR	(1 << 9)
#define V4L_DEBUG_VPP_DETAIL	(1 << 10)
#define V4L_DEBUG_TASK_CHAIN	(1 << 11)
#define V4L_DEBUG_GE2D_BUFMGR	(1 << 12)
#define V4L_DEBUG_GE2D_DETAIL	(1 << 13)

#define __v4l_dbg(h, id, fmt, args...)					\
	do {								\
		if (h)							\
			pr_info("[%d]: " fmt, id, ##args);		\
		else							\
			pr_info(fmt, ##args);				\
	} while (0)

#define __v4l_pr_debug(h, id, fmt, args...)				\
	do {								\
		if (h)							\
			pr_debug("[%d]: " fmt, id, ##args);		\
		else							\
			pr_debug(fmt, ##args);				\
	} while (0)

#define v4l_dbg(h, flags, fmt, args...)						\
	do {									\
		struct aml_vcodec_ctx *__ctx = (struct aml_vcodec_ctx *) h;	\
		if ((flags == V4L_DEBUG_CODEC_ERROR) ||				\
			(flags == V4L_DEBUG_CODEC_PRINFO) ||			\
			(debug_mode & flags)) {					\
			if (flags == V4L_DEBUG_CODEC_ERROR) {			\
				__v4l_dbg(h, __ctx->id, "[ERR]: " fmt, ##args);	\
			} else	{						\
				__v4l_dbg(h, __ctx->id, fmt, ##args);		\
			}							\
		}								\
	} while (0)

#define v4l_dbg_ext(id, flags, fmt, args...)					\
	do {									\
		if ((flags == V4L_DEBUG_CODEC_ERROR) ||				\
			(flags == V4L_DEBUG_CODEC_PRINFO) ||			\
			(debug_mode & flags)) {					\
			if (flags == V4L_DEBUG_CODEC_ERROR) {			\
				__v4l_dbg(1, id, "[ERR]: " fmt, ##args);	\
			} else	{						\
				__v4l_dbg(1, id, fmt, ##args);			\
			}							\
		}								\
	} while (0)

#define v4l_pr_debug(h, flags, fmt, args...)					\
	do {									\
		struct aml_vcodec_ctx *__ctx = (struct aml_vcodec_ctx *) h;	\
		if ((flags == V4L_DEBUG_CODEC_ERROR) ||				\
			(flags == V4L_DEBUG_CODEC_PRINFO) ||			\
			(debug_mode & flags)) {					\
			if (flags == V4L_DEBUG_CODEC_ERROR) {			\
				__v4l_pr_debug(h, __ctx->id,			\
					"[ERR]: " fmt, ##args);			\
			} else	{						\
				__v4l_pr_debug(h, __ctx->id, fmt, ##args);	\
			}							\
		}								\
	} while (0)

void aml_vcodec_set_curr_ctx(struct aml_vcodec_dev *dev,
	struct aml_vcodec_ctx *ctx);
struct aml_vcodec_ctx *aml_vcodec_get_curr_ctx(struct aml_vcodec_dev *dev);

/*
 * todo
 */
int user_to_task(enum buf_core_user user);

/*
 * todo
 */
int task_to_user(enum task_type_e task);
inline void *aml_media_mem_alloc(size_t size, gfp_t flags);
inline void aml_media_mem_free(const void *addr);

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0)
#ifdef __KERNEL__
static inline __u64 timeval_to_ns(const struct __kernel_v4l2_timeval *tv)
#else
static inline __u64 timeval_to_ns(const struct timeval *tv)
#endif
{
	return (__u64)tv->tv_sec * 1000000000ULL + tv->tv_usec * 1000;
}
#endif

#endif /* _AML_VCODEC_UTIL_H_ */
