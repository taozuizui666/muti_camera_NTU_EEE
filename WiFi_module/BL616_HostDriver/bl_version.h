#ifndef _BL_VERSION_H_
#define _BL_VERSION_H_

#include "bl_signed_gen.h"

#define RELEASE_VERSION  "102-204"
#ifdef CFG_REL_SIGNED
#define RELEASE_SIGNED CFG_REL_SIGNED
#else
#define RELEASE_SIGNED "NULL"
#endif

static inline void bl_print_version(void)
{
    printk("BL Driver version: %s, Signed: %s\n", RELEASE_VERSION, RELEASE_SIGNED);
}


#endif /* _BL_VERSION_H_ */
