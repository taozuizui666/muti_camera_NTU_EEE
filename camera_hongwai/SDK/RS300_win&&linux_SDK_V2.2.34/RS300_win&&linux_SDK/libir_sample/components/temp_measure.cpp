#define _CRT_SECURE_NO_WARNINGS
#include "temp_measure.h"

//printf debug info
void ir_temp_measure_debug_info(const char* fmt, ...)
{
	char printf_buf[200] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf(printf_buf, fmt, args);
	printf("%s", printf_buf);
	va_end(args);
	fflush(stdout);
}


//disable printf debug info
void ir_temp_measure_debug_void(const char* fmt, ...)
{
	;
}


//printf error info
void ir_temp_measure_error_info(const char* fmt, ...)
{
	char printf_buf[200] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf(printf_buf, fmt, args);
	printf("%s", printf_buf);
	va_end(args);
	fflush(stdout);
}


//disable printf error info
void ir_temp_measure_error_void(const char* fmt, ...)
{
	;
}


//register printf log
void ircmd_temp_measure_log_register(IrcmdLogLevel_e log_level)
{
	switch (log_level)
	{
	case IR_TEMP_MEASURE_LOG_DEBUG:
	{
		ir_temp_measure_debug_print = ir_temp_measure_debug_info;
		ir_temp_measure_error_print = ir_temp_measure_error_info;
		break;
	}
	case IR_TEMP_MEASURE_LOG_ERROR:
	{
		ir_temp_measure_debug_print = ir_temp_measure_debug_void;
		ir_temp_measure_error_print = ir_temp_measure_error_info;
		break;
	}
	case IR_TEMP_MEASURE_LOG_NO_PRINT:
	default:
	{
		ir_temp_measure_debug_print = ir_temp_measure_debug_void;
		ir_temp_measure_error_print = ir_temp_measure_error_void;
		break;
	}
	}
}


void (*ir_temp_measure_debug_print)(const char* fmt, ...) = ir_temp_measure_debug_void;
void (*ir_temp_measure_error_print)(const char* fmt, ...) = ir_temp_measure_error_void;


temp_measure_error_e init_temp_measure_handle(temp_measure_t* handle, IrinfoTpdInfo_t* tpd_info, \
	IrcmdHandle_t* ircmd_handle, uint32_t temp_height, uint32_t temp_width, frame_format_e frame_format, \
	temp_format_e temp_format)
{
	if (handle == NULL || tpd_info == NULL || ircmd_handle == NULL)
	{
		IR_TEMP_MEASURE_ERROR("handle or tpd_info or ircmd_handle is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	handle->frame_format = frame_format;
	handle->tpd_info = tpd_info;
	handle->temp_frame_info.temp_format = temp_format;
	handle->temp_frame_info.temp_height = temp_height;
	handle->temp_frame_info.temp_width = temp_width;
	handle->ircmd_handle = ircmd_handle;
	handle->temp_frame_info.temp_frame = (uint8_t*)malloc((handle->temp_frame_info.temp_height) * \
		(handle->temp_frame_info.temp_width) * 2 * sizeof(uint8_t));

	if (handle->temp_frame_info.temp_frame == NULL)
	{
		IR_TEMP_MEASURE_ERROR("there is no more space");
		return TEMP_MEASURE_ALLOC_FAIL;
	}

	return TEMP_MEASURE_SUCCESS;
}


temp_measure_error_e destroy_temp_measure_handle(temp_measure_t* handle)
{
	if (handle == NULL)
	{
		IR_TEMP_MEASURE_ERROR("handle is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	handle->ircmd_handle = NULL;
	handle->tpd_info = NULL;
	if (handle->temp_frame_info.temp_frame != NULL)
	{
		free(handle->temp_frame_info.temp_frame);
	}

	return TEMP_MEASURE_SUCCESS;
}


temp_measure_error_e get_frame_temp_from_frame_info(IrinfoTpdInfo_t* tpd_info, MaxMinTempData_t* frame_temp_value)
{
	if (tpd_info == NULL || frame_temp_value == NULL)
	{
		IR_TEMP_MEASURE_ERROR("tpd_info or frame_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (tpd_info->frame_tpd.frame_info_en == INFO_ON)
	{
		frame_temp_value->max_temp = tpd_info->frame_tpd.max_pixel_value;
		frame_temp_value->min_temp = tpd_info->frame_tpd.min_pixel_value;
		frame_temp_value->max_temp_point.x = tpd_info->frame_tpd.max_pixel.x;
		frame_temp_value->max_temp_point.y = tpd_info->frame_tpd.max_pixel.y;
		frame_temp_value->min_temp_point.x = tpd_info->frame_tpd.min_pixel.x;
		frame_temp_value->min_temp_point.y = tpd_info->frame_tpd.min_pixel.y;
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use frame infomation to get frame temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_frame_temp_from_temp_frame(temp_measure_t* handle, MaxMinTempData_t* frame_temp_value)
{
	if (handle == NULL || frame_temp_value == NULL)
	{
		IR_TEMP_MEASURE_ERROR("handle or frame_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (handle->frame_format != TEMP_MEASURE_ONLY_IMAGE)
	{
		if (handle->temp_frame_info.temp_format == TEMP_FRAME_FMT_Y16)
		{
			TempDataRes_t temp_res = { handle->temp_frame_info.temp_width, handle->temp_frame_info.temp_height };
			Area_t rect = { 0,0, handle->temp_frame_info.temp_width, handle->temp_frame_info.temp_height };
			TempInfo_t temp_info = { 0 };
			IrlibError_e ret;
			ret = get_rect_temp((uint16_t*)(handle->temp_frame_info.temp_frame), temp_res, rect, &temp_info);
			if (ret == IRLIB_SUCCESS)
			{
				frame_temp_value->max_temp = ((double)temp_info.max_temp) / 64 - 273.15;
				frame_temp_value->min_temp = ((double)temp_info.min_temp) / 64 - 273.15;
				frame_temp_value->max_temp_point.x = temp_info.max_cord.x;
				frame_temp_value->max_temp_point.y = temp_info.max_cord.y;
				frame_temp_value->min_temp_point.x = temp_info.min_cord.x;
				frame_temp_value->min_temp_point.y = temp_info.min_cord.y;
				return TEMP_MEASURE_SUCCESS;
			}
		}
	}

	IR_TEMP_MEASURE_DEBUG("use temp frame to get frame temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_frame_temp_from_vdcmd(IrcmdHandle_t* ircmd_handle, MaxMinTempData_t* frame_temp_value)
{
	if (ircmd_handle == NULL || frame_temp_value == NULL)
	{
		IR_TEMP_MEASURE_ERROR("ircmd_handle or frame_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	IrlibError_e ret;
	MaxMinTempInfo_t frame_temp_info = { 0 };
	ret = basic_frame_temp_info_get(ircmd_handle, &frame_temp_info);
	if (ret == IRLIB_SUCCESS)
	{
		frame_temp_value->max_temp = frame_temp_info.max_temp;
		frame_temp_value->min_temp = frame_temp_info.min_temp;
		frame_temp_value->max_temp_point.x = frame_temp_info.max_temp_point.x;
		frame_temp_value->max_temp_point.y = frame_temp_info.max_temp_point.y;
		frame_temp_value->min_temp_point.x = frame_temp_info.min_temp_point.x;
		frame_temp_value->min_temp_point.y = frame_temp_info.min_temp_point.y;
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use vdcmd get frame temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e temp_measure_get_frame_temp(temp_measure_t* handle, MaxMinTempData_t* frame_temp_value)
{
	if (handle == NULL || frame_temp_value == NULL)
	{
		IR_TEMP_MEASURE_ERROR("handle or frame_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	temp_measure_error_e ret;
	ret = get_frame_temp_from_temp_frame(handle, frame_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use temp frame to get frame temp successfully");
		return ret;
	}

	ret = get_frame_temp_from_frame_info(handle->tpd_info, frame_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use frame infomation to get frame temp successfully");
		return ret;
	}

	ret = get_frame_temp_from_vdcmd(handle->ircmd_handle, frame_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use vdcmd to get frame temp successfully");
		return ret;
	}

	IR_TEMP_MEASURE_DEBUG("fail to get frame temp");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_point_temp_from_frame_info(IrinfoTpdInfo_t* tpd_info, IrPoint_t point_pos, \
	float* point_temp_value)
{
	if (tpd_info == NULL || point_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("tpd_info or point_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (tpd_info->pixel_tpd_0.pixel_info_en == INFO_ON)
	{
		if ((point_pos.x == tpd_info->pixel_tpd_0.pixel.x) && (point_pos.y == tpd_info->pixel_tpd_0.pixel.y))
		{
			*point_temp_value = tpd_info->pixel_tpd_0.pixel_value;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	else if (tpd_info->pixel_tpd_1.pixel_info_en == INFO_ON)
	{
		if ((point_pos.x == tpd_info->pixel_tpd_1.pixel.x) && (point_pos.y == tpd_info->pixel_tpd_1.pixel.y))
		{
			*point_temp_value = tpd_info->pixel_tpd_1.pixel_value;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	else if (tpd_info->pixel_tpd_2.pixel_info_en == INFO_ON)
	{
		if ((point_pos.x == tpd_info->pixel_tpd_2.pixel.x) && (point_pos.y == tpd_info->pixel_tpd_2.pixel.y))
		{
			*point_temp_value = tpd_info->pixel_tpd_2.pixel_value;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	else if (tpd_info->pixel_tpd_3.pixel_info_en == INFO_ON)
	{
		if ((point_pos.x == tpd_info->pixel_tpd_3.pixel.x) && (point_pos.y == tpd_info->pixel_tpd_3.pixel.y))
		{
			*point_temp_value = tpd_info->pixel_tpd_3.pixel_value;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	IR_TEMP_MEASURE_DEBUG("use frame infomation to get point temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_point_temp_from_temp_frame(temp_measure_t* handle, IrPoint_t point_pos,\
	float* point_temp_value)
{
	if (handle == NULL || point_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("handle or point_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (handle->frame_format != TEMP_MEASURE_ONLY_IMAGE)
	{
		if (handle->temp_frame_info.temp_format == TEMP_FRAME_FMT_Y16)
		{
			IrlibError_e ret;
			TempDataRes_t temp_res = { handle->temp_frame_info.temp_width, handle->temp_frame_info.temp_height };
			Dot_t point = { point_pos.x, point_pos.y };
			uint16_t point_temp;
			ret = get_point_temp((uint16_t*)(handle->temp_frame_info.temp_frame), temp_res, point, &point_temp);
			if (ret == IRLIB_SUCCESS)
			{
				*point_temp_value = ((double)point_temp) / 64 - 273.15;
				return TEMP_MEASURE_SUCCESS;
			}
		}
	}

	IR_TEMP_MEASURE_DEBUG("use temp frame to get point temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_point_temp_from_vdcmd(IrcmdHandle_t* ircmd_handle, IrPoint_t point_pos, \
	float* point_temp_value)
{
	if (ircmd_handle == NULL || point_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("ircmd_handle or point_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	IrlibError_e ret;
	IrcmdPoint_t point_addr = { point_pos.x, point_pos.y };
	ret = basic_point_temp_info_get(ircmd_handle, point_addr, point_temp_value);
	if (ret == IRLIB_SUCCESS)
	{
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use vdcmd to get point temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e temp_measure_get_point_temp(temp_measure_t* handle, IrPoint_t point_pos, \
	float* point_temp_value)
{
	if (handle == NULL || point_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("handle or point_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	temp_measure_error_e ret;
	ret = get_point_temp_from_temp_frame(handle, point_pos, point_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use temp frame to get point temp successfully");
		return ret;
	}

	ret = get_point_temp_from_frame_info(handle->tpd_info, point_pos, point_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use frame infomation to get point temp successfully");
		return ret;
	}

	ret = get_point_temp_from_vdcmd(handle->ircmd_handle, point_pos, point_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use vdcmd to get point temp successfully");
		return ret;
	}

	IR_TEMP_MEASURE_DEBUG("fail to get point temp");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_line_temp_from_frame_info(IrinfoTpdInfo_t* tpd_info, IrLine_t line_pos, \
	LineRectTempData_t* line_temp_value)
{
	if (tpd_info == NULL || line_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("tpd_info or line_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (tpd_info->line_tpd_0.line_rect_info_en == INFO_ON)
	{
		if ((tpd_info->line_tpd_0.start_pixel.x == line_pos.start_point.x) && \
			(tpd_info->line_tpd_0.start_pixel.y == line_pos.start_point.y) && \
			(tpd_info->line_tpd_0.end_pixel.x == line_pos.end_point.x) && \
			(tpd_info->line_tpd_0.end_pixel.y == line_pos.end_point.y))
		{
			line_temp_value->max_min_temp_info.max_temp = tpd_info->line_tpd_0.max_pixel_value;
			line_temp_value->max_min_temp_info.min_temp = tpd_info->line_tpd_0.min_pixel_value;
			line_temp_value->ave_temp = tpd_info->line_tpd_0.ave_pixel_value;
			line_temp_value->max_min_temp_info.max_temp_point.x = tpd_info->line_tpd_0.max_pixel.x;
			line_temp_value->max_min_temp_info.max_temp_point.y = tpd_info->line_tpd_0.max_pixel.y;
			line_temp_value->max_min_temp_info.min_temp_point.x = tpd_info->line_tpd_0.min_pixel.x;
			line_temp_value->max_min_temp_info.min_temp_point.y = tpd_info->line_tpd_0.min_pixel.y;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	else if (tpd_info->line_tpd_1.line_rect_info_en == INFO_ON)
	{
		if ((tpd_info->line_tpd_1.start_pixel.x == line_pos.start_point.x) && \
			(tpd_info->line_tpd_1.start_pixel.y == line_pos.start_point.y) && \
			(tpd_info->line_tpd_1.end_pixel.x == line_pos.end_point.x) && \
			(tpd_info->line_tpd_1.end_pixel.y == line_pos.end_point.y))
		{
			line_temp_value->max_min_temp_info.max_temp = tpd_info->line_tpd_1.max_pixel_value;
			line_temp_value->max_min_temp_info.min_temp = tpd_info->line_tpd_1.min_pixel_value;
			line_temp_value->ave_temp = tpd_info->line_tpd_1.ave_pixel_value;
			line_temp_value->max_min_temp_info.max_temp_point.x = tpd_info->line_tpd_1.max_pixel.x;
			line_temp_value->max_min_temp_info.max_temp_point.y = tpd_info->line_tpd_1.max_pixel.y;
			line_temp_value->max_min_temp_info.min_temp_point.x = tpd_info->line_tpd_1.min_pixel.x;
			line_temp_value->max_min_temp_info.min_temp_point.y = tpd_info->line_tpd_1.min_pixel.y;
			return TEMP_MEASURE_SUCCESS;
		}
	}

	IR_TEMP_MEASURE_DEBUG("use frame info to get line temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_line_temp_from_temp_frame(temp_measure_t* handle, IrLine_t line_pos, \
	LineRectTempData_t* line_temp_value)
{
	if (handle == NULL || line_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("handle or line_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (handle->frame_format != TEMP_MEASURE_ONLY_IMAGE)
	{
		if (handle->temp_frame_info.temp_format == TEMP_FRAME_FMT_Y16)
		{
			IrlibError_e ret;
			TempInfo_t line_temp_info = { 0 };
			TempDataRes_t temp_res = { handle->temp_frame_info.temp_width, handle->temp_frame_info.temp_height };
			Line_t line = { line_pos.start_point.x, line_pos.start_point.y, line_pos.end_point.x, line_pos.end_point.y };
			ret = get_line_temp((uint16_t*)(handle->temp_frame_info.temp_frame), temp_res, line, & line_temp_info);
			if (ret == IRLIB_SUCCESS)
			{
				line_temp_value->max_min_temp_info.max_temp = ((double)line_temp_info.max_temp) / 64 - 273.15;
				line_temp_value->max_min_temp_info.min_temp = ((double)line_temp_info.min_temp) / 64 - 273.15;
				line_temp_value->ave_temp = ((double)line_temp_info.avr_temp) / 64 - 273.15;
				line_temp_value->max_min_temp_info.max_temp_point.x = line_temp_info.max_cord.x;
				line_temp_value->max_min_temp_info.max_temp_point.y = line_temp_info.max_cord.y;
				line_temp_value->max_min_temp_info.min_temp_point.x = line_temp_info.min_cord.x;
				line_temp_value->max_min_temp_info.min_temp_point.y = line_temp_info.min_cord.y;
				return TEMP_MEASURE_SUCCESS;
			}
		}
	}

	IR_TEMP_MEASURE_DEBUG("use temp frame to get line temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_line_temp_from_vdcmd(IrcmdHandle_t* ircmd_handle, IrLine_t line_pos, \
	LineRectTempData_t* line_temp_value)
{
	if (ircmd_handle == NULL || line_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("ircmd_handle or line_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	IrlibError_e ret;
	IrcmdLine_t line_addr = { line_pos.start_point.x, line_pos.start_point.y, \
		line_pos.end_point.x, line_pos.end_point.y };
	LineRectTempInfo_t line_temp_info = { 0 };
	ret = basic_line_temp_info_get(ircmd_handle, line_addr, &line_temp_info);
	if (ret == IRLIB_SUCCESS)
	{
		line_temp_value->max_min_temp_info.max_temp = line_temp_info.max_min_temp_info.max_temp;
		line_temp_value->max_min_temp_info.min_temp = line_temp_info.max_min_temp_info.min_temp;
		line_temp_value->ave_temp = line_temp_info.ave_temp;
		line_temp_value->max_min_temp_info.max_temp_point.x = line_temp_info.max_min_temp_info.max_temp_point.x;
		line_temp_value->max_min_temp_info.max_temp_point.y = line_temp_info.max_min_temp_info.max_temp_point.y;
		line_temp_value->max_min_temp_info.min_temp_point.x = line_temp_info.max_min_temp_info.min_temp_point.x;
		line_temp_value->max_min_temp_info.min_temp_point.y = line_temp_info.max_min_temp_info.min_temp_point.y;
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use vdcmd to get line temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e temp_measure_get_line_temp(temp_measure_t* handle, IrLine_t line_pos, \
	LineRectTempData_t* line_temp_value)
{
	if (handle == NULL || line_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("handle or line_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	temp_measure_error_e ret;
	ret = get_line_temp_from_temp_frame(handle, line_pos, line_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use temp frame to get line temp successfully");
		return ret;
	}

	ret = get_line_temp_from_frame_info(handle->tpd_info, line_pos, line_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use frame info to get line temp successfully");
		return ret;
	}

	ret = get_line_temp_from_vdcmd(handle->ircmd_handle, line_pos, line_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use vdcmd to get line temp successfully");
		return ret;
	}

	IR_TEMP_MEASURE_DEBUG("fail to get line temp");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_rect_temp_from_frame_info(IrinfoTpdInfo_t* tpd_info, IrRect_t rect_pos, \
	LineRectTempData_t* rect_temp_value)
{
	if (tpd_info == NULL || rect_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("tpd_info or rect_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	if (tpd_info->rect_tpd_0.line_rect_info_en == INFO_ON)
	{
		if ((tpd_info->rect_tpd_0.start_pixel.x == rect_pos.start_point.x) && \
			(tpd_info->rect_tpd_0.start_pixel.y == rect_pos.start_point.y) && \
			(tpd_info->rect_tpd_0.end_pixel.x == rect_pos.end_point.x) && \
			(tpd_info->rect_tpd_0.end_pixel.y == rect_pos.end_point.y))
		{
			rect_temp_value->max_min_temp_info.max_temp = tpd_info->rect_tpd_0.max_pixel_value;
			rect_temp_value->max_min_temp_info.min_temp = tpd_info->rect_tpd_0.min_pixel_value;
			rect_temp_value->ave_temp = tpd_info->rect_tpd_0.ave_pixel_value;
			rect_temp_value->max_min_temp_info.max_temp_point.x = tpd_info->rect_tpd_0.max_pixel.x;
			rect_temp_value->max_min_temp_info.max_temp_point.y = tpd_info->rect_tpd_0.max_pixel.y;
			rect_temp_value->max_min_temp_info.min_temp_point.x = tpd_info->rect_tpd_0.min_pixel.x;
			rect_temp_value->max_min_temp_info.min_temp_point.y = tpd_info->rect_tpd_0.min_pixel.y;
			return TEMP_MEASURE_SUCCESS;
		}
	}
	else if (tpd_info->rect_tpd_1.line_rect_info_en == INFO_ON)
	{
		if ((tpd_info->rect_tpd_1.start_pixel.x == rect_pos.start_point.x) && \
			(tpd_info->rect_tpd_1.start_pixel.y == rect_pos.start_point.y) && \
			(tpd_info->rect_tpd_1.end_pixel.x == rect_pos.end_point.x) && \
			(tpd_info->rect_tpd_1.end_pixel.y == rect_pos.end_point.y))
		{
			rect_temp_value->max_min_temp_info.max_temp = tpd_info->rect_tpd_1.max_pixel_value;
			rect_temp_value->max_min_temp_info.min_temp = tpd_info->rect_tpd_1.min_pixel_value;
			rect_temp_value->ave_temp = tpd_info->rect_tpd_1.ave_pixel_value;
			rect_temp_value->max_min_temp_info.max_temp_point.x = tpd_info->rect_tpd_1.max_pixel.x;
			rect_temp_value->max_min_temp_info.max_temp_point.y = tpd_info->rect_tpd_1.max_pixel.y;
			rect_temp_value->max_min_temp_info.min_temp_point.x = tpd_info->rect_tpd_1.min_pixel.x;
			rect_temp_value->max_min_temp_info.min_temp_point.y = tpd_info->rect_tpd_1.min_pixel.y;
			return TEMP_MEASURE_SUCCESS;
		}
	}

	IR_TEMP_MEASURE_DEBUG("use frame info to get rect temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_rect_temp_from_temp_frame(temp_measure_t* handle, IrRect_t rect_pos, \
	LineRectTempData_t* rect_temp_value)
{
	if (handle == NULL || rect_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("handle or rect_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	TempDataRes_t temp_res = { handle->temp_frame_info.temp_width, handle->temp_frame_info.temp_height };
	Area_t rect = { rect_pos.start_point.x, rect_pos.start_point.y, \
		rect_pos.end_point.x - rect_pos.start_point.x + 1, rect_pos.end_point.y - rect_pos.start_point.y + 1 };
	TempInfo_t temp_info = { 0 };
	IrlibError_e ret;
	ret = get_rect_temp((uint16_t*)(handle->temp_frame_info.temp_frame), temp_res, rect, &temp_info);
	if (ret == IRLIB_SUCCESS)
	{
		rect_temp_value->ave_temp = ((double)temp_info.avr_temp) / 64 - 273.15;
		rect_temp_value->max_min_temp_info.max_temp = ((double)temp_info.max_temp) / 64 - 273.15;
		rect_temp_value->max_min_temp_info.min_temp = ((double)temp_info.min_temp) / 64 - 273.15;
		rect_temp_value->max_min_temp_info.max_temp_point.x = temp_info.max_cord.x;
		rect_temp_value->max_min_temp_info.max_temp_point.y = temp_info.max_cord.y;
		rect_temp_value->max_min_temp_info.min_temp_point.x = temp_info.min_cord.x;
		rect_temp_value->max_min_temp_info.min_temp_point.y = temp_info.min_cord.y;
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use temp frame to get rect temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e get_rect_temp_from_vdcmd(IrcmdHandle_t* ircmd_handle, IrRect_t rect_pos, \
	LineRectTempData_t* rect_temp_value)
{
	if (ircmd_handle == NULL || rect_temp_value == NULL)
	{
		IR_TEMP_MEASURE_DEBUG("ircmd_handle or rect_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	IrlibError_e ret;
	IrcmdRect_t rect = { rect_pos.start_point.x, rect_pos.start_point.y, \
		rect_pos.end_point.x, rect_pos.end_point.y };
	LineRectTempInfo_t rect_temp_info = { 0 };
	ret = basic_rect_temp_info_get(ircmd_handle, rect, &rect_temp_info);
	if (ret == IRLIB_SUCCESS)
	{
		rect_temp_value->max_min_temp_info.max_temp = rect_temp_info.max_min_temp_info.max_temp;
		rect_temp_value->max_min_temp_info.min_temp = rect_temp_info.max_min_temp_info.min_temp;
		rect_temp_value->ave_temp = rect_temp_info.ave_temp;
		rect_temp_value->max_min_temp_info.max_temp_point.x = rect_temp_info.max_min_temp_info.max_temp_point.x;
		rect_temp_value->max_min_temp_info.max_temp_point.y = rect_temp_info.max_min_temp_info.max_temp_point.y;
		rect_temp_value->max_min_temp_info.min_temp_point.x = rect_temp_info.max_min_temp_info.min_temp_point.x;
		rect_temp_value->max_min_temp_info.min_temp_point.y = rect_temp_info.max_min_temp_info.min_temp_point.y;
		return TEMP_MEASURE_SUCCESS;
	}

	IR_TEMP_MEASURE_DEBUG("use vdcmd to get rect temp failed");
	return TEMP_MEASURE_PROCESS_FAIL;
}


temp_measure_error_e temp_measure_get_rect_temp(temp_measure_t* handle, IrRect_t rect_pos, \
	LineRectTempData_t* rect_temp_value)
{
	if (handle == NULL || rect_temp_value == NULL)
	{
		IR_TEMP_MEASURE_ERROR("handle or rect_temp_value is NULL");
		return TEMP_MEASURE_ERROR_PARAM;
	}
	if (rect_pos.start_point.x < 0 || rect_pos.start_point.y < 0 ||
		rect_pos.start_point.x > handle->temp_frame_info.temp_width ||
		rect_pos.start_point.y > handle->temp_frame_info.temp_height)
	{
		IR_TEMP_MEASURE_ERROR("rect_pos is invalid");
		return TEMP_MEASURE_ERROR_PARAM;
	}
	if (rect_pos.end_point.x < 0 || rect_pos.end_point.y < 0 ||
		rect_pos.end_point.x > handle->temp_frame_info.temp_width ||
		rect_pos.end_point.y > handle->temp_frame_info.temp_height)
	{
		IR_TEMP_MEASURE_ERROR("rect_pos is invalid");
		return TEMP_MEASURE_ERROR_PARAM;
	}

	temp_measure_error_e ret;
	ret = get_rect_temp_from_temp_frame(handle, rect_pos, rect_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use temp frame to get rect temp successfully");
		return ret;
	}

	ret = get_rect_temp_from_frame_info(handle->tpd_info, rect_pos, rect_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use frame info to get rect temp successfully");
		return ret;
	}

	ret = get_rect_temp_from_vdcmd(handle->ircmd_handle, rect_pos, rect_temp_value);
	if (ret == TEMP_MEASURE_SUCCESS)
	{
		IR_TEMP_MEASURE_DEBUG("use vdcmd to get rect temp successfully");
		return ret;
	}

	IR_TEMP_MEASURE_DEBUG("fail to get rect temp");
	return TEMP_MEASURE_PROCESS_FAIL;
}

void* temp_measure_function(void* threadarg)
{
	printf("temp_measure_function start\n");

	StreamFrameInfo_t* stream_frame_info;
	stream_frame_info = (StreamFrameInfo_t*)threadarg;
	if (stream_frame_info == NULL)
	{
		return NULL;
	}

	int cmd;
	temp_measure_t handle;
	MaxMinTempData_t frame_temp_value;
	float point_temp_value;
	IrPoint_t point_pos;
	IrLine_t line_pos;
	IrRect_t rect_pos;
	IrinfoFuncInfo_t function_info;
	LineRectTempData_t line_temp_value;
	memset(&line_temp_value, 0, sizeof(line_temp_value));
	LineRectTempData_t rect_temp_value;
	memset(&rect_temp_value, 0, sizeof(rect_temp_value));
	frame_format_e frame_format = TEMP_MEASURE_IMAGE_AND_TEMP;
	temp_format_e temp_format = TEMP_FRAME_FMT_Y16;

	if (stream_frame_info->frame_output_format == YUYV_IMAGE || stream_frame_info->frame_output_format == NV12_IMAGE)
	{
		frame_format = TEMP_MEASURE_ONLY_IMAGE;
	}
	else if (stream_frame_info->frame_output_format == NV12_AND_TEMP || stream_frame_info->frame_output_format == YUYV_AND_TEMP)
	{
		frame_format = TEMP_MEASURE_IMAGE_AND_TEMP;
	}
	init_temp_measure_handle(&handle, &(function_info.tpd_info), \
		stream_frame_info->ircmd_handle, \
		stream_frame_info->temp_info.height, \
		stream_frame_info->temp_info.width, \
		frame_format, \
		temp_format);
	handle.temp_frame_info.temp_frame = stream_frame_info->temp_info.data;

	//init temp correct env
	float org_temp = 100;
	float new_temp = 0;
	uint32_t table_len = 0;
	uint16_t correct_table[HEAD_SIZE + 45 * 88];

	EnvCorrectParam env_correct_param = { 0 };
	env_correct_param.dist = 0.25;
	env_correct_param.ems = 0.98;
	env_correct_param.hum = 0.45;
	env_correct_param.ta = 25;
	env_correct_param.tu = 25;

	FILE* fp = fopen("V265_mini256without_dust_tablets_L.bin", "rb");
	if (fp == NULL)
	{
		printf("V265_mini256without_dust_tablets_L.bin failed\n");
		return NULL;
	}
	fread(correct_table, 1, sizeof(correct_table), fp);
	fclose(fp);

	while (isRUNNING)
	{
#if defined(_WIN32)
		WaitForSingleObject(temp_sem, INFINITE);
#elif defined (linux)||(unix)
		sem_wait(&temp_sem);
#endif


		printf("--------------------------please select way of temp_measure--------------------------\n");
		printf("1:get_frame_temp\n");
		printf("2:get_point_temp\n");
		printf("3:get_line_temp\n");
		printf("4:get_rect_temp\n");
		printf("-------------------------------------------------------------------------------------\n");
		scanf("%d", &cmd);

		switch (cmd)
		{
		case 1:
			temp_measure_on = 1;
			temp_measure_get_frame_temp(&handle, &frame_temp_value);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, frame_temp_value.max_temp, &new_temp);
			printf("frame_temp_value.max_temp is %f\n", new_temp);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, frame_temp_value.min_temp, &new_temp);
			printf("frame_temp_value.min_temp is %f\n", new_temp);
			printf("frame_temp_value->max_temp_point.x = %d\n", frame_temp_value.max_temp_point.x);
			printf("frame_temp_value->max_temp_point.y = %d\n", frame_temp_value.max_temp_point.y);
			printf("frame_temp_value->min_temp_point.x = %d\n", frame_temp_value.min_temp_point.x);
			printf("frame_temp_value->min_temp_point.y = %d\n", frame_temp_value.min_temp_point.y);
			break;
		case 2:
			printf("Please enter point coordinate\n");
			scanf("%hd %hd", &(point_pos.x), &(point_pos.y));
			temp_measure_on = 1;
			temp_measure_get_point_temp(&handle, point_pos, &point_temp_value);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, point_temp_value, &new_temp);
			printf("point_temp_value is %f\n", new_temp);
			break;
		case 3:
			printf("Please enter line coordinate\n");
			printf("Please enter start point coordinate\n");
			scanf("%hd %hd", &(line_pos.start_point.x), &(line_pos.start_point.y));
			printf("Please enter end point coordinate\n");
			scanf("%hd %hd", &(line_pos.end_point.x), &(line_pos.end_point.y));
			temp_measure_on = 1;
			temp_measure_get_line_temp(&handle, line_pos, &line_temp_value);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, line_temp_value.ave_temp, &new_temp);
			printf("ave_temp is %f\n", new_temp);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, line_temp_value.max_min_temp_info.max_temp, &new_temp);
			printf("max_temp is %f\n", new_temp);
			printf("max_temp_point_coordinate is ( %d , %d )\n", line_temp_value.max_min_temp_info.max_temp_point.x, \
				line_temp_value.max_min_temp_info.max_temp_point.y);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, line_temp_value.max_min_temp_info.min_temp, &new_temp);
			printf("min_temp is %f\n", new_temp);
			printf("min_temp_point_coordinate is ( %d , %d )\n", line_temp_value.max_min_temp_info.min_temp_point.x, \
				line_temp_value.max_min_temp_info.min_temp_point.y);
			break;
		case 4:
			printf("Please enter rect coordinate\n");
			printf("Please enter start point coordinate\n");
			scanf("%hd %hd", &(rect_pos.start_point.x), &(rect_pos.start_point.y));
			printf("Please enter end point coordinate\n");
			scanf("%hd %hd", &(rect_pos.end_point.x), &(rect_pos.end_point.y));
			temp_measure_on = 1;
			temp_measure_get_rect_temp(&handle, rect_pos, &rect_temp_value);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, rect_temp_value.ave_temp, &new_temp);
			printf("ave_temp is %f\n", new_temp);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, rect_temp_value.max_min_temp_info.max_temp, &new_temp);
			printf("max_temp is %f\n", new_temp);
			printf("max_temp_point_coordinate is ( %d , %d )\n", rect_temp_value.max_min_temp_info.max_temp_point.x, \
				rect_temp_value.max_min_temp_info.max_temp_point.y);
			enhance_distance_temp_correct(&env_correct_param, correct_table, table_len, rect_temp_value.max_min_temp_info.min_temp, &new_temp);
			printf("min_temp is %f\n", new_temp);
			printf("min_temp_point_coordinate is ( %d , %d )\n", rect_temp_value.max_min_temp_info.min_temp_point.x, \
				rect_temp_value.max_min_temp_info.min_temp_point.y);
			break;
		default:
			printf("param is invalid!\n");
			break;
		}

		temp_measure_on = 0;
#if defined(_WIN32)
		ReleaseSemaphore(temp_done_sem, 1, NULL);
		ReleaseSemaphore(cmd_sem, 1, NULL);
#elif defined (linux)||(unix)
		sem_post(&temp_done_sem);
		sem_post(&cmd_sem);
#endif
	}
	destroy_temp_measure_handle(&handle);

	return NULL;

}