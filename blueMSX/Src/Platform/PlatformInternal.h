/*****************************************************************************
** blueMSX+ - Platform layer: internal helpers shared by its source files
******************************************************************************
*/
#ifndef PLATFORM_INTERNAL_H
#define PLATFORM_INTERNAL_H

#include "Frontend.h"
#include "AudioMixer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Never NULL: an all-NULL table until platformInit. */
const FrontendHooks* platformHooks(void);
void platformInputShutdown(void);

void platformInputInit(void);
void platformFrontendMouseCapture(int capture);
void platformFrontendMessage(FrontendMessageKind kind, const char* text);

/* Sends the mix to `callback` instead of the sound card (NULL: back to it);
** takes effect when the sound restarts. */
void platformSoundRedirect(MixerWriteCallback callback);

#ifdef __cplusplus
}
#endif

#endif
