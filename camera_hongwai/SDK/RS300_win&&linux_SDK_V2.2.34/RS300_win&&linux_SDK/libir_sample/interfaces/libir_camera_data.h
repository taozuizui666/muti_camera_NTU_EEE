#ifndef __LIBIR_CAMERA_FORMAT_H__
#define __LIBIR_CAMERA_FORMAT_H__

#include <stdint.h>
#include "Eazyai_yolov5.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef enum IrImageFormat
{
    YUYV_IMAGE = 0,
    NV12_IMAGE,
    UYVY_IMAGE,

    // don't use IR_IMAGE_FORMAT_CNT
    IR_IMAGE_FORMAT_CNT, 
} IrImageFormat_e;

typedef struct IrCamera
{
    /* point of data transform */
    int (*data_transform)(uint8_t* src, int width, int height, uint8_t* dst);
    /* camera device */
    char*           node_dev;
    /* width of image */
    int             width;
    /* height of data, width * height == one frame data length */
    int             height;
    /* fps of camera */
    int             fps;
    /* format of v4l2 */
    int             format;
    /* height of image */
    int             image_height;
    /* height of info_line */
    int             info_line_height;
    /* format of image, type see IrImageFormat_e */
    IrImageFormat_e image_format;
} IrCamera_t;

typedef struct IrImageData
{
    uint32_t len;
    uint8_t* data;
} IrImageData_t;

typedef struct IrStatusErrData
{
    /// Is the camera image output abnormal(0:normal image    1:image anomaly)
    uint8_t camera_sta;
    /// Is the focal temperature module in the normal range(0:within   1:out of range   3:sensor abnormal)
    uint8_t focal_temp_sta;
    /// Is the heating function abnormal(0:normal    1:error)
    uint8_t defrost_sta;
    /// Whether the heating function is on or not(0:open    1:close)
    uint8_t defrost_switch_sta;
    /// Whether the interparam is calibrated or not(0:calibrated    1:not calibrated)
    uint8_t interparam_sta;
    // Update the shutter countdown
    uint8_t ffc_countdown;
} IrStatusErrData_t;

typedef struct IrDataOutput
{
    // device status data
    IrStatusErrData_t   ir_status_err_data;
    // image data
    IrImageData_t*      ir_image_data;
    // target detect data
    TagInfo*            tag_info;
} IrDataOutput_t;


#ifdef __cplusplus
}
#endif // __cplusplus

#endif