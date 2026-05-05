#ifndef _DRM_DISPLAY_H_
#define _DRM_DISPLAY_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdint.h>
#include "data.h"

#define DRM_DEV_PATH "/dev/dri/card0"

//#define USE_RGA
#ifdef USE_RGA

#include "rga.h"
#include "im2d.hpp"
#include "RgaApi.h"
#define IR_SAMPLE_VERSION "UVC_V4L2_I2C 2.0.0_beta"

#endif /* USE_RGA */

#ifdef __cplusplus
extern "C"
{
#endif
    struct buffer_object
    {
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint32_t handle;
        uint32_t size;
        uint8_t *vaddr;
        uint32_t fb_id;
    };

    int drm_dev_open(int width, int height);
    int drm_dev_close();
    void drm_display(void *buff);

    uint32_t drm_get_screeninfo_width();
    uint32_t drm_get_screeninfo_height();

    //display thread
    void* drm_display_function(void* threadarg);

#ifdef __cplusplus
}
#endif

#endif /* _DRM_DISPLAY_H_ */
