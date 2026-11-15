/*****************************************************************************
** blueMSX+ - stand-in for the Windows <basetsd.h> the skin engine includes
******************************************************************************
*/
#ifndef PLATFORM_COMPAT_BASETSD_H
#define PLATFORM_COMPAT_BASETSD_H

#include <stdint.h>

#ifndef LONG_PTR
typedef intptr_t  LONG_PTR;
typedef uintptr_t ULONG_PTR;
#endif

#endif
