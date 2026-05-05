#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include "drm_display.h"
#include "libirparse.h"

#define UNUSED(x) ((void)(x))

static int drm_dev_fd = 0;
static uint8_t buf_flag = 0;
static struct buffer_object plane_buf[2];
static struct buffer_object buf;
static drmEventContext drm_ev_cont = {};
static int fd;
static drmModeConnector *conn;
static drmModeRes *res;
static drmModePlaneRes *plane_res;
static uint32_t crtc_id;
static void write_color(struct buffer_object *bo, unsigned int color)
{
	uint8_t *pt;
	int i;

	pt = bo->vaddr;
	for (i = 0; i < (int)(bo->size / 4); i++)
	{
		*pt = color;
		pt++;
	}
}

static int modeset_create_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_create_dumb create = {};
	struct drm_mode_map_dumb map = {};
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	int ret;

	create.width = bo->width;
	create.height = bo->height;
	create.bpp = 24;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

	bo->pitch = create.pitch;
	bo->size = create.size;
	bo->handle = create.handle;

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

	bo->vaddr = (uint8_t*)(mmap(0, create.size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, map.offset));

#if 0
	drmModeAddFB(fd, bo->width, bo->height, 24, 32, bo->pitch,
			   bo->handle, &bo->fb_id);
#else
	offsets[0] = 0;
	handles[0] = bo->handle;
	pitches[0] = bo->pitch;

	ret = drmModeAddFB2(fd, bo->width, bo->height,
						DRM_FORMAT_BGR888, handles, pitches, offsets, &bo->fb_id, 0); //DRM_FORMAT_BGR888 bmp DRM_FORMAT_XRGB8888
	if (ret)
	{
		printf("drmModeAddFB2 return err %d\n", ret);
		return 0;
	}

	printf("bo->pitch %d\n", bo->pitch);

#endif

	memset(bo->vaddr, 0x00, bo->size);

	return 0;
}

static void modeset_destroy_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_destroy_dumb destroy = {};

	drmModeRmFB(fd, bo->fb_id);

	munmap(bo->vaddr, bo->size);

	destroy.handle = bo->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

static void modeset_page_flip_handler(int fd, uint32_t frame,
									  uint32_t sec, uint32_t usec, void *data)
{
	uint32_t crtc_id = *(uint32_t *)data;

	drmModePageFlip(fd, crtc_id, plane_buf[buf_flag].fb_id, DRM_MODE_PAGE_FLIP_EVENT, data);

	UNUSED(frame);
	UNUSED(sec);
	UNUSED(usec);
}

void get_planes_property(int fd, drmModePlaneRes *pr)
{
	drmModeObjectPropertiesPtr props;
	int i, j;
	drmModePropertyPtr p;

	for (i = 0; i < (int)(pr->count_planes); i++)
	{

		printf("planes id %d\n", pr->planes[i]);
		props = drmModeObjectGetProperties(fd, pr->planes[i],
										   DRM_MODE_OBJECT_PLANE);

		for (j = 0; j < (int)(props->count_props); j++)
		{
			p = drmModeGetProperty(fd, props->props[j]);
			printf("get property ,name %s, id %d\n", p->name, p->prop_id);
			drmModeFreeProperty(p);
		}

		printf("\n\n");
	}
}

int drm_dev_open(int width, int height)
{

	uint32_t conn_id;
	uint32_t x, y;
	uint32_t plane_id;
	int ret;
	int rotation = 1, alpha = 10;

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

	res = drmModeGetResources(fd);
	crtc_id = res->crtcs[0];
	conn_id = res->connectors[0];

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	ret = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (ret)
	{
		printf("failed to set client cap\n");
		return -1;
	}
	plane_res = drmModeGetPlaneResources(fd);
	plane_id = plane_res->planes[0];

	printf("get plane count %d,plane_id %d\n", plane_res->count_planes, plane_id);

	conn = drmModeGetConnector(fd, conn_id);
	if ((!conn)||conn->connection != DRM_MODE_CONNECTED){
		printf("have no screen\n");
		return -1;
	}

	buf.width = conn->modes[0].hdisplay;
	buf.height = conn->modes[0].vdisplay;
	/*
	printf("get connector nanme %s,hdisplay %d, vdisplay %d,vrefresh %d\n",conn->modes[0].name,conn->modes[0].vdisplay,\
		conn->modes[0].hdisplay,conn->modes[0].vrefresh);
	modeset_create_fb(fd, &buf);
	drmModeSetCrtc(fd, crtc_id, buf.fb_id,0, 0, &conn_id, 1, &conn->modes[0]);
	write_color(&buf,0xff00ff00);
	*/

	// -------------------  overlay 1
	plane_buf[0].width = width;
	plane_buf[0].height = height;
	modeset_create_fb(fd, &plane_buf[0]);
	plane_buf[1].width = width;
	plane_buf[1].height = height;
	modeset_create_fb(fd, &plane_buf[1]);
	ret = drmModeSetPlane(fd, plane_res->planes[1], crtc_id, plane_buf[0].fb_id, 0,
						0, 0, 800, 1200,
						0, 0, (plane_buf[0].width) << 16, (plane_buf[0].height) << 16);
	if (ret < 0)
		printf("drmModeSetPlane err %d\n", ret);

	return 0;
}

int drm_dev_close()
{
	modeset_destroy_fb(fd, &buf);
	modeset_destroy_fb(fd, &plane_buf[1]);
	modeset_destroy_fb(fd, &plane_buf[0]);

	drmModeFreeConnector(conn);
	drmModeFreePlaneResources(plane_res);
	drmModeFreeResources(res);
	close(fd);
	return 0;
}

void drm_display(void *buff)
{
	buf_flag ^= 1;//double buffer to avoid line crash
	memcpy(plane_buf[buf_flag].vaddr, buff, plane_buf[buf_flag].size);
	drmModeSetPlane(fd, plane_res->planes[1], crtc_id, plane_buf[buf_flag].fb_id, 0,
						0, 0, 800, 1200,
						0, 0, (plane_buf[buf_flag].width) << 16, (plane_buf[buf_flag].height) << 16);
}

u_int32_t drm_get_screeninfo_width()
{
	return plane_buf[buf_flag].width;
}

u_int32_t drm_get_screeninfo_height()
{
	return plane_buf[buf_flag].height;
}

#ifdef USE_RGA
IM_API static void empty_structure(rga_buffer_t* src, rga_buffer_t* dst, rga_buffer_t* pat, im_rect* srect, im_rect* drect, im_rect* prect) {
	if (src != NULL)
		memset(src, 0, sizeof(*src));
	if (dst != NULL)
		memset(dst, 0, sizeof(*dst));
	if (pat != NULL)
		memset(pat, 0, sizeof(*pat));
	if (srect != NULL)
		memset(srect, 0, sizeof(*srect));
	if (drect != NULL)
		memset(drect, 0, sizeof(*drect));
	if (prect != NULL)
		memset(prect, 0, sizeof(*prect));
}
#endif

//display thread function
void* drm_display_function(void* threadarg)
{
	if (threadarg == NULL)
	{
		printf("data is NULL\n");
		return NULL;
	}

	StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

	int ret;
	int drm_dev_open_flag = 0;
	int file_n = 0;
	int cnt = 0;

	uint8_t* rgb_image_frame = NULL;
	rgb_image_frame = (uint8_t*)malloc(stream_frame_info->width * stream_frame_info->height * 3);
	if (rgb_image_frame == NULL)
	{
		printf("there is no more space!\n");
		return NULL;
	}

#ifdef USE_RGA
	rga_buffer_t src;
	rga_buffer_t dst;
	rga_buffer_t pat;
	rga_info_t src_info, dst_info;
	im_rect srect;
	im_rect drect;
	im_rect prect;
	empty_structure(NULL, NULL, &pat, &srect, &drect, &prect);
	uint8_t* src_rgb_image_frame = NULL;
	src_rgb_image_frame = (uint8_t*)malloc(stream_frame_info->width * stream_frame_info->height * 3);
	if (src_rgb_image_frame == NULL)
	{
		printf("there is no more space!\n");
		return NULL;
	}
#endif

	while (isRUNNING)
	{
		sem_wait(&image_sem);
#ifdef USE_RGA
		memcpy(src_rgb_image_frame, stream_frame_info->image_info.data, stream_frame_info->image_info.byte_size); //image data
		if ((stream_frame_info->frame_output_format == NV12_IMAGE) || (stream_frame_info->frame_output_format == NV12_AND_TEMP))
		{
			src = wrapbuffer_virtualaddr(src_rgb_image_frame, stream_frame_info->width, \
				stream_frame_info->height, RK_FORMAT_YCbCr_420_SP);
			dst = wrapbuffer_virtualaddr(rgb_image_frame, stream_frame_info->width, \
				stream_frame_info->height, RK_FORMAT_RGB_888);
		}
		if ((stream_frame_info->frame_output_format == YUYV_IMAGE) || (stream_frame_info->frame_output_format == YUYV_AND_TEMP))
		{
			src = wrapbuffer_virtualaddr(src_rgb_image_frame, stream_frame_info->width, \
				stream_frame_info->height, RK_FORMAT_YVYU_422);
			dst = wrapbuffer_virtualaddr(rgb_image_frame, stream_frame_info->width, \
				stream_frame_info->height, RK_FORMAT_RGB_888);
		}

		src_info.fd = src.fd;
		src_info.virAddr = src.vir_addr;
		src_info.mmuFlag = 1;
		rga_set_rect(&src_info.rect, srect.x, srect.y, src.width, src.height, src.wstride, src.hstride, src.format);
		dst_info.fd = dst.fd;
		dst_info.virAddr = dst.vir_addr;
		dst_info.mmuFlag = 1;
		rga_set_rect(&dst_info.rect, drect.x, drect.y, dst.width, dst.height, dst.wstride, dst.hstride, dst.format);
		RgaBlit(&src_info, &dst_info, NULL);//旋转+缩放+颜色转换
#else
		if ((stream_frame_info->frame_output_format == YUYV_IMAGE) || (stream_frame_info->frame_output_format == YUYV_AND_TEMP)
			|| (stream_frame_info->frame_output_format == UYVY_IMAGE))
		{
			yuv422_to_rgb(stream_frame_info->image_info.data, (stream_frame_info->width*stream_frame_info->height), rgb_image_frame);
		}
		if ((stream_frame_info->frame_output_format == NV12_IMAGE) || (stream_frame_info->frame_output_format == NV12_AND_TEMP))
		{
			nv12_to_rgb(stream_frame_info->image_info.data, stream_frame_info->width, stream_frame_info->height, rgb_image_frame);
		}

#endif
		if(drm_dev_open_flag == 0)
		{
			ret = drm_dev_open(stream_frame_info->width, stream_frame_info->height); //init display
			if (ret != 0)
			{
				printf("drm dev open fail\n");
				return NULL;
			}
			drm_dev_open_flag = 1;
		}
		drm_display(rgb_image_frame); //send to display
		sem_post(&image_done_sem);
	}

	if (rgb_image_frame != NULL)
	{
		free(rgb_image_frame);
		rgb_image_frame = NULL;
	}
#ifdef USE_RGA
	if (src_rgb_image_frame != NULL)
	{
		free(src_rgb_image_frame);
		src_rgb_image_frame = NULL;
	}
#endif
	printf("display thread exit!!\n");
	drm_dev_close();
	return NULL;
}