#include "v4l2_camera.h"
#include <atomic>

std::atomic_bool isRUNNING(true);
IrVideoHandle_t* ir_image_video_handle = NULL;
IrVideoHandle_t* ir_temp_video_handle = NULL;

void wait_sem_for_streaming()
{
#if defined(_WIN32)
   WaitForSingleObject(image_done_sem, INFINITE);	//waitting for image singnal
#elif defined(linux) || defined(unix)
   sem_wait(&image_done_sem);
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

void release_temp_sem() {
#if defined(_WIN32)
    ReleaseSemaphore(i_temp_sem, 1, NULL);
#elif defined(linux) || defined(unix)
    sem_post(&i_temp_sem);
#endif
}

void wait_temp_sem_for_streaming() {
    #if defined(_WIN32)
    WaitForSingleObject(i_temp_sem, INFINITE);	//waitting for image singnal
#elif defined(linux) || defined(unix)
    sem_wait(&i_temp_sem);
#endif
}

void release_temp_done() {
#if defined(_WIN32)
    ReleaseSemaphore(i_temp_done, 1, NULL);
#elif defined(linux) || defined(unix)
    sem_post(&i_temp_done);
#endif
}

void wait_temp_done() {
    #if defined(_WIN32)
    WaitForSingleObject(i_temp_done, INFINITE);	//waitting for image singnal
#elif defined(linux) || defined(unix)
    sem_wait(&i_temp_done);
#endif
}

void init_v4l2_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    int ret;
    ret = ir_image_video_handle->ir_video_open(stream_frame_info->image_driver_handle, \
        (char*)stream_frame_info->image_name);
    printf("open video node: %d\n", ret);
    CamDevParams_t dev_params;
    stream_frame_info->image_dev_params = &dev_params;
    FrameOutputFmt_t cur_fmt = stream_frame_info->frame_output_format;
    dev_params.width = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_width;
    dev_params.format = V4L2_PIX_FMT_YUYV;  //we transfer the data by composite format, only use V4L2_PIX_FMT_YUYV for 2 bytes transfer.
    dev_params.fps = stream_frame_info->product_config.camera.v4l2_config.image_stream.fps;
    dev_params.height = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_height;

    CamStreamParams_t stream_params;
    memset(&stream_params, 0, sizeof(stream_params));
    stream_params.width = dev_params.width;
    stream_params.height = dev_params.height;
    ir_image_video_handle->ir_video_init(stream_frame_info->image_driver_handle, &dev_params);
    ir_image_video_handle->ir_video_start_stream(stream_frame_info->image_driver_handle, &stream_params);
    printf("irv4l2_camera_start_stream done\n");

    return;
}

void destroy_v4l2_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    CamStreamParams_t stream_params;
    CamDevParams_t* image_dev_param = (CamDevParams_t*)(stream_frame_info->image_dev_params);
    memset(&stream_params, 0, sizeof(stream_params));
    stream_params.width = image_dev_param->width;
    stream_params.height = image_dev_param->height;
    ir_image_video_handle->ir_video_stop_stream(stream_frame_info->image_driver_handle, &stream_params);
    ir_image_video_handle->ir_video_release(stream_frame_info->image_driver_handle, image_dev_param);
    ir_image_video_handle->ir_video_close(stream_frame_info->image_driver_handle);
    ir_video_handle_delete(&ir_image_video_handle);

    return;
}

void* v4l2_stream_function(void* threadarg)
{
    StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    init_v4l2_video_stream(stream_frame_info);

    uint8_t* yuyv_frame = NULL;
    uint8_t* nv16_frame = NULL;
    if (stream_frame_info->product_config.camera.image_channel_type == "dvp")
    {
        nv16_frame = (uint8_t*)malloc(stream_frame_info->raw_byte_size);
        if (nv16_frame == NULL)
        {
            printf("there is no more space!\n");
            return NULL;
        }
    }
    if (stream_frame_info->product_config.camera.format == UYVY_IMAGE)
    {
        yuyv_frame = (uint8_t*)malloc(stream_frame_info->raw_byte_size);
        if (yuyv_frame == NULL)
        {
            printf("there is no more space!\n");
            return NULL;
        }
    }

    while (isRUNNING)
    {
        wait_sem_for_streaming();
        if (stream_frame_info->product_config.camera.image_channel_type != "dvp")
        {
            ir_image_video_handle->ir_video_frame_get(stream_frame_info->image_driver_handle, NULL, \
                stream_frame_info->raw_frame, stream_frame_info->raw_byte_size); //raw_data
        }
        else
        {
            ir_image_video_handle->ir_video_frame_get(stream_frame_info->image_driver_handle, NULL, \
                    nv16_frame, stream_frame_info->raw_byte_size); //raw_data
            if (stream_frame_info->product_config.camera.format == YUYV_IMAGE || stream_frame_info->product_config.camera.format == YUYV_AND_TEMP)
            {
                nv16_to_yuyv(nv16_frame, stream_frame_info->width, (stream_frame_info->image_info.height + stream_frame_info->information_line.height
                    + stream_frame_info->temp_info.height + stream_frame_info->dummy_info.height), stream_frame_info->raw_frame);
            }
            else if (stream_frame_info->product_config.camera.format == UYVY_IMAGE)
            {
                nv16_to_yuyv(nv16_frame, stream_frame_info->width, (stream_frame_info->image_info.height + stream_frame_info->information_line.height
                    + stream_frame_info->temp_info.height + stream_frame_info->dummy_info.height), yuyv_frame);
                uyvy_to_yuyv(yuyv_frame, stream_frame_info->width, (stream_frame_info->image_info.height + stream_frame_info->information_line.height
                    + stream_frame_info->temp_info.height + stream_frame_info->dummy_info.height), stream_frame_info->raw_frame);
            }
        }

        memcpy(stream_frame_info->image_info.data, stream_frame_info->raw_frame, \
            stream_frame_info->image_info.byte_size); //image data
        memcpy(stream_frame_info->information_line.data, stream_frame_info->raw_frame + \
            stream_frame_info->image_info.byte_size, stream_frame_info->information_line.byte_size); //information line
        memcpy(stream_frame_info->temp_info.data, stream_frame_info->raw_frame + \
            stream_frame_info->image_info.byte_size + stream_frame_info->information_line.byte_size, \
            stream_frame_info->temp_info.byte_size); //temp data
        release_sem_after_streaming();
    }

    if (nv16_frame != NULL)
	{
		free(nv16_frame);
		nv16_frame = NULL;
	}
    if (yuyv_frame != NULL)
	{
		free(yuyv_frame);
		yuyv_frame = NULL;
	}

    destroy_v4l2_video_stream(stream_frame_info);

    return NULL;
}

void init_double_channel_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    int ret;
    ret = ir_image_video_handle->ir_video_open(stream_frame_info->image_driver_handle, (char*)stream_frame_info->image_name);
    printf("open image node:%d\n", ret);

    ret = ir_temp_video_handle->ir_video_open(stream_frame_info->temp_driver_handle, (char*)stream_frame_info->temp_name);
    printf("open temp node:%d\n", ret);

    CamDevParams_t image_dev_params, temp_dev_params;
    memset(&image_dev_params, 0, sizeof(image_dev_params));
    memset(&temp_dev_params, 0, sizeof(temp_dev_params));

    stream_frame_info->image_dev_params = &image_dev_params;
    stream_frame_info->temp_dev_params = &temp_dev_params;

    FrameOutputFmt_t cur_fmt = stream_frame_info->frame_output_format;
    image_dev_params.width = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_width;
    image_dev_params.format = V4L2_PIX_FMT_YUYV;  //we transfer the data by composite format, only use V4L2_PIX_FMT_YUYV for 2 bytes transfer.
    image_dev_params.fps = stream_frame_info->product_config.camera.v4l2_config.image_stream.fps;

    temp_dev_params.width = stream_frame_info->product_config.camera.v4l2_config.temp_stream.dev_width;
    temp_dev_params.format = V4L2_PIX_FMT_YUYV;  //we transfer the data by composite format, only use V4L2_PIX_FMT_YUYV for 2 bytes transfer.
    temp_dev_params.fps = stream_frame_info->product_config.camera.v4l2_config.temp_stream.fps;

    image_dev_params.height = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_height;
    temp_dev_params.height = stream_frame_info->product_config.camera.v4l2_config.temp_stream.dev_height;


    CamStreamParams_t image_stream_params, temp_stream_params;
    memset(&image_stream_params, 0, sizeof(image_stream_params));
    memset(&temp_stream_params, 0, sizeof(temp_stream_params));

    printf("start image stream\n");
    image_stream_params.width = image_dev_params.width;
    image_stream_params.height = image_dev_params.height;
    ir_image_video_handle->ir_video_init(stream_frame_info->image_driver_handle, &image_dev_params);
    ir_image_video_handle->ir_video_start_stream(stream_frame_info->image_driver_handle, &image_stream_params);

    printf("start temp stream\n");
    temp_stream_params.width = temp_dev_params.width;
    temp_stream_params.height = temp_dev_params.height;
    ir_temp_video_handle->ir_video_init(stream_frame_info->temp_driver_handle, &temp_dev_params);
    ir_temp_video_handle->ir_video_start_stream(stream_frame_info->temp_driver_handle, &temp_dev_params);
    printf("irv4l2_camera_start_stream done\n");

    return;
}

void destroy_double_channel_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    CamDevParams_t* image_dev_params = (CamDevParams_t*)(stream_frame_info->image_dev_params);
    CamDevParams_t* temp_dev_params = (CamDevParams_t*)(stream_frame_info->temp_dev_params);

    CamStreamParams_t image_stream_params;
    memset(&image_stream_params, 0, sizeof(image_stream_params));
    image_stream_params.width = image_dev_params->width;
    image_stream_params.height = image_dev_params->height;

    CamStreamParams_t temp_stream_params;
    memset(&temp_stream_params, 0, sizeof(temp_stream_params));
    temp_stream_params.width = temp_dev_params->width;
    temp_stream_params.height = temp_dev_params->height;

    printf("stop image stream\n");
    ir_image_video_handle->ir_video_stop_stream(stream_frame_info->image_driver_handle, &image_stream_params);
    ir_image_video_handle->ir_video_release(stream_frame_info->image_driver_handle, &image_dev_params);
    ir_image_video_handle->ir_video_close(stream_frame_info->image_driver_handle);
    ir_video_handle_delete(&ir_image_video_handle);

    printf("stop temp stream\n");
    ir_temp_video_handle->ir_video_stop_stream(stream_frame_info->temp_driver_handle, &temp_stream_params);
    ir_temp_video_handle->ir_video_release(stream_frame_info->temp_driver_handle, &temp_dev_params);
    ir_temp_video_handle->ir_video_close(stream_frame_info->temp_driver_handle);
    ir_video_handle_delete(&ir_temp_video_handle);

    return;
}

void* v4l2_double_channel_stream_function(void* threadarg)
{
    StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    init_double_channel_video_stream(stream_frame_info);

    uint32_t image_data_byte = ((CamDevParams_t*)stream_frame_info->image_dev_params)->height \
        * ((CamDevParams_t*)stream_frame_info->image_dev_params)->width * 2;
    uint32_t temp_data_byte = ((CamDevParams_t*)stream_frame_info->temp_dev_params)->height \
        * ((CamDevParams_t*)stream_frame_info->temp_dev_params)->width * 2;

    while (isRUNNING)
    {
        wait_sem_for_streaming();

        ir_image_video_handle->ir_video_frame_get(stream_frame_info->image_driver_handle, NULL, \
            stream_frame_info->raw_frame, image_data_byte);
        memcpy(stream_frame_info->image_info.data, stream_frame_info->raw_frame, \
            stream_frame_info->image_info.byte_size); //image data
        memcpy(stream_frame_info->information_line.data, stream_frame_info->raw_frame + stream_frame_info->image_info.byte_size, \
            stream_frame_info->information_line.byte_size); //temp data

        ir_temp_video_handle->ir_video_frame_get(stream_frame_info->temp_driver_handle, NULL, \
            stream_frame_info->raw_temp_frame, temp_data_byte);
        memcpy(stream_frame_info->temp_info.data, stream_frame_info->raw_temp_frame, \
            stream_frame_info->temp_info.byte_size); //temp data

        release_sem_after_streaming();
    }

    destroy_double_channel_video_stream(stream_frame_info);

    return NULL;
}

void destroy_temp_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    CamDevParams_t* temp_dev_params = (CamDevParams_t*)(stream_frame_info->temp_dev_params);

    CamStreamParams_t temp_stream_params;
    memset(&temp_stream_params, 0, sizeof(temp_stream_params));
    temp_stream_params.width = temp_dev_params->width;
    temp_stream_params.height = temp_dev_params->height;

    printf("stop temp stream\n");
    ir_temp_video_handle->ir_video_stop_stream(stream_frame_info->temp_driver_handle, &temp_stream_params);
    ir_temp_video_handle->ir_video_release(stream_frame_info->temp_driver_handle, &temp_dev_params);
    ir_temp_video_handle->ir_video_close(stream_frame_info->temp_driver_handle);
    ir_video_handle_delete(&ir_temp_video_handle);

    return;
}

void init_temp_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    int ret;
    ret = ir_temp_video_handle->ir_video_open(stream_frame_info->temp_driver_handle, (char*)stream_frame_info->temp_name);
    printf("open temp node:%d\n", ret);

    CamDevParams_t temp_dev_params;
    memset(&temp_dev_params, 0, sizeof(temp_dev_params));

    stream_frame_info->temp_dev_params = &temp_dev_params;

    FrameOutputFmt_t cur_fmt = stream_frame_info->frame_output_format;

    temp_dev_params.width = stream_frame_info->product_config.camera.v4l2_config.temp_stream.dev_width;
    temp_dev_params.format = V4L2_PIX_FMT_YUYV;  //we transfer the data by composite format, only use V4L2_PIX_FMT_YUYV for 2 bytes transfer.
    temp_dev_params.fps = stream_frame_info->product_config.camera.v4l2_config.temp_stream.fps;
    temp_dev_params.height = stream_frame_info->product_config.camera.v4l2_config.temp_stream.dev_height;

    CamStreamParams_t temp_stream_params;
    memset(&temp_stream_params, 0, sizeof(temp_stream_params));

    printf("start temp stream\n");
    temp_stream_params.width = temp_dev_params.width;
    temp_stream_params.height = temp_dev_params.height;
    ir_temp_video_handle->ir_video_init(stream_frame_info->temp_driver_handle, &temp_dev_params);
    ir_temp_video_handle->ir_video_start_stream(stream_frame_info->temp_driver_handle, &temp_dev_params);
    printf("irv4l2_camera_start_stream done\n");

    return;
}

void* v4l2_temp_channel_stream_function(void* threadarg) {
    StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    init_temp_video_stream(stream_frame_info);

    uint32_t temp_data_byte = ((CamDevParams_t*)stream_frame_info->temp_dev_params)->height \
        * ((CamDevParams_t*)stream_frame_info->temp_dev_params)->width * 2;

    while (isRUNNING)
    {
        wait_temp_sem_for_streaming();
        ir_temp_video_handle->ir_video_frame_get(stream_frame_info->temp_driver_handle, NULL, \
            stream_frame_info->raw_temp_frame, temp_data_byte);

        memcpy(stream_frame_info->temp_info.data, stream_frame_info->raw_temp_frame, \
            stream_frame_info->temp_info.byte_size); //temp data
        memcpy(stream_frame_info->dummy_info.data, stream_frame_info->raw_temp_frame + stream_frame_info->temp_info.byte_size,
            stream_frame_info->dummy_info.byte_size);
        release_temp_done();
    }

    destroy_temp_video_stream(stream_frame_info);

    return NULL;
}

void destroy_image_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    CamDevParams_t* image_dev_params = (CamDevParams_t*)(stream_frame_info->image_dev_params);

    CamStreamParams_t image_stream_params;
    memset(&image_stream_params, 0, sizeof(image_stream_params));
    image_stream_params.width = image_dev_params->width;
    image_stream_params.height = image_dev_params->height;

    printf("stop image stream\n");
    ir_image_video_handle->ir_video_stop_stream(stream_frame_info->image_driver_handle, &image_stream_params);
    ir_image_video_handle->ir_video_release(stream_frame_info->image_driver_handle, &image_dev_params);
    ir_image_video_handle->ir_video_close(stream_frame_info->image_driver_handle);
    ir_video_handle_delete(&ir_image_video_handle);

    return;
}

void init_image_video_stream(StreamFrameInfo_t* stream_frame_info)
{
    int ret;
    ret = ir_image_video_handle->ir_video_open(stream_frame_info->image_driver_handle, (char*)stream_frame_info->image_name);
    printf("open image node:%d\n", ret);

    CamDevParams_t image_dev_params;
    memset(&image_dev_params, 0, sizeof(image_dev_params));

    stream_frame_info->image_dev_params = &image_dev_params;

    FrameOutputFmt_t cur_fmt = stream_frame_info->frame_output_format;
    image_dev_params.width = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_width;
    image_dev_params.format = V4L2_PIX_FMT_YUYV;  //we transfer the data by composite format, only use V4L2_PIX_FMT_YUYV for 2 bytes transfer.
    image_dev_params.fps = stream_frame_info->product_config.camera.v4l2_config.image_stream.fps;
    image_dev_params.height = stream_frame_info->product_config.camera.v4l2_config.image_stream.dev_height;

    CamStreamParams_t image_stream_params;
    memset(&image_stream_params, 0, sizeof(image_stream_params));

    printf("start image stream\n");
    image_stream_params.width = image_dev_params.width;
    image_stream_params.height = image_dev_params.height;
    ir_image_video_handle->ir_video_init(stream_frame_info->image_driver_handle, &image_dev_params);
    ir_image_video_handle->ir_video_start_stream(stream_frame_info->image_driver_handle, &image_stream_params);
    printf("irv4l2_camera_start_stream done\n");

    return;
}

void* v4l2_image_channel_stream_function(void* threadarg) {
StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    init_image_video_stream(stream_frame_info);

    uint32_t image_data_byte = ((CamDevParams_t*)stream_frame_info->image_dev_params)->height \
        * ((CamDevParams_t*)stream_frame_info->image_dev_params)->width * 2;

    while (isRUNNING)
    {
        wait_sem_for_streaming();
        release_temp_sem();

        ir_image_video_handle->ir_video_frame_get(stream_frame_info->image_driver_handle, NULL, \
            stream_frame_info->raw_frame, image_data_byte);
 	    memcpy(stream_frame_info->image_info.data, stream_frame_info->raw_frame, \
             stream_frame_info->image_info.byte_size); //image data
        memcpy(stream_frame_info->information_line.data, stream_frame_info->raw_frame + stream_frame_info->image_info.byte_size, \
            stream_frame_info->information_line.byte_size);
        wait_temp_done();
        release_sem_after_streaming();
    }

    destroy_image_video_stream(stream_frame_info);
    return NULL;
}


void stop_stream()
{
    isRUNNING = false;
}
