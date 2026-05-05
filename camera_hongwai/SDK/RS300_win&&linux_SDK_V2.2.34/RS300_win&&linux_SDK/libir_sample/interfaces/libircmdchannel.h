#ifndef _LIBCMDCHANNEL_H_
#define _LIBCMDCHANNEL_H_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <linux/videodev2.h>
#include "error.h"
#include "libir_camera_data.h"
#ifdef __cplusplus
extern "C" {
#endif

//command list in libircmdchannel library

/**
* @brief download firmware
* @param[in] firmware path, type: char*
*/
#define SET_FW_UPDATE     "FwUpdate"
/**
 * @brief error status process
 * @param[in] car's status
 */
#define SET_ERROR_PROCESS "ErrProc"
/**
 * @brief get firmware log
 * @param[in] buffer for log, type: uint8_t*, size: 1024 * 1024 Byte
 */
#define GET_FW_LOG        "FwLog"
/**
 * @brief get one image frame
 * @param[in] pointer of IrDataOutput_t, type: IrDataOutput_t*, user need not alloca data memory
 */
#define GET_FRAME         "GetFrame"
/**
 * @brief get device name
 * @param[in] buffer of device name, type: uint8_t*, size: 100 Byte
 */
#define GET_DEV_NAME      "DevName"
/**
 * @brief get device product number
 * @param[in] buffer of device product number, type: uint8_t*, size: 100 Byte
 */
#define GET_DEV_PN        "DevPN"
/**
 * @brief get device serial number
 * @param[in] buffer of device serial number, type: uint8_t*, size: 100 Byte
 */
#define GET_DEV_SN        "DevSN"
/**
 * @brief get device firmware version
 * @param[in] buffer of firmware version, type: uint8_t*, size: 100 Byte
 */
#define GET_FW_VERSION    "FwVersion"
/**
 * @brief get SDK version
 * @param[in] buffer of SDK version, type: uint8_t*, size: 100 Byte
 */
#define GET_SDK_VERSION   "SDKVersion"


extern void (*ircmdchannel_debug_print)(const char* fmt, ...);
extern void (*ircmdchannel_error_print)(const char* fmt, ...);

/**
* @brief Log level definition in libircmdchannel library
*/
enum IrcmdchannelLogLevels_e
{
	/// print debug and error infomation
	IRCMDCHANNEL_LOG_DEBUG = 0,
	/// only print error infomation
	IRCMDCHANNEL_LOG_ERROR = 1,
	/// don't print debug and error infomation
	IRCMDCHANNEL_LOG_NO_PRINT = 2,
};


/**
 * @brief register log level for libircmdchannel library
 *
 * @param[in] log_level log level
 * @param[in] callback the callback function for printing log messages
 * @param[in] priv_data the private parameter of callback function
 *
 */
void ircmdchannel_log_register(enum IrcmdchannelLogLevels_e log_level, void* (*callback)(void* callback_data, void* priv_data), void* priv_data);

/**
 * @brief Get libircmdchannel library's version
 *
 * @return version's string
 */
const char* ircmdchannel_version(void);

/**
 * @brief Get libircmdchannel library's version number
 *
 * @return version number's string
 */
const char* ircmdchannel_version_number(void);


/**
 * @brief Initialize the configuration of the control node
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_control_init(const char* dev_node);

/**
 * @brief write data to the device
 *
 * @param[in] data_type write data type
 * @param[out] data_value write data value
 *
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_set_value(char* data_type, void* data_value);

/**
 * @brief read data from the device
 *
 * @param[in] data_type wrireadte data type
 * @param[in] data_value read data value
 *
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_get_value(char* data_type, void* data_value);

/**
 * @brief Release the control node
 *
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_control_release();

/**
 * @brief init camera
 * 
 * @param[in] camera paramter
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_camera_init(IrCamera_t* ir_camera);

/**
 * @brief register data transform, if data from platform not you need(yuyv or uyvy), you can call this function,
 *        register a function to finish the data convert
 * 
 * @param[in] ir_camera see IrCamera_t
 * @param[in] data_transform point of convert function, default is NULL
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_register_camera_data_convert(int (*data_transform)(uint8_t* src, int width, int height, uint8_t* dst));

/**
 * @brief start camera
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_start_camera();

/**
 * @brief stop camera
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_stop_camera();

/**
 * @brief release camera
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_release_camera();

/**
 * @brief open or close target detect
 * 
 * @param[in] open, 1 is open, 0 is close
 * 
 * @return see IrlibError_e
 */
IrlibError_e ircmdchannel_set_open_target_detect(int open);

#ifdef __cplusplus
}
#endif
#endif
