#define _CRT_SECURE_NO_WARNINGS
#include "uvc_camera.h"
#include <atomic>

std::atomic_bool isRUNNING(true);
IrVideoHandle_t* ir_image_video_handle = NULL;
IrVideoHandle_t* ir_temp_video_handle = NULL;
//temp_measure_on 0:Dont measure temp 1:Start measure temp
int temp_measure_on = 0;

void wait_sem_for_streaming()
{
#if defined(_WIN32)
    WaitForSingleObject(image_done_sem, INFINITE);	//waitting for image singnal
    if (temp_measure_on == 1)
    {
        WaitForSingleObject(temp_done_sem, INFINITE);	//waitting for temp singnal
    }
#elif defined(linux) || defined(unix)
    sem_wait(&image_done_sem);
    if (temp_measure_on == 1)
    {
        sem_wait(&temp_done_sem);
    }
#endif

#ifdef INFO_LINE
#if defined(_WIN32)
    WaitForSingleObject(info_done_sem, INFINITE);	//waitting for info_line singnal
#elif defined(linux) || defined(unix)
    sem_wait(&info_done_sem);
#endif
#endif
    return;
}

void release_sem_after_streaming()
{
#if defined(_WIN32)
    ReleaseSemaphore(image_sem, 1, NULL);
#elif defined(linux) || defined(unix)
    sem_post(&image_sem);
#endif

#ifdef INFO_LINE
#if defined(_WIN32)
    ReleaseSemaphore(info_sem, 1, NULL);
#elif defined(linux) || defined(unix)
    sem_post(&info_sem);
#endif
#endif
    return;
}

void init_uvc_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    int ret = 0;
    char data_type[10];
    memset(data_type, 0, 10);
    IruvcCamStreamParams_t* params;
    params = (IruvcCamStreamParams_t*)(stream_frame_info->stream_config);
    params->usr_callback.iruvc_handle = (IruvcHandle_t*)(stream_frame_info->image_driver_handle);
    params->usr_callback.usr_func = NULL;
    params->usr_callback.usr_param = NULL;
    params->camera_param.width = stream_frame_info->product_config.camera.uvc_stream_conf.width;
    params->camera_param.height = stream_frame_info->product_config.camera.uvc_stream_conf.height;
    params->camera_param.frame_size = params->camera_param.width * params->camera_param.height
        * stream_frame_info->product_config.camera.uvc_stream_conf.frame_size_ratio;
    params->camera_param.fps = stream_frame_info->product_config.camera.uvc_stream_conf.fps;
    params->camera_param.timeout_ms_delay = 2000;
    FrameOutputFmt_t cur_fmt = stream_frame_info->frame_output_format;
    if (cur_fmt == YUYV_IMAGE || cur_fmt == YUYV_AND_TEMP || cur_fmt == UYVY_IMAGE)
    {
        strcpy(data_type, "YUYV");
    }
    else if (cur_fmt == NV12_IMAGE || cur_fmt == NV12_AND_TEMP)
    {
        strcpy(data_type, "NV12");
    }
    params->camera_param.format = data_type;
    ret = ir_image_video_handle->ir_video_start_stream(stream_frame_info->image_driver_handle, params);
    printf("width=%d, height=%d, frame_size=%d, fps=%d, format=%s\n", params->camera_param.width, params->camera_param.height, params->camera_param.frame_size, params->camera_param.fps, data_type);

    printf("start stream : %d\n", ret);
    printf("stream_frame_info->raw_byte_size = %d\n", stream_frame_info->raw_byte_size);

    return;
}

void destroy_uvc_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    IruvcStreamStopParams_t stop_params;
    memset(&stop_params, 0, sizeof(IruvcStreamStopParams_t));
    stop_params.stop_mode = CLOSE_CAM_SIDE_PREVIEW;
    ir_image_video_handle->ir_video_stop_stream(stream_frame_info->image_driver_handle, &stop_params);
    ir_image_video_handle->ir_video_release(stream_frame_info->image_driver_handle, &(stream_frame_info->image_dev_params));
    ir_image_video_handle->ir_video_close(stream_frame_info->image_driver_handle);
    ir_video_handle_delete(&ir_image_video_handle);

    return;
}

void* uvc_stream_function(void* threadarg)
{
    printf("stream_function start\n");
    StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    init_uvc_video_stream(stream_frame_info);
    uint8_t* yuyv_raw_frame = NULL;
    if (stream_frame_info->product_config.camera.format == UYVY_IMAGE)
    {
        yuyv_raw_frame = (uint8_t*)malloc(stream_frame_info->raw_byte_size);
        if (yuyv_raw_frame == NULL)
        {
            printf("there is no more space!\n");
            return NULL;
        }
    }

    while (isRUNNING)
    {
        wait_sem_for_streaming();
        //printf("111\n");
        ir_image_video_handle->ir_video_frame_get(stream_frame_info->image_driver_handle, NULL, \
            stream_frame_info->raw_frame, stream_frame_info->raw_byte_size); //raw_data
        if (stream_frame_info->product_config.camera.format == UYVY_IMAGE)
        {
            uyvy_to_yuyv(stream_frame_info->raw_frame, stream_frame_info->width, (stream_frame_info->image_info.height
                + stream_frame_info->information_line.height + stream_frame_info->temp_info.height + stream_frame_info->dummy_info.height), yuyv_raw_frame);
            memcpy(stream_frame_info->raw_frame, yuyv_raw_frame, stream_frame_info->raw_byte_size);
        }

        memcpy(stream_frame_info->image_info.data, stream_frame_info->raw_frame, \
            stream_frame_info->image_info.byte_size); //image data
        //printf("data=%d\n", stream_frame_info->image_info.data[0]);
        if (stream_frame_info->information_line.byte_size > 0)
        {
            memcpy(stream_frame_info->information_line.data, stream_frame_info->raw_frame + \
                stream_frame_info->image_info.byte_size, stream_frame_info->information_line.byte_size);//information line
            if (stream_frame_info->temp_info.byte_size > 0)
            {
                memcpy(stream_frame_info->temp_info.data, stream_frame_info->raw_frame + \
                    stream_frame_info->image_info.byte_size + stream_frame_info->information_line.byte_size, \
                    stream_frame_info->temp_info.byte_size); //temp data
            }
        }
        release_sem_after_streaming();
    }
    if (yuyv_raw_frame != NULL)
    {
        free(yuyv_raw_frame);
        yuyv_raw_frame = NULL;
    }
    destroy_uvc_video_stream(stream_frame_info);

    return NULL;
}

void stop_stream() {
    isRUNNING = false;
}
