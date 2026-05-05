#include "sample.h"

//void process_cb(double process_num)
//{
//    printf("process_cb:%.2f\n", process_num);
//}

void* process_cb(void* process_num, void* priv_data)
{
    printf("process_cb:%.2f %%\n", *((float*)process_num) * 100);
    return NULL;
}

void uart_firmware_update_demo(IrcmdHandle_t* cmd_handle)
{
    if (cmd_handle == NULL)
    {
        printf("cmd_handle is NULL\n");
        return;
    }

    char local_file_path[256];
    char basename[256];
    uint8_t* firmware_file_data;
    firmware_file_data = (uint8_t*)malloc(MAX_MALLOC_DATA_LENGTH * sizeof(uint8_t));
    if (firmware_file_data == NULL)
    {
        printf("there is no more space!\n");
        return;
    }
    int local_file_length = 0;
    FILE* fp = NULL;
    printf("please enter local firmware file path:\n");
    scanf("%s", local_file_path);

    int valid_flag;
    judge_path_valid(local_file_path, &valid_flag, basename);
    if (valid_flag == PATH_INVALID)
    {
        printf("local file path is invalid\n");
        free(firmware_file_data);
        return;
    }
    printf("basename = %s\n", basename);

    fp = fopen(local_file_path, "rb");
    if (fp == NULL)
    {
        printf("fail to open %s\n", local_file_path);
        free(firmware_file_data);
        return;
    }
    fseek(fp, 0, SEEK_END);
    local_file_length = ftell(fp);
    printf("local_file_length = %d\n", local_file_length);
    fseek(fp, 0, SEEK_SET);
    fread(firmware_file_data, 1, local_file_length, fp);
    fclose(fp);

    IrlibError_e ret;
    ircmd_init_callback_function(cmd_handle, 0, process_cb, NULL);
    ret = basic_firmware_download(cmd_handle, basename, firmware_file_data, local_file_length);
    ircmd_delete_callback_function(cmd_handle, 0);
    if (ret != IRLIB_SUCCESS)
    {
        printf("firmware_download failed\n");
        free(firmware_file_data);
        return;
    }

    free(firmware_file_data);
    return;
}

int main(void)
{
    printf("uart cmd sample start\n");
    printf("version:%s\n", iruart_version());
    printf("version:%s\n", ircmd_version());
    printf("version:%s\n", ircam_version());

    IrControlHandle_t* ir_control_handle = NULL;
    ir_control_handle_create(&ir_control_handle);
    IruartHandle_t* iruart_handle = NULL;
    iruart_handle = iruart_handle_create(ir_control_handle);

    IrcmdHandle_t* cmd_handle = NULL;

    int ret;
    int com_number = 0;
    ret = search_com(ir_control_handle, &cmd_handle, com_number);
    if (ret != 0)
    {
        printf("fail to find available com\n");
        return -1;
    }

    iruart_log_register(IRUART_LOG_DEBUG,NULL,NULL);
    ircmd_log_register(IRCMD_LOG_DEBUG,NULL,NULL);
    ircam_log_register(IRCAM_LOG_DEBUG,NULL,NULL);

    StreamFrameInfo_t stream_frame_info;
    stream_frame_info.ircmd_handle = cmd_handle;

    int cmd;
    char info_version[20] = { 0 };

    while (1)
    {
        printf("--------------please select cmd type--------------\n");
        printf("1:get_device_info\n");
        printf("2:basic_ffc_update\n");
        printf("3:palette_operate\n");
        printf("4:temp_measure\n");
        printf("5:data_update\n");
        printf("6:set_image_scene_mode\n");
        printf("7:image_effection\n");
        printf("8:firmware_upgrade\n");
        printf("--------------------------------------------------\n");
        scanf("%d", &cmd);
        if (cmd == 8)
        {
            uart_firmware_update_demo(cmd_handle);
        }
        else if(cmd < 8 && cmd > 0)
        {
            command_sel(cmd, &stream_frame_info);
        }
        else
        {
            printf("cmd is invalid\n");
            break;
        }
    }

    printf("sample done\n");
    getchar();
    return 0;
}