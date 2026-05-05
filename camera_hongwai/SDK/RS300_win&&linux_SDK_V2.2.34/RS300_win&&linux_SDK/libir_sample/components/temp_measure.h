#ifndef _TEMP_MEASURE_
#define _TEMP_MEASURE_

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "libircmd.h"
#include "libircmd_temp.h"
#include "libirtemp.h"
#include "info_parse.h"
#include "data.h"

#if defined(_WIN32)
#define IR_TEMP_MEASURE_DEBUG(format, ...) ir_temp_measure_debug_print("ir temp measure debug [%s:%d/%s] " format "\n", \
														 __FILE__,__LINE__, __FUNCTION__, __VA_ARGS__)
#define IR_TEMP_MEASURE_ERROR(format, ...) ir_temp_measure_error_print("ir temp measure error [%s:%d/%s] " format "\n", \
														__FILE__,__LINE__, __FUNCTION__, __VA_ARGS__)
#elif defined(linux) || defined(unix)
#include <libgen.h>
#define IR_TEMP_MEASURE_DEBUG(format, ...) ir_temp_measure_debug_print("ir temp measure debug [%s:%d/%s] " format "\n", \
														basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define IR_TEMP_MEASURE_ERROR(format, ...) ir_temp_measure_error_print("ir temp measure error [%s:%d/%s] " format "\n", \
														basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#endif

	extern void (*ir_temp_measure_debug_print)(const char* fmt, ...);
	extern void (*ir_temp_measure_error_print)(const char* fmt, ...);

#pragma pack (1)

	#define	INFO_ON	1

	/**
	* @brief Error type in temp measure
	*/
	typedef enum {
		/**Function excute success*/
		TEMP_MEASURE_SUCCESS = 0,
		/**Invalid input parameter*/
		TEMP_MEASURE_ERROR_PARAM = -1,
		/**Dynamic alloc memory fail*/
		TEMP_MEASURE_ALLOC_FAIL = -2,
		/**Temp measure process fail*/
		TEMP_MEASURE_PROCESS_FAIL = -3
	}temp_measure_error_e;

	/**
	* @brief Log level definition in temp measure
	*/
	typedef enum {
		/**Print debug and error infomation*/
		IR_TEMP_MEASURE_LOG_DEBUG = 0,
		/**Only print error infomation*/
		IR_TEMP_MEASURE_LOG_ERROR = 1,
		/**Don't print debug and error infomation*/
		IR_TEMP_MEASURE_LOG_NO_PRINT = 2,
	}IrTempMeasureLogLevel_e;

	/**
	* @brief Point struct, start from 1
	*/
	typedef struct {
		/// x position
		uint16_t x;
		/// y position
		uint16_t y;
	}IrPoint_t;

	/**
	* @brief Line struct, start from 1
	*/
	typedef struct {
		/// start point's position
		IrPoint_t start_point;
		/// end point's position
		IrPoint_t end_point;
	}IrLine_t;

	/**
	* @brief Rectangle struct, start from 1
	*/
	typedef struct {
		/// start point's position
		IrPoint_t start_point;
		/// end point's position
		IrPoint_t end_point;
	}IrRect_t;

	/**
	* @brief Maximum and minimum temperature information struct
	*/
	typedef struct
	{
		/// maximum temperature value
		float max_temp;
		/// minimum temperature value
		float min_temp;
		/// maximum temperature point's position
		IrPoint_t max_temp_point;
		/// minimum temperature point's position
		IrPoint_t min_temp_point;
	}MaxMinTempData_t;

	/**
	 * @brief Temperature information struct of a line or a rectangle.
	 */
	typedef struct {
		/// maximum temperature point's position
		float ave_temp;
		/// maximum temperature's point position
		MaxMinTempData_t max_min_temp_info;
	}LineRectTempData_t;

	/**
	* @brief Format of frame
	*/
	typedef enum {
		TEMP_MEASURE_ONLY_IMAGE = 0,
		TEMP_MEASURE_ONLY_TEMP = 1,
		TEMP_MEASURE_IMAGE_AND_TEMP = 2
	}frame_format_e;

	/**
	 * @brief Format of temp frame
	 */
	typedef enum {
		TEMP_FRAME_FMT_Y16 = 0,
		TEMP_FRAME_FMT_YUYV = 1,
	}temp_format_e;

	/**
	 * @brief Information of temp frame
	 */
	typedef struct {
		temp_format_e  temp_format;
		uint8_t* temp_frame;
		uint32_t temp_height;
		uint32_t temp_width;
	}temp_frame_info_t;

	/**
	 * @brief the handle of temp measure
	 */
	typedef struct {
		IrinfoTpdInfo_t* tpd_info;
		frame_format_e frame_format;
		temp_frame_info_t temp_frame_info;
		IrcmdHandle_t* ircmd_handle;
	}temp_measure_t;

#pragma pack ()


	temp_measure_error_e init_temp_measure_handle(temp_measure_t* handle, IrinfoTpdInfo_t* tpd_info, \
		IrcmdHandle_t* ircmd_handle, uint32_t temp_height, uint32_t temp_width, frame_format_e frame_format,\
		temp_format_e temp_format);

	temp_measure_error_e destroy_temp_measure_handle(temp_measure_t* handle);

	temp_measure_error_e temp_measure_get_frame_temp(temp_measure_t* handle, MaxMinTempData_t* frame_temp_value);

	temp_measure_error_e temp_measure_get_point_temp(temp_measure_t* handle, IrPoint_t point_pos, float* point_temp_value);

	temp_measure_error_e temp_measure_get_line_temp(temp_measure_t* handle, IrLine_t line_pos, LineRectTempData_t* line_temp_value);

	temp_measure_error_e temp_measure_get_rect_temp(temp_measure_t* handle, IrRect_t rect_pos, LineRectTempData_t* rect_temp_value);

	void* temp_measure_function(void* threadarg);

	void ircmd_temp_measure_log_register(IrcmdLogLevel_e log_level);

#endif
