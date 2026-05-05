#ifndef _LIBIRCMD_TEMP_H_
#define _LIBIRCMD_TEMP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "libircam.h"
#include "libircmd.h"

//=====================================
//           Basic command
//======================================


/**
 * @brief Get the temperature value of a point
 *
 * @param[in] handle ircmd's handle
 * @param[in] point_pos the point's position
 * @param[out] point_temp_value the point temp value,Unit:Celsius
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e basic_point_temp_info_get(IrcmdHandle_t* handle, IrcmdPoint_t point_pos, float* point_temp_value);

/**
 * @brief Line struct, start from 1
 */
typedef struct {
    /// start point's position
    IrcmdPoint_t start_point;
    /// end point's position
    IrcmdPoint_t end_point;
}IrcmdLine_t;


/**
* @brief Get the temperature information of a line
*
* @param[in] handle ircmd's handle
* @param[in] line_pos the line's position
* @param[out] line_temp_info the temperature information of the line
*
* @return see IrlibError_e
*/
DLLEXPORT IrlibError_e basic_line_temp_info_get(IrcmdHandle_t* handle, IrcmdLine_t line_pos, LineRectTempInfo_t* line_temp_info);



/**
* @brief Get the temperature information of a rectangle
*
* @param[in] handle ircmd's handle
* @param[in] rect_pos the rectangle's position
* @param[out] rect_temp_info the temperature information of the rectangle
*
* @return see IrlibError_e
*/
DLLEXPORT IrlibError_e basic_rect_temp_info_get(IrcmdHandle_t* handle, IrcmdRect_t rect_pos, LineRectTempInfo_t* rect_temp_info);


/**
* @brief Get the temperature information of the whole frame
*
* @param[in] handle ircmd's handle
* @param[out] frame_temp_info the temperature information of the whole frame
*
* @return see IrlibError_e
*/
DLLEXPORT IrlibError_e basic_frame_temp_info_get(IrcmdHandle_t* handle, MaxMinTempInfo_t* frame_temp_info);


//=====================================
//           Advanced command
//======================================

/**
 * @brief Display the point's temperature on the screen.
 *
 * @param[in] handle ircmd's handle
 * @param[in] status enable or disable, see basic_enable_status_e
 * @param[in] point_idx point's index(0-4)
 * @param[in] point the position infomation of point
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_show_point_temp(IrcmdHandle_t* handle, int status, uint8_t point_idx, IrcmdPoint_t point);

/**
 * @brief Display the max and min frame temperature points on the screen.
 *
 * @param[in] handle ircmd's handle
 * @param[in] status enable or disable, see basic_enable_status_e
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_show_frame_temp(IrcmdHandle_t* handle, int status);

/**
 * @brief Set environment correct switch
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] status enable or disable, see basic_enable_status_e
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_switch_set(IrcmdHandle_t* handle, int status);

/**
 * @brief Get environment correct switch
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] status enable or disable, see basic_enable_status_e
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_switch_get(IrcmdHandle_t* handle, int* status);

/**
 * @brief Set tu in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] tu reflection temperature, range:233~373 (unit: kelvin degree celsius)
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_tu_set(IrcmdHandle_t* handle, int tu);

/**
 * @brief Get tu in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] tu reflection temperature, range:233~373 (unit: kelvin degree celsius)
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_tu_get(IrcmdHandle_t* handle, int* tu);

/**
 * @brief Set ta in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] ta atmospheric temperature, range:233~373 (unit: kelvin degree celsius)
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_ta_set(IrcmdHandle_t* handle, int ta);

/**
 * @brief Get ta in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] ta atmospheric temperature, range:233~373 (unit: kelvin degree celsius)
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_ta_get(IrcmdHandle_t* handle, int* ta);

/**
 * @brief Set tau in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] tau atmospheric transmittance, range:1~16384
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_tau_set(IrcmdHandle_t* handle, int tau);

/**
 * @brief Get tau in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] tau atmospheric transmittance, range:1~16384
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_tau_get(IrcmdHandle_t* handle, int* tau);

/**
 * @brief Set ems in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] ems target emissivity, range:1~16384
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_ems_set(IrcmdHandle_t* handle, int ems);

/**
 * @brief Get ems in environment correct
 * @support   Tiny2C
 *
 * @param[in] handle ircmd's handle
 * @param[in] ems target emissivity, range:1~16384
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_env_correct_ems_get(IrcmdHandle_t* handle, int* ems);


/**
 * @brief Hide the point's temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 * @param[in] point_idx the position infomation of point
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_hide_point_temp(IrcmdHandle_t* handle, enum preview_path path, uint8_t point_idx);
#endif


/**
 * @brief Display the line's temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 * @param[in] line_idx line's index.(0-5)
 * @param[in] line the position infomation of line
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_show_line_temp(IrcmdHandle_t* handle, enum preview_path path, uint8_t line_idx, IrcmdLine_t line);
#endif


/**
 * @brief Hide the line's temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 * @param[in] line_idx the position infomation of line
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_hide_line_temp(IrcmdHandle_t* handle, enum preview_path path, uint8_t line_idx);
#endif


/**
 * @brief Display the rectangle's temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 * @param[in] rect_idx rectangle's index.(0-5)
 * @param[in] rect the position infomation of rectangle
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_show_rect_temp(IrcmdHandle_t* handle, enum preview_path path, uint8_t rect_idx, IrcmdRect_t rect);
#endif


/**
 * @brief Hide the rectangle's temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 * @param[in] rect_idx the position infomation of rectangle
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_hide_rect_temp(IrcmdHandle_t* handle, enum preview_path path, uint8_t rect_idx);
#endif


/**
 * @brief Hide the whole frame temperature measurement.
 *
 * @param[in] handle ircmd's handle
 * @param[in] path the path of preview
 *
 * @return see IrlibError_e
 */
#ifndef COMMAND_IN_DEVELOPING
DLLEXPORT IrlibError_e adv_hide_frame_temp(IrcmdHandle_t* handle, enum preview_path path);
#endif


/**
 * @brief Set point coord
 *
 * @param[in] handle ircmd's handle
 * @param[in] index point index,range:0-8
 * @param[in] point_pos the point's position
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_tpd_point_coord_set(IrcmdHandle_t* handle, uint8_t index, IrcmdPoint_t point_pos);


/**
 * @brief Set line coord
 *
 * @param[in] handle ircmd's handle
 * @param[in] index line index,range:0-1
 * @param[in] line_pos the line's position
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_tpd_line_coord_set(IrcmdHandle_t* handle, uint8_t index, IrcmdLine_t line_pos);


/**
 * @brief Set rect coord
 *
 * @param[in] handle ircmd's handle
 * @param[in] index rect index.range:0-5
 * @param[in] rect_pos the rect's position
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_tpd_rect_coord_set(IrcmdHandle_t* handle, uint8_t index, IrcmdRect_t rect_pos);


/**
 * @brief Auto recalculate the kt_bt by 1 point's temperature
 *
 * @param[in] handle ircmd's handle
 * @param[in] temp the target black body's temperature when calibrating
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_1point(IrcmdHandle_t* handle, float temp);

/**
 * @brief Cancel tpd recal data
 *
 * @param[in]  handle ircmd's handle
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_1point_cancel(IrcmdHandle_t* handle);

/**
 * @brief Clear tpd recal data
 *
 * @param[in]  handle ircmd's handle
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_1point_clear(IrcmdHandle_t* handle);

/**
 * @brief Ktbt recalculate point index
 */
typedef enum
{
    /// the first point
    TPD_KTBT_RECAL_P1 = 0,
    /// the second point
    TPD_KTBT_RECAL_P2
}tpd_ktbt_recal_point_idx;

/**
 * @brief Auto recalculate the kt_bt by 2 points' temperature, set P1(low temp) first, then set P2(high temp)
 *
 * @param[in] handle ircmd's handle
 * @param[in] point_index choose P1 for low temp, P2 for high temp, see tpd_ktbt_recal_point_idx
 * @param[in] temp the target black body's temperature when calibrating
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_2point(IrcmdHandle_t* handle, int point_index, float temp);

/**
 * @brief Cancel thre resulet of kt_bt recalibration
 *
 * @param[in] handle ircmd's handle
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_2point_cancel(IrcmdHandle_t* handle);

/**
 * @brief Clear thre resulet of kt_bt recalibration
 *
 * @param[in] handle ircmd's handle
 *
 * @return see IrlibError_e
 */
DLLEXPORT IrlibError_e adv_recal_tpd_by_2point_clear(IrcmdHandle_t* handle);




#ifdef __cplusplus
}
#endif

#endif
