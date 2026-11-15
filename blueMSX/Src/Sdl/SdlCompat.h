/* Force-included on non-Windows builds: maps the MSVC CRT names the core uses. */
#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H
#ifndef _WIN32
#include <stdio.h>
#include <string.h>
#define sprintf_s snprintf
#define _strdup   strdup
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#ifndef __cplusplus
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif
#endif
#endif
