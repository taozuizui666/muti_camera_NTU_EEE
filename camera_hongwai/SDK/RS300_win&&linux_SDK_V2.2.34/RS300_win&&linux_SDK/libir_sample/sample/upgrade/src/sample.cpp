#define _CRT_SECURE_NO_WARNINGS
#include "sample.h"

void* upgrade_process(void* callback_data, void* priv_data)
{
    float process_schedule = *((float*)callback_data) * 100;
    if (priv_data == NULL)
    {
        printf("download percentage = %.2f %%\n", process_schedule);
        return NULL;
    }
    char* priv_data_ptr = (char *)priv_data;
    if ((int)process_schedule > strlen(priv_data_ptr))
    {
        int priv_data_idx = 0;
        for (; priv_data_idx < (int)process_schedule && priv_data_idx < 100; ++priv_data_idx)
        {
            priv_data_ptr[priv_data_idx] = '#';
        }
    }
    printf("\r[%-100s][%.2f%%]", priv_data_ptr, process_schedule);
    fflush(stdout);
    if (100 == (int)process_schedule)
    {
        memset(priv_data_ptr, '\0', strlen(priv_data_ptr));
        printf("\n");
    }
    return NULL;
}

bool download_upgrade_package_zip_demo(IrcmdHandle_t* ircmd_handle, char* dev_info)
{
    char upgrade_package_path[100] = { 0 };
    int ret;

    printf("please input upgrade package zip path:\n");
    scanf("%s", upgrade_package_path);

    char* priv_data = (char*)malloc(101);
    memset(priv_data, '\0', 101);
    ret = upgrade_package_function(ircmd_handle, dev_info, upgrade_package_path, NULL, upgrade_process, priv_data);
    free(priv_data);
    priv_data = NULL;
    if (ret != IRLIB_SUCCESS)
    {
        printf("upgrade_package_function failed\n");
        return false;
    }

    printf("upgrade_package_function success\n");
    return true;
}


/*******************************************
 * download single file
 *******************************************/

bool download_firmware_data_demo(IrcmdHandle_t* ircmd_handle, void* dev_param)
{
    int ret;
    ret = adv_device_close(ircmd_handle);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to close device\n");
    }

    ret = adv_device_open(ircmd_handle, dev_param);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to open device\n");
    }

    // int device_params[64] = { 0 };
    // device_params[0] = 115200;  //skip baud rate adapt
    // ret = adv_device_init(ircmd_handle, device_params);
    // if (ret != IRLIB_SUCCESS)
    // {
    //     printf("fail to init device\n");
    // }

    char firmware_path[512] = { 0 };
    printf("please input firmware data path:\n");
    scanf("%s", firmware_path);

    uint64_t file_length = 0;
    FILE* fp = NULL;
    fp = fopen(firmware_path, "rb");
    if (fp == NULL)
    {
        printf("firm path is invalid\n");
    }

    fseek(fp, 0, SEEK_END);
    file_length = ftell(fp);
    printf("firmware length is %lld\n", file_length);
    fseek(fp, 0, SEEK_SET);

    uint8_t* single_file = NULL;
    single_file = (uint8_t*)malloc((file_length + 1) * sizeof(uint8_t));
    if (single_file == NULL)
    {
        printf("there is no more space!\n");
        return false;
    }
    memset(single_file, 0, (file_length + 1) * sizeof(uint8_t));

    fread(single_file, 1, file_length, fp);
    fclose(fp);

    char* priv_data = (char*)malloc(101);
    memset(priv_data, '\0', 101);
    ircmd_init_callback_function(ircmd_handle, UPGRADE_PROCESS_CALLBACK, upgrade_process, priv_data);
    ret = basic_firmware_download(ircmd_handle, firmware_path, single_file, file_length);
    ircmd_delete_callback_function(ircmd_handle, UPGRADE_PROCESS_CALLBACK);
    free(priv_data);
    priv_data = NULL;
    if (ret != 0)
    {
        printf("====== fail to download firmware data ======\n");
        free(single_file);
        return false;
    }

    printf("====== succeed to download firmware data ======\n");
    free(single_file);
    return true;
}

bool download_single_file(IrcmdHandle_t* ircmd_handle)
{
    char file_name_in_device[256] = { 0 };
    char local_file_path[256] = { 0 };

    printf("please input target file name\n");
    scanf("%s", file_name_in_device);
    printf("please input local file path\n");
    scanf("%s", local_file_path);

    FILE* fp = NULL;
    fp = fopen(local_file_path, "rb");
    if (fp == NULL)
    {
        printf("fail to open file %s\n", local_file_path);
        return false;
    }

    uint32_t file_length = 0;
    fseek(fp, 0, SEEK_END);
    file_length = ftell(fp);
    printf("local file length is %lld\n", file_length);
    fseek(fp, 0, SEEK_SET);

    uint8_t* single_file = NULL;
    single_file = (uint8_t*)malloc((file_length + 1) * sizeof(uint8_t));
    if (single_file == NULL)
    {
        printf("there is no more space!\n");
        fclose(fp);
        return false;
    }

    fread(single_file, 1, file_length, fp);
    fclose(fp);

    int ret = 0;
    char* priv_data = (char*)malloc(101);
    memset(priv_data, '\0', 101);
    ret = single_file_write(ircmd_handle, file_name_in_device, single_file, \
        file_length, upgrade_process, priv_data);
    free(priv_data);
    priv_data = NULL;
    if (ret != IRLIB_SUCCESS)
    {
        printf("====== fail to write normal file ======\n");
        free(single_file);
        return false;
    }

    free(single_file);
    printf("====== succeed to write normal file ======\n");
    return true;
}

bool read_single_file(IrcmdHandle_t* ircmd_handle)
{
    char file_name_in_device[256] = { 0 };
    char local_file_path[256] = { 0 };

    printf("please input target file name\n");
    scanf("%s", file_name_in_device);
    printf("please input local file path\n");
    scanf("%s", local_file_path);

    int file_exist_flag = 0;
    int file_length = 0;
    FILE* fp = NULL;
#if defined(_WIN32)
    file_exist_flag = _access(local_file_path, 0);
#elif defined (unix) || (linux)
    file_exist_flag = access(local_file_path, 0);
#endif
    if (file_exist_flag >= 0)
    {
        fp = fopen(local_file_path, "rb");
        if (fp == NULL)
        {
            printf("fail to open the file!\n");
            return false;
        }
        fseek(fp, 0, SEEK_END);
        file_length = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        fclose(fp);

        if (file_length > 0)
        {
            printf("file is not empty, please modify local file path!\n");
            return false;
        }
    }

    uint8_t* single_file = NULL;
    single_file = (uint8_t*)malloc(SINGLE_FILE_MAX_LENGTH * sizeof(uint8_t));
    if (single_file == NULL)
    {
        printf("there is no more space!\n");
        return false;
    }

    file_length = 0;
    int ret;
    char* priv_data = (char*)malloc(101);
    memset(priv_data, '\0', 101);
    ret = single_file_read(ircmd_handle, file_name_in_device, single_file, &file_length, \
        upgrade_process, priv_data);
    free(priv_data);
    priv_data = NULL;
    if (ret != IRLIB_SUCCESS)
    {
        printf("====== fail to read single file ======\n");
        free(single_file);
        return false;
    }
    else
    {
        fp = fopen(local_file_path, "wb");
        if (fp == NULL)
        {
            printf("fail to open file %s\n", local_file_path);
            free(single_file);
            return false;
        }
        fwrite(single_file, 1, file_length, fp);
        fclose(fp);
    }

    printf("====== succeed to read single file ======\n");
    free(single_file);
    return true;
}

bool single_file_read_or_write(IrcmdHandle_t* ircmd_handle, void* dev_param)
{
    int ret;
    ret = adv_device_close(ircmd_handle);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to close device\n");
    }

    ret = adv_device_open(ircmd_handle, dev_param);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to open device\n");
        return false;
    }

    // int device_params[64] = { 0 };
    // device_params[0] = 115200;  //skip baud rate adapt
    // ret = adv_device_init(ircmd_handle, device_params);
    // if (ret != IRLIB_SUCCESS)
    // {
    //     printf("fail to init device\n");
    //     return false;
    // }

    int param;
    while (1)
    {
        printf("--------------------------please select download type--------------------------\n");
        printf("1:write single file\n");
        printf("2:read single file\n");
        printf("--------------------------------------------------------------------------\n");
        scanf("%d", &param);
        if (param > 2)
        {
            printf("you are exit single file download select\n");
            break;
        }

        switch (param)
        {
        case 1:
            ret = download_single_file(ircmd_handle);
            if (ret != true)
            {
                printf("fail to download single file\n");
            }
            break;
        case 2:
            ret = read_single_file(ircmd_handle);
            if (ret != true)
            {
                printf("fail to read single file\n");
            }
            break;
        default:
            printf("====== select is invalid ======\n");
            break;
        }
    }

    return true;
}

bool download_single_file_demo(IrcmdHandle_t* ircmd_handle, void* dev_param)
{
    int param;
    int ret;

    while (1)
    {
        printf("--------------------------please select download type--------------------------\n");
        printf("1:download firmware data\n");
        printf("2:write or read single file\n");
        printf("--------------------------------------------------------------------------\n");
        scanf("%d", &param);
        if (param > 2)
        {
            printf("you are exit single file download select\n");
            break;
        }
        switch (param)
        {
        case 1:
            ret = download_firmware_data_demo(ircmd_handle, dev_param);
            if (ret != true)
            {
                printf("fail to download firmware data\n");
            }
            break;
        case 2:
            ret = single_file_read_or_write(ircmd_handle, dev_param);
            if (ret != true)
            {
                printf("fail to read or write single file\n");
            }
            break;
        default:
            printf("====== select is invalid ======\n");
            break;
        }
    }

    return true;
}

int main(void)
{
    printf("upgrade sample start\n");

    ircmd_log_register(IRCMD_LOG_ERROR, NULL, NULL);
    iruart_log_register(IRUART_LOG_ERROR, NULL, NULL);
    //iri2c_log_register(IRI2C_LOG_ERROR, NULL, NULL);
    irupgrade_log_register(IRUPGRADE_LOG_DEBUG, NULL, NULL);
    //irdfu_log_register(IRDFU_LOG_DEBUG, NULL, NULL);

    IrControlHandle_t* ir_control_handle = NULL;
    ir_control_handle_create(&ir_control_handle);

    IruartHandle_t* uart_handle = NULL;
    uart_handle = iruart_handle_create(ir_control_handle);

    //Iri2cHandle_t* i2c_handle = NULL;
    //i2c_handle = iri2c_handle_create(ir_control_handle);

    //irdfu_handle_create(ir_control_handle);
    //IruvcHandle_t* uvc_handle = NULL;
    //uvc_handle = iruvc_usb_handle_create(ir_control_handle);

    IrcmdHandle_t* ircmd_handle = NULL;
    ircmd_handle = ircmd_create_handle(ir_control_handle);

    char dev_info[] = "ONLY_I2C:/dev/i2c-1;UVC_USB:pid=0x0020,vid=0x3474,sameidx=0;UART:\\\\.\\COM12;";//for windows
    //char dev_info[] = "ONLY_I2C:/dev/i2c-1;UVC_USB:pid=0x0020,vid=0x3474,sameidx=0;UART:/dev/ttyUSB0;";//for linux
    int ret;

    void* dev_param = NULL;
    int param;
    while (1)
    {
        printf("--------------------------please select download type--------------------------\n");
        printf("1:download upgrade package\n");
        printf("2:download single file\n");
        printf("-------------------------------------------------------------------------------\n");
        scanf("%d", &param);
        if (param > 2)
        {
            printf("you are exit download type select\n");
            break;
        }
        switch (param)
        {
        case 1:
            ret = download_upgrade_package_zip_demo(ircmd_handle, dev_info);
            if (ret != true)
            {
                printf("fail to use upgrade package zip to download\n");
            }
            break;
        case 2:
            int command_channel_type;
            ret = adv_command_channel_type_get(ircmd_handle, &command_channel_type);
            if (ret != IRLIB_SUCCESS)
            {
                printf("fail to get current command channel\n");
                return ret;
            }

            ret = parse_device_information((command_channel_e)command_channel_type, dev_info, &dev_param);
            if (ret != IRLIB_SUCCESS)
            {
                printf("fail to parse device information\n");
                return ret;
            }

            ret = download_single_file_demo(ircmd_handle, dev_param);
            if (ret != true)
            {
                printf("fail to single file to download\n");
            }
            free(dev_param);
            break;
        default:
            printf("====== select is invalid ======\n");
            break;
        }
    }


    ret = adv_device_close(ircmd_handle);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to close device\n");
    }

    ircmd_delete_handle(ircmd_handle);

    printf("upgrade sample done\n");
    return 0;
}
