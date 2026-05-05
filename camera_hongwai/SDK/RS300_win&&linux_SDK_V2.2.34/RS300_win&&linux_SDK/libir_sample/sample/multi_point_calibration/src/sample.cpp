#include "sample.h"


void* upgrade_process(void* callback_data, void* priv_data)
{
    printf("download percentage = %.2f %%\n", *((float*)callback_data) * 100);
    return NULL;
}

void find_text_in_brackets(const char* pn, char* text)
{
    const char* start = strrchr(pn, '[');
    const char* end = strrchr(pn, ']');

    if (start != NULL && end != NULL && (end > start + 1))
    {
        const char* text_start = start + 1;
        size_t text_size = end - text_start;
        memcpy(text, text_start, text_size);
        text[text_size] = '\0';
    }
}

int read_file(IrcmdHandle_t* ircmd_handle, int gain_mode, int file_type, uint16_t* org_table)
{
    if (ircmd_handle == NULL || org_table == NULL)
    {
        printf("ircmd_handle or nuc_table is NULL!\n");
        return -1;
    }

    if (gain_mode != HIGH_GAIN && gain_mode != LOW_GAIN)
    {
        printf("gain_mode is invalid!\n");
        return -1;
    }

    if (file_type != KT && file_type != BT && file_type != CORRECT && file_type != NUC_T)
    {
        printf("file_type is invalid!\n");
        return -1;
    }

    char file_name_in_device[256] = { 0 };
    void* priv_data = NULL;
    int ret;
    int file_length = 0;

    if (file_type == NUC_T)
    {
        file_length = ADVANCED_NUC_T_SIZE * 2;
        if (gain_mode == HIGH_GAIN)
        {
            char file_name_in_device_temp[] = { "default_data/high_gain/tpd_nuc_t.bin" };
            memcpy(file_name_in_device, file_name_in_device_temp, sizeof(file_name_in_device_temp));
        }
        if (gain_mode == LOW_GAIN)
        {
            char file_name_in_device_temp[] = { "default_data/low_gain/tpd_nuc_t.bin" };
            memcpy(file_name_in_device, file_name_in_device_temp, sizeof(file_name_in_device_temp));
        }
    }

    if (file_type == CORRECT)
    {
        file_length = CORRECT_TABLE_SIZE * 2;
        char pn_data[100] = { 0 };
        ret = basic_device_info_get(ircmd_handle, BASIC_DEV_INFO_GET_PN, pn_data);
        printf("ret = %d\n", ret);
        printf("PN is: %s\n", pn_data);

        //Find the data in [] from the last part in the PN
        char text[10] = { 0 };
        find_text_in_brackets(pn_data, text);
        if (strlen(text)==0)
        {
            printf("text in [] is empty, use F1\n");
            memcpy(text, "F1", 3);
        }

        if (gain_mode == HIGH_GAIN)
        {
            sprintf(file_name_in_device, "system_data/tpd_dis_table/high%s.bin", text);
        }
        else if (gain_mode == LOW_GAIN)
        {
            sprintf(file_name_in_device, "system_data/tpd_dis_table/low%s.bin", text);
        }
        printf("correct table file name:%s\n", file_name_in_device);
    }

    uint8_t* single_file = NULL;
    single_file = (uint8_t*)malloc(file_length * sizeof(uint8_t));
    if (single_file == NULL)
    {
        printf("there is no more space!\n");
        return -1;
    }

    ret = single_file_read(ircmd_handle, file_name_in_device, single_file, &file_length, \
        upgrade_process, priv_data);
    if (ret != IRLIB_SUCCESS)
    {
        printf("====== fail to read single file ======\n");
        free(single_file);
        return -1;
    }
    memcpy(org_table, single_file, file_length);
    printf("====== succeed to read single file ======\n");
    free(single_file);
    return 0;
}

int download_file(IrcmdHandle_t* ircmd_handle, int gain_mode, int file_type,uint16_t* target_table)
{
    if (ircmd_handle == NULL || target_table == NULL)
    {
        printf("ircmd_handle or nuc_table is NULL!\n");
        return -1;
    }

    if (gain_mode != HIGH_GAIN && gain_mode != LOW_GAIN)
    {
        printf("gain_mode is invalid!\n");
        return -1;
    }

    if (file_type != KT && file_type != BT && file_type != CORRECT && file_type != NUC_T)
    {
        printf("file_type is invalid!\n");
        return -1;
    }

    int ret = 0;
    int file_length = 0;
    char file_name_in_device[256] = { 0 };
    void* priv_data = NULL;

    if (file_type == NUC_T)
    {
        file_length = ADVANCED_NUC_T_SIZE * 2;
        if (gain_mode == HIGH_GAIN)
        {
            char file_name_in_device_temp[] = { "secd_cali_data/high_gain/tpd_nuc_t.bin" };
            memcpy(file_name_in_device, file_name_in_device_temp, sizeof(file_name_in_device_temp));
        }
        if (gain_mode == LOW_GAIN)
        {
            char file_name_in_device_temp[] = { "secd_cali_data/low_gain/tpd_nuc_t.bin" };
            memcpy(file_name_in_device, file_name_in_device_temp, sizeof(file_name_in_device_temp));
        }
    }

    uint8_t* single_file = NULL;
    single_file = (uint8_t*)malloc(file_length * sizeof(uint8_t));
    if (single_file == NULL)
    {
        printf("there is no more space!\n");
        return -1;
    }
    memcpy(single_file, (uint8_t*)target_table, file_length);

    ret = single_file_write(ircmd_handle, file_name_in_device, single_file, \
        file_length, upgrade_process, priv_data);
    if (ret != IRLIB_SUCCESS)
    {
        printf("====== fail to write normal file ======\n");
        free(single_file);
        return false;
    }
    free(single_file);
    printf("====== succeed to write normal file ======\n");
    return 0;
}

int main(void)
{
    printf("multi point calibration sample start\n");
    printf("version:%s\n", iruart_version());
    printf("version:%s\n", ircmd_version());
    printf("version:%s\n", ircam_version());

    iruart_log_register(IRUART_LOG_ERROR, NULL, NULL);
    ircmd_log_register(IRCMD_LOG_ERROR, NULL, NULL);
    ircam_log_register(IRCAM_LOG_ERROR, NULL, NULL);
    irtemp_log_register(IRTEMP_LOG_ERROR);

    IrControlHandle_t* ir_control_handle = NULL;
    ir_control_handle_create(&ir_control_handle);

    IruartHandle_t* iruart_handle = NULL;
    iruart_handle = iruart_handle_create(ir_control_handle);
    if (iruart_handle == NULL)
    {
        printf("fail to create iruart_handle\n");
        return -1;
    }

    IrcmdHandle_t* ircmd_handle = NULL;
    ircmd_handle = ircmd_create_handle(ir_control_handle);
    //for windows
    /*char dev_info[] = "ONLY_I2C:/dev/i2c-1;UVC_USB:pid=0x0001,vid=0x3474,sameidx=0;UART:\\\\.\\COM18;";*/
    //for linux
    char dev_info[] = "ONLY_I2C:/dev/i2c-1;UVC_USB:pid=0x0001,vid=0x3474,sameidx=0;UART:/dev/ttyUSB0;";
    int ret;

    int command_channel_type;
    ret = adv_command_channel_type_get(ircmd_handle, &command_channel_type);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to get current command channel\n");
        return ret;
    }

    void* dev_param = NULL;
    ret = parse_device_information((command_channel_e)command_channel_type, dev_info, &dev_param);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to parse device information\n");
        return ret;
    }

    ret = adv_device_open(ircmd_handle, dev_param);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to open device\n");
        return ret;
    }

    int device_params[64] = { 0 };
    device_params[0] = 115200;
    ret = adv_device_init(ircmd_handle, device_params);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to init device\n");
    }

    //init information for multi point calib
    int gain_mode = -1;
    printf("-------------------please choose the number of gain mode-------------------\n");
    printf("0:low gain\n");
    printf("1:high gain\n");
    printf("--------------------------------------------------------------------------\n");
    scanf("%d", &gain_mode);

    float ems[4] = { 1, 1, 0.95, 1 };
    float hum[4] = { 0.45, 0.45, 0.45, 0.45 };
    float dist[4] = { 0.25, 3, 3, 4 };
    float ta[4] = { 25, 25, 25, 25 };
    float tu[4] = { 25, 25, 25, 25 };
    EnvCorrectParam env_cor_param[] = {
    { dist[0], ems[0], hum[0], ta[0], tu[0] }, \
    { dist[1], ems[1], hum[1], ta[1], tu[1] }, \
    { dist[2], ems[2], hum[2], ta[2], tu[2] }, \
    { dist[3], ems[3], hum[3], ta[3], tu[3] } };

    MultiPointCalibNuc_t multi_point_nuc[] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };

    MultiPointCalibTemp_t multi_point_temp[] = { { 4722 / 16.0 - 273.15, 20  },\
    { 5602 / 16.0 - 273.15, 80  }, { 6122 / 16.0 - 273.15, 120 }, { 6500 / 16.0 - 273.15, 140 } };

    uint16_t org_nuc_table[ADVANCED_NUC_T_SIZE] = { 0 };
    uint16_t new_nuc_table[ADVANCED_NUC_T_SIZE] = { 0 };
    uint16_t correct_table[CORRECT_TABLE_SIZE] = { 0 };

#if defined MULTI_POINT_CALIBRATION
    ret = adv_second_cali_flag_set(ircmd_handle, ADV_SECOND_CALI_FLAG_NUC_T, ADV_SECOND_CALI_DISABLE);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to set nuc_t to default data\n");
        adv_device_close(ircmd_handle);
        return ret;
    }

    MultiPointCalibInputParam_version3_t multi_point_calib_input_param;
    multi_point_calib_input_param.multi_point_param = (AdvancedMultiPointCalibParam_t*)malloc(sizeof(AdvancedMultiPointCalibParam_t));
    if (multi_point_calib_input_param.multi_point_param == NULL)
    {
        printf("fail to malloc for multi_point_calib_input_param.multi_point_param\n");
        return -1;
    }
    multi_point_calib_input_param.multi_point_calib_array = (MultiPointCalibArray_version3_t*)malloc(sizeof(MultiPointCalibArray_version3_t));
    if (multi_point_calib_input_param.multi_point_calib_array == NULL)
    {
        printf("fail to malloc for multi_point_calib_input_param.multi_point_calib_array\n");
        free(multi_point_calib_input_param.multi_point_param);
        return -1;
    }

    printf("please input the number of muliti points:");
    scanf("%d", (int*)&multi_point_calib_input_param.multi_point_num);

    //read NUC_T table
    ret = read_file(ircmd_handle, gain_mode, NUC_T, org_nuc_table);
    if (ret != 0)
    {
        printf("fail to read single nuc file\n");
        adv_device_close(ircmd_handle);
        free(multi_point_calib_input_param.multi_point_param);
        free(multi_point_calib_input_param.multi_point_calib_array);
        return ret;
    }
    multi_point_calib_input_param.multi_point_calib_array->default_nuc_table = org_nuc_table;

#if 1    //use correct table file read from camera
    ret = read_file(ircmd_handle, gain_mode, CORRECT, correct_table);
    if (ret != 0)
    {
        printf("fail to read single correct file\n");
        return ret;
    }
#else    //use correct table from exist file
    //read CORRECT table
    FILE* fp = NULL;
    fp = fopen("RS300_9.1mm_GermaniumWindows__Ver259_H.bin", "rb");
    if (fp == NULL)
    {
        printf("fail to open correct table\n");
        adv_device_close(ircmd_handle);
        free(multi_point_calib_input_param.multi_point_param);
        free(multi_point_calib_input_param.multi_point_calib_array);
        return -1;
    }
    fread(correct_table, 1, sizeof(correct_table), fp);
    fclose(fp);
#endif

    //reverse calc NUC
    for (int i = 0; i < multi_point_calib_input_param.multi_point_num; i++)
    {
        multi_point_calib_input_param.multi_point_param->high_temp_env_cor_param = env_cor_param[i];
        multi_point_calib_input_param.multi_point_param->low_temp_env_cor_param = env_cor_param[i];
        multi_point_calib_input_param.multi_point_temp = &multi_point_temp[i];
        multi_point_calib_input_param.multi_point_nuc = &multi_point_nuc[i];

        ret = reverse_calc_NUC_with_nuc_t_table_len(org_nuc_table, multi_point_calib_input_param.multi_point_temp->output_temp, \
            ADVANCED_NUC_T_SIZE, &multi_point_calib_input_param.multi_point_nuc->output_nuc);
        if (ret != IRLIB_SUCCESS)
        {
            printf("reverse_calc_nuc_with_nuc_t failed\n");
            return ret;
        }

        ret = reverse_enhance_distance_temp_correct(&(multi_point_calib_input_param.multi_point_param->high_temp_env_cor_param), \
            correct_table, CORRECT_TABLE_SIZE, multi_point_calib_input_param.multi_point_temp->setting_temp, \
            &multi_point_calib_input_param.multi_point_temp->setting_temp);
        if (ret != IRLIB_SUCCESS)
        {
            printf("reverse setting temp failed\n");
            return ret;
        }

        ret = reverse_calc_NUC_with_nuc_t_table_len(org_nuc_table, multi_point_calib_input_param.multi_point_temp->setting_temp, \
            ADVANCED_NUC_T_SIZE, &multi_point_calib_input_param.multi_point_nuc->setting_nuc);
        if (ret != IRLIB_SUCCESS)
        {
            printf("reverse calc setting_temp nuc failed\n");
            return ret;
        }
        printf("setting_nuc=%d output_nuc=%d\n", multi_point_nuc[i].setting_nuc, multi_point_nuc[i].output_nuc);
    }

    //calc new NUC_T table
    multi_point_calib_input_param.multi_point_nuc = multi_point_nuc;
    multi_point_calc_new_nuc_table_with_version(&multi_point_calib_input_param, MULTI_POINT_CALIB_V3, new_nuc_table);

    //download new NUC_T table
    ret = download_file(ircmd_handle, gain_mode, NUC_T, new_nuc_table);
    if (ret != 0)
    {
        printf("fail to write normal file\n");
        adv_device_close(ircmd_handle);
        free(multi_point_calib_input_param.multi_point_param);
        free(multi_point_calib_input_param.multi_point_calib_array);
        return ret;
    }

    ret = adv_second_cali_flag_set(ircmd_handle, ADV_SECOND_CALI_FLAG_NUC_T, ADV_SECOND_CALI_ENABLE);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to set second cali flag\n");
        adv_device_close(ircmd_handle);
        free(multi_point_calib_input_param.multi_point_param);
        free(multi_point_calib_input_param.multi_point_calib_array);
        return ret;
    }

    printf("multi point calibration sample finish\n");
    free(multi_point_calib_input_param.multi_point_param);
    free(multi_point_calib_input_param.multi_point_calib_array);

#elif defined ENHANCE_DISTANCE_TEMP_CORRECT
    IrcmdPoint_t point_pos;
    float org_temp = 0;
    float new_temp = 0;
    uint32_t table_len = 0;

    printf("Please enter point coordinate\n");
    scanf("%hd %hd", &(point_pos.x), &(point_pos.y));

    ret = read_file(ircmd_handle, gain_mode, CORRECT, correct_table);
    if (ret != 0)
    {
        printf("fail to read single correct file\n");
        return ret;
    }

    ret = basic_point_temp_info_get(ircmd_handle, point_pos, &org_temp);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to get point temp\n");
        return ret;
    }

    ret = enhance_distance_temp_correct(&env_cor_param[0], correct_table, table_len, org_temp, &new_temp);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to enhance_distance_temp_correct\n");
        return ret;
    }

    printf("point_temp_value is %f\n", new_temp);
#endif
    ret = adv_device_close(ircmd_handle);
    if (ret != IRLIB_SUCCESS)
    {
        printf("fail to close device\n");
        return ret;
    }
    return 0;
}