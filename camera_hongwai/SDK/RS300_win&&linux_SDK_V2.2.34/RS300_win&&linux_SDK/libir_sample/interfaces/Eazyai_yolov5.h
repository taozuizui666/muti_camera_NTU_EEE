#ifndef _EAZYAI_YOLOV5_H_
#define _EAZYAI_YOLOV5_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MAXNUM 20

int Eazyai_yolov5_init(char* model_name,int ImgStartY);

typedef struct {
    uint8_t*     BaseAddrY;      //Y分量
    uint8_t*     BaseAddrUV;		//UV分量
} SVC_LIV_IMG_s; 
typedef struct {
    unsigned short cat;   //类别
    unsigned short  obj;   //ID号
    unsigned short  score; //置信度*255
    unsigned short  x;     //检测框左上角x轴坐标
    unsigned short  y;     //检测框左上角y轴坐标
    unsigned short  w;     //检测框宽度
    unsigned short  h;     //检测框高度
} TagBbx;
typedef struct {
    unsigned short  num;   //当前帧检测框总数
    TagBbx bbx[MAXNUM];
} TagInfo;
typedef enum{
    DetDrawRect_Black,
    DetDrawRect_White,
    DetDrawRect_Blue,
    DetDrawRect_Red,
    DetDrawRect_Rainbow 
}DetDrawRectInfColorMode;
/*
运行
@input：
pImgNV12   NV12图像信息
pImgUYVY   原始UYVY图像信息
mode       颜色模式
@output：
pDet       检测框信息
pImgUYVY   带框的UYVY图像信息
@ret：
返回状态信息
*/
int Eazyai_yolov5_run(SVC_LIV_IMG_s* pImgNV12, TagInfo* pDet,uint8_t* pImgUYVY,DetDrawRectInfColorMode mode);

int Eazyai_yolov5_release();

#ifdef __cplusplus
}
#endif

#endif
