#ifndef _OPENCV_DISPALAY_H_
#define _OPENCV_DISPALAY_H_

#include <stdint.h>
#include "opencv2/opencv.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/highgui/highgui_c.h>
#include "data.h"
#include "libirparse.h"


#if defined(linux) || defined(unix)
#include <unistd.h>
#include <semaphore.h>
#include <sys/time.h>
#endif

//use opencv to display thread
void* opencv_display_function(void* threadarg);

#endif