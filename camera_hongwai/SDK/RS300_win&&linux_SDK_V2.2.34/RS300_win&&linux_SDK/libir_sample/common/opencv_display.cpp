#define _CRT_SECURE_NO_WARNINGS
#include "opencv_display.h"

#if defined (_WIN32)
#define DELTA_EPOCH_IN_MICROSECS  116444736000000000Ui64
// gettimeofday - get time of day for Windows;
// A gettimeofday implementation for Microsoft Windows;
// Public domain code, author "ponnada";
int gettimeofday(struct timeval* tv, struct timezone* tz)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag = 0;
    if (NULL != tv)
    {
        GetSystemTimeAsFileTime(&ft);
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
        tmpres /= 10;
        tmpres -= DELTA_EPOCH_IN_MICROSECS;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }
    return 0;
}
#endif // Windows

void display_one_frame(uint8_t* frame, uint16_t width, uint16_t height, const char* title)
{
    struct timeval image_now_time, temp_now_time;
    static struct timeval image_last_time, temp_last_time;
    float fps = 0;
    if (!strcmp("image", title))
    {
        gettimeofday(&image_now_time, NULL);
        fps = 1000000 / (float)((image_now_time.tv_sec - image_last_time.tv_sec) * 1000000 + \
            (image_now_time.tv_usec - image_last_time.tv_usec));
        image_last_time = image_now_time;
    }
    if (!strcmp("temp", title))
    {
        temp_now_time = image_last_time;
        fps = 1000000 / (float)((temp_now_time.tv_sec - temp_last_time.tv_sec) * 1000000 + \
            (temp_now_time.tv_usec - temp_last_time.tv_usec));
        temp_last_time = temp_now_time;
    }
    
    char frameText[10] = { " " };
    sprintf(frameText, "%.2f", fps);

    cv::Mat image = cv::Mat(height, width, CV_8UC3, frame);
    putText(image, frameText, cv::Point(11, 11), cv::FONT_HERSHEY_PLAIN, 1, cv::Scalar::all(0), 1, 8);
    putText(image, frameText, cv::Point(10, 10), cv::FONT_HERSHEY_PLAIN, 1, cv::Scalar::all(255), 1, 8);
    //cv::namedWindow(title, CV_WINDOW_NORMAL);
    cv::imshow(title, image);         //显示原图
    cvWaitKey(5);
}

void* opencv_display_function(void* threadarg)
{
    if (threadarg == NULL)
    {
        printf("data is NULL\n");
        return NULL;
    }

    StreamFrameInfo_t* stream_frame_info = (StreamFrameInfo_t*)threadarg;

    uint8_t* rgb_image_frame = NULL;
    rgb_image_frame = (uint8_t*)malloc(stream_frame_info->width * stream_frame_info->height * 3);
    if (rgb_image_frame == NULL)
    {
        printf("there is no more space!\n");
        return NULL;
    }
    uint8_t* bgr_image_frame = NULL;
    bgr_image_frame = (uint8_t*)malloc(stream_frame_info->width * stream_frame_info->height * 3);
    if (bgr_image_frame == NULL)
    {
        printf("there is no more space!\n");
        return NULL;
    }

    while (isRUNNING)
    {
#if defined(_WIN32)
        WaitForSingleObject(image_sem, INFINITE);
#elif defined (linux)||(unix)
        sem_wait(&image_sem);
#endif
        if ((stream_frame_info->frame_output_format == YUYV_IMAGE) || (stream_frame_info->frame_output_format == YUYV_AND_TEMP)
            || (stream_frame_info->frame_output_format == UYVY_IMAGE))
        {
            yuv422_to_rgb(stream_frame_info->image_info.data, (stream_frame_info->width*stream_frame_info->height), rgb_image_frame);
            rgb_to_bgr(rgb_image_frame, (stream_frame_info->width * stream_frame_info->height), bgr_image_frame);
        }
        if ((stream_frame_info->frame_output_format == NV12_IMAGE) || (stream_frame_info->frame_output_format == NV12_AND_TEMP))
        {
            nv12_to_rgb(stream_frame_info->image_info.data, stream_frame_info->width, stream_frame_info->height, rgb_image_frame);
            rgb_to_bgr(rgb_image_frame, (stream_frame_info->width * stream_frame_info->height), bgr_image_frame);
        }
        display_one_frame(bgr_image_frame, stream_frame_info->width, stream_frame_info->height, "image");


        if ((stream_frame_info->frame_output_format == YUYV_AND_TEMP) || (stream_frame_info->frame_output_format == NV12_AND_TEMP))
        {
            y16_to_rgb((uint16_t*)(stream_frame_info->temp_info.data), stream_frame_info->width, stream_frame_info->height, rgb_image_frame);
            rgb_to_bgr(rgb_image_frame, (stream_frame_info->width * stream_frame_info->height), bgr_image_frame);
            display_one_frame(bgr_image_frame, stream_frame_info->width, stream_frame_info->height, "temp");
        }

#if defined(_WIN32)
        ReleaseSemaphore(image_done_sem, 1, NULL);
#elif defined (linux)||(unix)
        sem_post(&image_done_sem);
#endif
    }

    if (rgb_image_frame != NULL)
    {
        free(rgb_image_frame);
        rgb_image_frame = NULL;
    }

    if (bgr_image_frame != NULL)
    {
        free(bgr_image_frame);
        bgr_image_frame = NULL;
    }

    printf("display thread exit!!\n");
    return NULL;
}