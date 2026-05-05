## 配置文件格式

```
[
    {
        "product":"G1280s",
        "control":{
            "is_uart_control":true,
            "is_i2c_control":false,
            "is_i2c_usb_control":false,
            "is_usb_control":false,
            "uart_param":{
                "com_index":0
            },
            "i2c_param":{
                "device_name":"/dev/i2c-1"
            },
            "usb_param":{
                "pid":"0x00C3",
                "vid":"0x04B4",
                "same_id":0
            }
        },
        "camera":{
            "is_auto_image":false,
            "format":"nv12_and_temp",
            "width":1280,
            "height":1024,
            "image_info_height":1024,
            "info_line_height":3,
            "temp_info_height":1024,
            "dummy_info_height":2,
            "image_info_ratio":1.5,
            "info_line_ratio":2,
            "temp_line_ratio":2,
            "dummy_info_ratio":2,
            "v4l2_stream":[
                {
                    "device_name":"/dev/video5",
                    "type":"image",
                    "fps":30,
                    "dev_width":1280,
                    "dev_height":771
                },
                {
                    "device_name":"/dev/video6",
                    "type":"temp",
                    "fps":30,
                    "dev_width":1280,
                    "dev_height":1026
                }
            ],
            "uvc_stream":{
                "device_info":{
                    "pid":"0x00C3",
                    "vid":"0x04B4",
                    "same_id":0
                },
                "width":1280,
                "height":1797,
                "fps":30,
                "frame_size_ratio":2
            }
        }
    }
]
```
整体上为json数组，数组内每一个json对象为一个型号的配置，因部分机芯windows和linux下使用的命令通道类型不同（如linux下i2c，windows下usb），在windos下使用`stream_win.conf`配置文件，linux下使用`stream.conf`文件，另外

## 各个字段的含义

|字段|含义|是否必填|类型|
|--|--|--|--|
|product|产品名|是|字符串|
|control|命令有关配置|是|json对象|
|control::is_uart_control|是否使用uart命令通道|否|bool|
|control::is_i2c_control|是否使用i2c命令通道|否|bool|
|control::is_i2c_usb_control|是否使用i2c-usb命令通道|否|bool|
|control::is_usb_control|是否使用usb命令通道|否|bool|
|control::uart_param|uart命令通道参数|使用uart命令通道必填|json对象|
|control::uart_param::com_index|uart串口号|windows下uart命令必填|整型|
|control::i2c_param|i2c命令通道参数|使用i2c命令通道必填|json对象|
|control::i2c_param::device_name|i2c设备名|使用i2c命令通道必填|字符串|
|control::usb_param|usb设备信息|使用usb或i2c usb命令通道必填|json对象|
|control::usb_param::pid|usb设备的pid|使用usb或i2c usb命令通道必填|字符串|
|control::usb_param::vid|usb设备的vid|使用usb或i2c usb命令通道必填|字符串|
|control::usb_param::same_id|usb设备的same_id|使用usb或i2c usb命令通道必填|整型，填0即可|
|camera|图像相关参数|是|json对象|
|camera::is_auto_image|是否自动出图|否|bool，默认true|
|camera::format|图像格式|是|字符串，可选"yuyv_image", "nv12_image", "nv12_and_temp", "yuyv_and_temp", "uyvu_image"|
|camera::width|显示图像宽度|是|整型|
|camera::height|显示图像高度|是|整型|
|camera::image_info_height|图像高度|是|整型|
|camera::info_line_height|图像信息行高度|是|整型|
|camera::temp_info_height|温度图像高度|是|整型|
|camera::dummy_info_height|温度信息行高度|是|整型|
|camera::image_info_ratio|图像信息大小系数|是|浮点型|
|camera::info_line_ratio|图像信息行大小系数|是|浮点型|
|camera::temp_line_ratio|温度信息大小系数|是|浮点型|
|camera::dummy_info_ratio|温度信息行大小系数|是|浮点型|
|camera::image_channel_type|图像与温度数据通道类型|否，默认为usb，可填usb、dvp、mipi|字符串|
|camera::v4l2_stream|v4l2参数|否，使用v4l2出图时必填|json对象|
|camera::v4l2_stream::device_name|v4l2设备名|否，使用v4l2出图时必填|字符串|
|camera::v4l2_stream::type|图像/温度数据|否，使用v4l2出图时必填|字符串，可填image、temp|
|camera::v4l2_stream::fps|图像帧数|否，使用v4l2出图时必填|整型|
|camera::v4l2_stream::dev_width|图像宽度|否，使用v4l2出图时必填|整型|
|camera::v4l2_stream::dev_height|图像高度|否，使用v4l2出图时必填|整型|
|camera::uvc_stream|uvc参数|否，使用uvc出图时必填|json对象|
|camera::uvc_stream::device_info|uvc设备信息|否，使用uvc出图时必填|json对象|
|camera::uvc_stream::device_info::pid|usb图像设备pid|否，使用uvc出图时必填|字符串|
|camera::uvc_stream::device_info::vid|usb图像设备vid|否，使用uvc出图时必填|字符串|
|camera::uvc_stream::device_info::same_id|usb图像设备same_id|否，使用uvc出图时必填|整型，填0即可|
|camera::uvc_stream::width|usb图像宽度|否，使用uvc出图时必填|整型|
|camera::uvc_stream::height|usb图像高度|否，使用uvc出图时必填|整型|
|camera::uvc_stream::fps|usb图像帧率|否，使用uvc出图时必填|整型|
|camera::uvc_stream::frame_size_ratio|usb图像帧大小系数|否，使用uvc出图时必填|浮点型|


## 部分机芯参数设置：需要输入对应的宽高。

|                 | /dev/video5 图像 + 信息行 + dummy数据                | /dev/video6 温度 + 信息行数据          |
| --------------- | ---------------------------------------------------- | -------------------------------------- |
| **MINI640机芯** |                                                      |                                        |
| NV12双出图      | 分辨率640*387                                        | 分辨率640*514                          |
|                 | 数据内容为640x512(NV12) + 640x2(info) + 640x1(dummy) | 数据内容为640x512(Y16) + 640x2(info)   |
| **G1280s机芯**  |                                                      |                                        |
| NV12双出图      | 分辨率1280*771，数据768(NV12)+2(info)+1(dummy)       | 分辨率1280*1026，数据1024(Y16)+2(info) |
|                 | 数据内容为1280x1024(NV12)+1280x2(info)+1(dummy)      | 数据内容为1280x1024(Y16)+1280x2(info)  |