#ifndef _CAPTURE_
#define _CAPTURE_

#include "data.h"
#include <sys/timeb.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avio.h"
#include "libavutil/imgutils.h"


//capture thread, save the raw frame image to jpg
void *capture_function(void *threadarg);

#ifdef __cplusplus
}
#endif

#endif
