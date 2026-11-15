/*****************************************************************************
** blueMSX+ - Platform layer: internal helpers shared by its source files
******************************************************************************
*/
#ifndef PLATFORM_INTERNAL_H
#define PLATFORM_INTERNAL_H

#include "Frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Never NULL: an all-NULL table until platformInit. */
const FrontendHooks* platformHooks(void);

void platformInputInit(void);
void platformFrontendMouseCapture(int capture);
void platformFrontendMessage(FrontendMessageKind kind, const char* text);

#ifdef __cplusplus
}
#endif

#endif
