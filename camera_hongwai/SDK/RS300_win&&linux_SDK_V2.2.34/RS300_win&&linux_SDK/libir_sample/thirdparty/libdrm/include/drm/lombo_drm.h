/**
 * Copyright (C) 2016-2018, LomboTech Co.Ltd.
 * Authors:
 *	lomboswer <lomboswer@lombotech.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _UAPI_LOMBO_DRM_H_
#define _UAPI_LOMBO_DRM_H_

#include <drm/drm.h>

#define DRM_LOMBO_PAGE_FLIP_FLAG_ATOMIC_CACHE	0x1
#define DRM_LOMBO_PAGE_FLIP_FLAG_ATOMIC_FLUSH	0x2
#define DRM_LOMBO_PAGE_FLIP_FLAG_ATOMIC_MASK 0xF
#define DRM_LOMBO_PAGE_FLIP_FLAG_FRAME_FENCE_REQ (1 << 4)
#define DRM_LOMBO_PAGE_FLIP_FLAG_FRAME_FENCE_REL (1 << 5)
#define DRM_LOMBO_PAGE_FLIP_FLAG_PLANE_FENCE_REQ (1 << 6)
#define DRM_LOMBO_PAGE_FLIP_FLAG_PLANE_FENCE_REL (1 << 7)

/**
 * @flag: see DRM_LOMBO_PAGE_FLIP_FLAG_xxx.
 *    1. DRM_LOMBO_PAGE_FLIP_FLAG_ATOMIC_CACHE: do declaration
 *        for atomic kms beginning, this prevent dbreg from loading.
 *    2. DRM_LOMBO_PAGE_FLIP_FLAG_ATOMIC_FLUSH: do declaration
 *        for atomic kms finishing, this stop "preventing dbreg from loading".
 *    3. Between ATOMIC_CACHE and ATOMIC_FLUSH, all kms operations'
 *        configurations will be cached in DBR. Configurations wil
 *        become effective in the first vblank after ATOMIC_FLUSH calling.
 *
 * @frame_fence_fd.require: a require fence of frame.
 * @frame_fence_fd.release: a release fence of frame.
 *
 * @num_fb: number of plane's  fb.
 * @fb_id_ptr: user pointer of fb_id(s).
 *    the num must equals @num_fb.
 * @fb_fence_fd.require_ptr: user pointer of plane fence require.
 *    the num must equals @num_fb.
 * @fb_fence_fd.release_ptr: user pointer of plane fence release.
 *    the num must equals @num_fb.
 */
struct drm_lombo_page_flip_atomic {
	uint32_t flag;

	uint32_t crtc_id;
	union {
		int32_t require;
		int32_t release;
	} frame_fence_fd;

	uint32_t num_fb;
	uint64_t fb_id_ptr;
	union {
		uint64_t require_ptr;
		uint64_t release_ptr;
	} fb_fence_fd;
};

enum {
	LOMBO_IGNORE_IOCTL_PERMIT_SET = 1,
	LOMBO_IGNORE_IOCTL_PERMIT_UNSET,
	LOMBO_IGNORE_IOCTL_PERMIT_GET,
};

struct drm_lombo_rect_wh {
	uint32_t x; /* pos x */
	uint32_t y; /* pos y */
	uint32_t w; /* width */
	uint32_t h; /* height */
};

struct drm_lombo_rect_pt {
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;
};

/**
 * @_SRC_PIPE: which display pipe wb from.
 * @_SRC_CROP_X/Y/W/H: crop of src, in pixel.
 * @_DST_WIDTH/HEIGHT: wb output resolution, in pixel.
 * @_CREATE_FB_QUEUE: create fb_queue with max count of buf of queue.
 */
enum {
	LOMBO_WB_PARA_KEY_SRC_PIPE = 0,
	LOMBO_WB_PARA_KEY_SRC_CROP_X,
	LOMBO_WB_PARA_KEY_SRC_CROP_Y,
	LOMBO_WB_PARA_KEY_SRC_CROP_W,
	LOMBO_WB_PARA_KEY_SRC_CROP_H,
	LOMBO_WB_PARA_KEY_DST_WIDTH,
	LOMBO_WB_PARA_KEY_DST_HEIGHT,
	LOMBO_WB_PARA_KEY_CREATE_FB_QUEUE,
	LOMBO_WB_PARA_KEY_NUM_MAX,
};

/**
 * @key: see LOMBO_WB_PARA_KEY_xxx.
 */
struct drm_lombo_wb_set_para {
	uint32_t key;
	uint64_t value;
};

/**
 * @fb: drm fb id.
 * @release_fence: fence fd created by consumer,
 *     signaled if consumer dont read this buf anymore.
 * @user_priv: private data of user that from.
 */
struct drm_lombo_wb_queue_buf {
	uint32_t fb;
	int release_fence;
	uint64_t user_priv;
};

enum {
	LOMBO_WB_FB_STATE_IDLE = 0,
	LOMBO_WB_FB_STATE_WRITING,
	LOMBO_WB_FB_STATE_WRITTEN,
};

/**
 * @fb: drm fb id return to user.
 * @fb_state: see LOMBO_WB_FB_STATE_xxx.
 * @acquire_fence:
 *     No return to user if @acquire_fence is set as -1 by user.
 *     Otherwise, return positive-value to @acquire_fence which
 *     will be signaled if wb finish writing this fb.
 * @user_priv: private data of user that to return.
 */
struct drm_lombo_wb_dequeue_buf {
	uint32_t fb;
	uint32_t fb_state;
	int acquire_fence;
	uint64_t user_priv;
};

/**
 * @count: the time of wb exec.
 *     keep wb infinitly if @count==0.
 */
struct drm_lombo_wb_start_arg {
	uint32_t count;
};

/**
 * Alpha mode
 * PIXEL_ALPHA: Using the per-pixel alpha.
 * PLANE_ALPHA: Using the per-plane alpha.
 * PLANE_PIXEL_ALPHA: Using the per-plane and per-pixel alpha together.
 */
enum drm_lombo_alpha_mode {
	DRM_LOMBO_PIXEL_ALPHA,
	DRM_LOMBO_PLANE_ALPHA,
	DRM_LOMBO_PLANE_PIXEL_ALPHA,
	DRM_LOMBO_ALPHA_MODE_MAX,
};

/**
 * fb arg at set mode.
 * @fb_id: if fb_id == 0, disable this fb.
 * @is_premul: pixel data is premul alpha.
 * @alpha_mode: enum drm_lombo_alpha_mode.
 * @alpha_value: global alpha value,
 *     it is valid for PLANE_ALPHA or PLANE_PIXEL_ALPHA.
 */
struct drm_lombo_mode_fb {
	uint32_t fb_id; /* fb object contains surface format type */

	/* Source values are 16.16 fixed point */
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_h;
	uint32_t src_w;

	/* dest location on actual screen */
	int32_t crtc_x;
	int32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;

	uint32_t reserve[4]; /* reserve for pre_scale dest location */

	uint8_t alpha_mode;
	uint8_t alpha_value;
	uint8_t is_premul;
	uint8_t pad;
};

/**
 * arg of set/disable plane.
 * @plane_id: plane id to be set/disable.
 * @crtc_id: crtc id.
 * @fb_num: valid fb num to be set to plane.
 * @fbs_ptr: pointer to user_memory array of struct drm_lombo_mode_fb.
 * if @fb_num > 0, it set plane, and @fbs_ptr must be non-null.
 * if @fb_num = 0, it disable plane.
 */
struct drm_lombo_mode_set_plane {
	uint32_t plane_id;
	uint32_t crtc_id;
	uint32_t flags; /* see struct drm_mode_set_plane.flags */

	uint32_t fb_num;
	uint64_t fbs_ptr;
};

struct drm_lombo_fb_array_cmd {
	uint32_t num_fb;
	uint32_t data_offset;
};

/**
 * drm_lombo_fb_array_fbcmd: a fb cmd for creating fb_array.
 * @fb_id: drm fb id.
 * @src: src crop of @fb. These are Q16 values.
 * @dst: dest location on fb_array.
 * @is_premul: pixel data is premul alpha.
 * @alpha_mode: enum drm_lombo_alpha_mode.
 * @alpha_value: global alpha value of plane alpha.
 */
struct drm_lombo_fb_array_fbcmd {
	uint32_t fb_id;
	struct drm_lombo_rect_wh src;
	struct drm_lombo_rect_wh dst;
	uint8_t alpha_mode;
	uint8_t alpha_value;
	uint8_t is_premul;
	uint8_t pad;
};

/* hw ip version */
struct drm_lombo_dit_ver {
	uint32_t major;
	uint32_t minor;
};

/* user handle of dit */
typedef uint32_t drm_lombo_dit_user_handle_t;

/* dit timeout, default 1s in ditdrv */
struct drm_lombo_dit_timeout {
	drm_lombo_dit_user_handle_t handle;
	uint64_t time_ns;
};

/*
 * @handle: client handle of dit.
 * @top_field_first:
 *     0: BFF;
 *     1: TFF.
 * @frame_seq:
 *     0: dit out the 1st frame.
 *     1: dit out the 2nd frame.
 * @in_fb0: input fb0 id.
 * @in_fb1: input fb1 id.
 * @in_fb2: input fb2 id.
 * @out_fb0: output fb0 id.
 * @out_fb1: output fb1 id.
 */
struct drm_lombo_dit_exec_arg {
	drm_lombo_dit_user_handle_t handle;

	uint32_t top_filed_first;

	uint32_t frame_seq;
	uint32_t in_fb0;
	uint32_t in_fb1;
	uint32_t in_fb2;

	uint32_t out_fb0;
};

/*
 * @ROT_X: clockwise rotation.
 * COPY/FLIP_H/FLIP_V/ROT_90 are the base transform operation.
 * Others are the expand transform operation(flip first, then rot).
 */
typedef enum {
	LOMBO_DRM_TRANSFORM_COPY = 0, /* original image copy */
	LOMBO_DRM_TRANSFORM_FLIP_H = 1, /* (1 << 0) */
	LOMBO_DRM_TRANSFORM_FLIP_V = 2, /* (1 << 1) */
	LOMBO_DRM_TRANSFORM_ROT_90 = 4, /* (1 << 2) */
	LOMBO_DRM_TRANSFORM_ROT_180 = 3, /* (FLIP_H | FLIP_V) */
	LOMBO_DRM_TRANSFORM_ROT_270 = 7, /* ((FLIP_H | FLIP_V) + ROT_90) */
	LOMBO_DRM_TRANSFORM_FLIP_H_ROT_90 = 5, /* (FLIP_H + ROT_90) */
	LOMBO_DRM_TRANSFORM_FLIP_V_ROT_90 = 6, /* (FLIP_V + ROT_90) */
} drm_lombo_transform_t;

/* hw ip version */
struct drm_lombo_rot_ver {
	uint32_t major;
	uint32_t minor;
};

/* user handle of rot */
typedef uint32_t drm_lombo_rot_user_handle_t;

/* rot timeout, default 1s in ditdrv */
struct drm_lombo_rot_timeout {
	drm_lombo_rot_user_handle_t handle;
	uint64_t time_ns;
};

/*
 * @handle: client handle of rot.
 * @transform: rotate&flip mode. see drm_lombo_transform_t.
 * @in_fb: input fb id.
 * @out_fb: output fb id.
 */
struct drm_lombo_rot_exec_arg {
	drm_lombo_rot_user_handle_t handle;
	drm_lombo_transform_t transform;
	uint32_t in_fb;
	uint32_t out_fb;
};

/* user handle of scale */
typedef uint32_t drm_lombo_scale_user_handle_t;

/* scale timeout, default 3s in drv */
struct drm_lombo_scale_timeout {
	drm_lombo_scale_user_handle_t handle;
	uint64_t time_ns;
};

/*
 * @fb_id: id of drm framebuffer.
 * @crop_x/y/w/h: crop(x, y, width, height) of fb.
 */
struct drm_lombo_scale_fb {
	uint32_t fb_id;
	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t crop_w;
	uint32_t crop_h;
};

/*
 * @handle: client handle of scale.
 * @in_fb: input fb id.
 * @out_fb: output fb id.
 */
struct drm_lombo_scale_exec_arg {
	drm_lombo_scale_user_handle_t handle;

	struct drm_lombo_scale_fb in_fb;
	struct drm_lombo_scale_fb out_fb;
};

/* LOMBO NR for DRM IOCLT */

#define DRM_LOMBO_PAGE_FLIP_ATOMIC 0x01

#define DRM_LOMBO_WB_SET_PARA 0x02
#define DRM_LOMBO_WB_QUEUE_BUF 0x03
#define DRM_LOMBO_WB_DEQUEUE_BUF 0x04
#define DRM_LOMBO_WB_START 0x05
#define DRM_LOMBO_WB_STOP 0x06
#define DRM_LOMBO_IGNORE_IOCTL_PERMIT 0x07
#define DRM_LOMBO_MODE_OBJ_SETPROPERTY 0x08
#define DRM_LOMBO_MODE_SETGAMMA 0x09

#define DRM_LOMBO_DIT_GET_VER 0x10
#define DRM_LOMBO_DIT_OPEN 0x11
#define DRM_LOMBO_DIT_CLOSE 0x12
#define DRM_LOMBO_DIT_SET_TIMEOUT 0x13
#define DRM_LOMBO_DIT_EXEC 0x14

#define DRM_LOMBO_ROT_GET_VER 0x18
#define DRM_LOMBO_ROT_OPEN 0x19
#define DRM_LOMBO_ROT_CLOSE 0x1A
#define DRM_LOMBO_ROT_SET_TIMEOUT 0x1B
#define DRM_LOMBO_ROT_EXEC 0x1C

#define DRM_LOMBO_SCALE_OPEN 0x20
#define DRM_LOMBO_SCALE_CLOSE 0x21
#define DRM_LOMBO_SCALE_SET_TIMEOUT 0x22
#define DRM_LOMBO_SCALE_EXEC 0x23

/* DRM_IOCTL_LOMBO */

/**
 * If there are more than one plane needed to be set for next page flip,
 * please use DRM_IOCTL_LOMBO_PAGE_FLIP_ATOMIC(cache) and
 * DRM_IOCTL_LOMBO_PAGE_FLIP_ATOMIC(flush) to include "all
 * setplanes & setcrtc". All calling must to use same crtc_id.
 * ie:
 * DRM_IOCTL_LOMBO_PAGE_FLIP_ATOMIC(crtc_id, flag = cache);
 * DRM_IOCTL_MODE_SETCRTC(crtc_id, fb);
 * DRM_IOCTL_MODE_SETPLANE(crtc_id, plane_0);
 * DRM_IOCTL_MODE_SETPLANE(crtc_id, plane_1);
 * ...
 * DRM_IOCTL_LOMBO_PAGE_FLIP_ATOMIC(crtc_id, flag = flush);
 */
#define DRM_IOCTL_LOMBO_PAGE_FLIP_ATOMIC DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_PAGE_FLIP_ATOMIC, struct drm_lombo_page_flip_atomic)

#define DRM_IOCTL_LOMBO_WB_SET_PARA DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_WB_SET_PARA, struct drm_lombo_wb_set_para)
#define DRM_IOCTL_LOMBO_WB_QUEUE_BUF DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_WB_QUEUE_BUF, struct drm_lombo_wb_queue_buf)
#define DRM_IOCTL_LOMBO_WB_DEQUEUE_BUF DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_WB_DEQUEUE_BUF, struct drm_lombo_wb_dequeue_buf)
#define DRM_IOCTL_LOMBO_WB_START DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_WB_START, struct drm_lombo_wb_start_arg)
#define DRM_IOCTL_LOMBO_WB_STOP DRM_IO(DRM_COMMAND_BASE +	\
		DRM_LOMBO_WB_STOP)

#define DRM_IOCTL_LOMBO_IGNORE_IOCTL_PERMIT DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_IGNORE_IOCTL_PERMIT, unsigned int)

/**
 * lombo mode set obj property.
 */
#define DRM_IOCTL_LOMBO_MODE_OBJ_SETPROPERTY DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_LOMBO_MODE_OBJ_SETPROPERTY,	\
		struct drm_mode_obj_set_property)

/**
 * lombo mode set gamma.
 */
#define DRM_IOCTL_LOMBO_MODE_SETGAMMA DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_LOMBO_MODE_SETGAMMA, struct drm_mode_crtc_lut)

#define DRM_IOCTL_LOMBO_DIT_GET_VER DRM_IOR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_DIT_GET_VER, struct drm_lombo_dit_ver)
#define DRM_IOCTL_LOMBO_DIT_OPEN DRM_IOR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_DIT_OPEN, drm_lombo_dit_user_handle_t)
#define DRM_IOCTL_LOMBO_DIT_CLOSE DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_DIT_CLOSE, drm_lombo_dit_user_handle_t)
#define DRM_IOCTL_LOMBO_DIT_SET_TIMEOUT DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_DIT_SET_TIMEOUT, struct drm_lombo_dit_timeout)
#define DRM_IOCTL_LOMBO_DIT_EXEC DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_DIT_EXEC, struct drm_lombo_dit_exec_arg)

#define DRM_IOCTL_LOMBO_ROT_GET_VER DRM_IOR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_ROT_GET_VER, struct drm_lombo_rot_ver)
#define DRM_IOCTL_LOMBO_ROT_OPEN DRM_IOR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_ROT_OPEN, drm_lombo_rot_user_handle_t)
#define DRM_IOCTL_LOMBO_ROT_CLOSE DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_ROT_CLOSE, drm_lombo_rot_user_handle_t)
#define DRM_IOCTL_LOMBO_ROT_SET_TIMEOUT DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_ROT_SET_TIMEOUT, struct drm_lombo_rot_timeout)
#define DRM_IOCTL_LOMBO_ROT_EXEC DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_ROT_EXEC, struct drm_lombo_rot_exec_arg)

#define DRM_IOCTL_LOMBO_SCALE_OPEN DRM_IOR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_SCALE_OPEN, drm_lombo_scale_user_handle_t)
#define DRM_IOCTL_LOMBO_SCALE_CLOSE DRM_IOWR(DRM_COMMAND_BASE +	\
		DRM_LOMBO_SCALE_CLOSE, drm_lombo_scale_user_handle_t)
#define DRM_IOCTL_LOMBO_SCALE_SET_TIMEOUT DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_SCALE_SET_TIMEOUT, struct drm_lombo_scale_timeout)
#define DRM_IOCTL_LOMBO_SCALE_EXEC DRM_IOW(DRM_COMMAND_BASE +	\
		DRM_LOMBO_SCALE_EXEC, struct drm_lombo_scale_exec_arg)

#endif /* _UAPI_LOMBO_DRM_H_ */
