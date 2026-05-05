#include "info_parse.h"

void* info_line_parse_function(void* threadarg)
{
    printf("info_line_parse_function start\n");
    StreamFrameInfo_t* stream_frame_info;
    stream_frame_info = (StreamFrameInfo_t*)threadarg;
    uint8_t* info_line_data = NULL;
	IrinfoStatusInfo_t status_info;
	IrinfoTestInfo_t test_info;
	IrinfoFuncInfo_t function_info;
	FILE* fp = NULL;
	uint16_t crc_calc_value = 0;
	if (stream_frame_info == NULL)
	{
		return NULL;
	}
	fp = fopen("info_line.txt", "w+t");
    while (isRUNNING)
    {
        if ((stream_frame_info->information_line.byte_size == 0) || (stream_frame_info->information_line.data == NULL))
        {
			printf("No current information line\n");
            break;
        }
#if defined(_WIN32)
        WaitForSingleObject(info_sem, INFINITE);	//waitting for info_line signal
#elif defined(linux) || defined(unix)
        sem_wait(&info_sem);
#endif
        irinfoparse_get_irinfo_status_info(stream_frame_info->information_line.data, &status_info);
		irinfoparse_get_irinfo_function_info(stream_frame_info->information_line.data, &function_info);
		printf( "----------------------------------------------\n");
   		printf("frame_fps = %d,width = %d,height = %d\n",\
			status_info.frame_status.frame_fps,function_info.device_info.width,function_info.device_info.height);
    	printf( "frame_count = %d\n",status_info.frame_status.frame_count);
		printf( "frame_type = %d\n",status_info.frame_status.frame_type);
		printf( "flip_status = %d\n",status_info.image_status.flip_status);
		printf( "temp_type = %d\n",status_info.tpd_status.temp_type);
		printf( "dev_pn = %s\n",function_info.device_info.dev_pn);
		printf( "------------------------------------------------\n");
		memset(stream_frame_info->information_line.data, 0, stream_frame_info->information_line.byte_size);
#if defined(_WIN32)
        ReleaseSemaphore(info_done_sem, 1, NULL);
#elif defined(linux) || defined(unix)
        sem_post(&info_done_sem);
#endif
    }
	fclose(fp);
    return NULL;
}