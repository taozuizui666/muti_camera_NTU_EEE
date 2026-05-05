#include "libirupgrade.h"

#include "libiruart.h"
#include "libiruvc.h"
#include "libirdfu.h"
#include "libirtemp.h"
#include "libircmd_temp.h"


#define NUC_TABLE 0
#define CORRECT_TABLE  1
#define CORRECT_TABLE_SIZE 4088

#define MULTI_POINT_CALIBRATION
//#define ENHANCE_DISTANCE_TEMP_CORRECT

/**
 * @brief file type the type of file that read or download
 */
typedef enum
{
    KT = 0,
    BT = 1,
    CORRECT = 2,
    NUC_T = 3,
}file_type;