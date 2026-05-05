#include "sample.h"
#include "config.h"

IrlibError_e auto_image_set(StreamFrameInfo_t stream_frame_info)
{
    int ret;
    char device_name[64] = { 0 };
    IrcmdHandle_t* handle = stream_frame_info.ircmd_handle;
    VideoOutputInfo_t output_info;
    output_info.video_output_status = BASIC_ENABLE;
    ret = basic_device_info_get(handle, BASIC_DEV_NAME, device_name);
    printf("basic_device_info_get:%d\n", ret);
    //set ouput format
    if(stream_frame_info.product_config.camera.image_channel_type == "dvp")
    {
        output_info.video_output_format = ADV_DVP_FORMAT;
    }
    else if(stream_frame_info.product_config.camera.image_channel_type == "mipi")
    {
        output_info.video_output_format = ADV_MIPI_FORMAT;
    }
    else
    {
        output_info.video_output_format = ADV_USB_FORMAT;
    }

    //set output fps
    output_info.video_output_fps = stream_frame_info.product_config.camera.v4l2_config.image_stream.fps;

    if((strcmp(device_name, "Camera MINI2256") == 0) || \
            (strcmp(device_name, "Camera MINI2640") == 0) || \
            (strcmp(device_name, "Camera MINI2384") == 0))
    {
        printf("MINI2 auto image\n");
        ret = adv_digital_video_output_set(handle, output_info);
        printf("adv_digital_video_output_set:%d\n", ret);
    }
    else if((strcmp(device_name, "MINI640S266-12A30009D10X") == 0) || \
            (strcmp(device_name, "G1280S") == 0))
    {
        printf("CS640 or G1280s auto image\n");
        ret = basic_video_stream_continue(handle);
        printf("basic_video_stream_continue:%d\n", ret);    //control mipi start
    }
    else
    {
        printf("not support device\n");
        return IRCMD_PARAM_ERROR;
    }
    return IRLIB_SUCCESS;
}

int main(int argc, char* argv[])
{
    printf("uart cmd and usb image sample start\n");

    config config_obj;
    char* config_path;
    if (argc != 2)
    {
        printf("usage: %s <path_of_config_file>\n");
        return -1;
    }

    config_path = argv[1];

    if (config_obj.parse_config(config_path) != 0)
    {
        printf("parse config failed\n");
        return -1;
    }

    single_config product_config;
    if (false == config_obj.get_config(product_config))
    {
        printf("can't find config\n");
        return -1;
    }

    printf("Depends lib version:\n");
    printf("version:%s\n", ircmd_version());
    printf("version:%s\n", ircam_version());
    printf("version:%s\n", iruart_version());

    printf("set libs log level:\n");
    ircmd_log_register(IRCMD_LOG_ERROR,NULL,NULL);
    iruart_log_register(IRUART_LOG_DEBUG,NULL,NULL);
    irv4l2_log_register(IRV4L2_LOG_ERROR,NULL,NULL);

    printf("init all stream handle\n");
    int fd;
    int ret;
    StreamFrameInfo_t stream_frame_info;
    memset(&stream_frame_info, 0, sizeof(stream_frame_info) - sizeof(product_config));
    stream_frame_info.product_config = product_config;

    printf("init stream handle\n");
    if (!product_config.camera.v4l2_config.has_image)
    {
        printf("not found image stream config\n");
        return -1;
    }
    stream_frame_info.image_name = (char*)product_config.camera.v4l2_config.image_stream.device_name.data();
    ir_video_handle_create(&ir_image_video_handle);
    stream_frame_info.image_driver_handle = (Irv4l2VideoHandle_t*)irv4l2_handle_create(ir_image_video_handle);

    printf("init control handle\n");
    IrControlHandle_t* ir_control_handle = NULL;
    ir_control_handle_create(&ir_control_handle);
    if (product_config.control.is_i2c_control)
    {
        Iri2cHandle_t* i2c_handle = NULL;
        i2c_handle = iri2c_handle_create(ir_control_handle);
        char* dev_node = (char*)product_config.control.i2c_param.dev_name.data();
        printf("dev_node is %s\n", dev_node);
        ret = ir_control_handle->ir_control_open(i2c_handle, dev_node);
        printf("open control node: %d\n", ret);
        stream_frame_info.ircmd_handle = ircmd_create_handle(ir_control_handle);
    }
    else if (product_config.control.is_uart_control)
    {
        IruartHandle_t* iruart_handle = NULL;
        iruart_handle = iruart_handle_create(ir_control_handle);

        FILE* fp;
        char buffer[80] = { 0 };
        char dev_node[20] = "/dev/";
        fp = popen("ls /dev | grep \"ttyUSB*\" ", "r");
        fgets(buffer, sizeof(buffer), fp);
        printf("content of ls /dev | grep \"ttyUSB*\" is %s\n", buffer);
        strcat(dev_node, buffer);
        printf("dev_node is %s\n", dev_node);

        int i;
        for (i = sizeof(dev_node) - 1; i < (int)(sizeof(dev_node)); i--)
        {
            if (dev_node[i] == '\n')
            {
                dev_node[i] = 0;
                break;
            }
        }
        ret = ir_control_handle->ir_control_open(iruart_handle, dev_node);
        printf("open control node: %d\n", ret);
        //set baudrate, params:int baudrate[64] = {115200} or {921600} or NULL(self-adaptation)
        ret = ir_control_handle->ir_control_init(iruart_handle, NULL);
        printf("control init: %d\n", ret);
        stream_frame_info.ircmd_handle = ircmd_create_handle(ir_control_handle);
    }

    //set auto image
    if (!product_config.camera.is_auto_image)
    {
        ret = auto_image_set(stream_frame_info);
        printf("auto_image_set:%d\n", ret);
    }

    load_stream_frame_info(&stream_frame_info, true, true);
    init_pthread_sem();
    pthread_t stream_thread,display_thread,capture_thread,cmd_thread;
    pthread_create(&stream_thread, NULL, v4l2_stream_function, &stream_frame_info);
    pthread_create(&display_thread, NULL, drm_display_function, &stream_frame_info);
   // pthread_create(&capture_thread, NULL, capture_function, &stream_frame_info);
    pthread_create(&cmd_thread, NULL, cmd_function, &stream_frame_info);
    sleep(1);

    printf("in streaming\n");
    void *thread_result;
    pthread_join(stream_thread, &thread_result);
    printf("stop stream\n");
    //pthread_cancel(display_thread);
    //pthread_cancel(cmd_thread);
    destroy_pthread_sem();
    destroy_data_demo(&stream_frame_info);

    return 0;
}