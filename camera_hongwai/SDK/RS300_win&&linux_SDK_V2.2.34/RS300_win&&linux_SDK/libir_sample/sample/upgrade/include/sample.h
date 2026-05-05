#include "libirupgrade.h"

#include "libiruart.h"
#include "libiruvc.h"
#include "libirdfu.h"
#include "libirupgrade.h"
#include "libircam.h"
#include "libircmd.h"
#if defined (_WIN32)
#include "windows.h"
#include "io.h"
#elif defined (unix) || (linux)
#include "unistd.h"
#include "libiri2c.h"
#include "libirv4l2.h"
#endif
