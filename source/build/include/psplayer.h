// PSP interface layer
// for the Build Engine

#ifndef build_interface_layer_
#define build_interface_layer_ PSP

#include "baselayer.h"
#include "compat.h"
#include "psp_inc.h"

extern int32_t maxrefreshfreq;

void exitsystem(int status);

static inline void idle_waitevent_timeout(uint32_t timeout) {}
static inline void idle_waitevent(void) {}
static inline void idle(void)
{
    sceKernelDelayThread(1000);
}

#else
#if (build_interface_layer_ != PSP)
#error "Already using the " build_interface_layer_ ". Can't now use PSP."
#endif
#endif // build_interface_layer_

