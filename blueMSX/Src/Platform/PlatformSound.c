/*****************************************************************************
** blueMSX+ - Platform layer: sound output
**
** The mixer pushes fixed size fragments; they go into an SDL3 audio stream,
** which does the buffering and any format conversion. To keep latency bounded
** a fragment is dropped when more than twice the configured buffer is queued
** (the emulation is running ahead of the sound card).
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "PlatformInternal.h"
#include "ArchSound.h"
#include "AudioMixer.h"

#include <SDL3/SDL.h>

static struct {
    SDL_AudioStream* stream;
    Mixer* mixer;
    int    bytesPerFrame;
    int    maxQueued;       /* bytes */
    int    suspended;
} snd;

static Int32 soundWrite(void* ref, Int16* buffer, UInt32 count)
{
    int bytes = (int)count * (int)sizeof(Int16);

    if (snd.stream == NULL || snd.suspended) {
        return 0;
    }
    if (SDL_GetAudioStreamQueued(snd.stream) > snd.maxQueued) {
        return 0;
    }
    SDL_PutAudioStreamData(snd.stream, buffer, bytes);
    return 0;
}

/* While a replay renders to video the mix goes to the encoder only. */
static MixerWriteCallback redirect;

void platformSoundRedirect(MixerWriteCallback callback)
{
    redirect = callback;
}

void archSoundCreate(Mixer* mixer, UInt32 sampleRate, UInt32 bufferSize, Int16 channels)
{
    SDL_AudioSpec spec;
    int fragmentFrames = 256;

    archSoundDestroy();

    if (redirect != NULL) {
        snd.mixer = mixer;
        mixerSetStereo(mixer, 1);
        mixerSetWriteCallback(mixer, redirect, NULL, 512);
        return;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("blueMSX+: audio init failed: %s", SDL_GetError());
        return;
    }

    spec.format   = SDL_AUDIO_S16;
    spec.channels = channels;
    spec.freq     = (int)sampleRate;

    snd.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (snd.stream == NULL) {
        SDL_Log("blueMSX+: cannot open audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    /* bufferSize is in milliseconds. Fragments of ~1/4 of it, at least 64
    ** frames, keep the queue fed smoothly. */
    while (fragmentFrames * 4 < (int)(bufferSize * sampleRate / 1000) && fragmentFrames < 4096) {
        fragmentFrames *= 2;
    }
    while (fragmentFrames > 64 && fragmentFrames * 4 > (int)(bufferSize * sampleRate / 1000)) {
        fragmentFrames /= 2;
    }

    snd.mixer         = mixer;
    snd.bytesPerFrame = channels * (int)sizeof(Int16);
    snd.maxQueued     = 2 * (int)(bufferSize * sampleRate / 1000) * snd.bytesPerFrame;
    if (snd.maxQueued < 4 * fragmentFrames * snd.bytesPerFrame) {
        snd.maxQueued = 4 * fragmentFrames * snd.bytesPerFrame;
    }
    snd.suspended = 0;

    mixerSetStereo(mixer, channels == 2);
    mixerSetWriteCallback(mixer, soundWrite, NULL, fragmentFrames * channels);

    SDL_ResumeAudioStreamDevice(snd.stream);
}

void archSoundDestroy(void)
{
    if (snd.stream == NULL && snd.mixer != NULL) {
        mixerSetWriteCallback(snd.mixer, NULL, NULL, 0);     /* redirected */
    }
    if (snd.stream != NULL) {
        if (snd.mixer != NULL) {
            mixerSetWriteCallback(snd.mixer, NULL, NULL, 0);
        }
        SDL_DestroyAudioStream(snd.stream);
        snd.stream = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    snd.mixer = NULL;
}

void archSoundResume(void)
{
    snd.suspended = 0;
    if (snd.stream != NULL) {
        SDL_ResumeAudioStreamDevice(snd.stream);
    }
}

void archSoundSuspend(void)
{
    snd.suspended = 1;
    if (snd.stream != NULL) {
        SDL_PauseAudioStreamDevice(snd.stream);
        SDL_ClearAudioStream(snd.stream);
    }
}
