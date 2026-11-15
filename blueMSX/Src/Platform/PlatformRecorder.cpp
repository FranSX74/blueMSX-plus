/*****************************************************************************
** blueMSX+ portable platform layer - live video recording with FFmpeg
**
** File > Record video writes an MP4: H.264 (or HEVC, Properties
** capture.videoCodec) at 1280x960 and AAC audio, like the Windows recorder.
**   - Video: the frontend hands in each frame it shows. A frontend with a
**     shader renderer gives the processed picture (scanlines, CRT effects;
**     platformRecorderSetPostRender), otherwise the software rendered frame
**     is taken in platformRenderFrame. Timestamps come from the emulated
**     time, so pauses and menus leave no frozen stretch, and 50 Hz machines
**     record at 50 fps.
**   - Audio: the mixer's master mix, through its tap (emulator thread).
** Encoding runs on a worker thread fed by a bounded queue; video frames are
** dropped rather than slowing the emulation down.
**
** Without FFmpeg at build time recording is not offered.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
extern "C" {
#include "Platform.h"
#include "Frontend.h"
#include "PlatformInternal.h"
#include "Properties.h"
#include "AudioMixer.h"
#include "Board.h"
#include "Actions.h"
#include "FileHistory.h"
#include "ArchNotifications.h"
#include "Language.h"
}

#include <cstdio>
#include <cstring>
#include <string>

#ifdef BLUEMSX_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {

const int REC_WIDTH  = 1280;
const int REC_HEIGHT = 960;
const int TIME_BASE  = 90000;           /* video timestamps, ticks per second */
const size_t MAX_QUEUED_FRAMES = 8;     /* ~130 ms of video */

struct VideoItem {
    std::vector<uint8_t> pixels;        /* REC_WIDTH x REC_HEIGHT BGRA */
    int64_t pts;
};

struct Recorder {
    std::string      fileName;
    AVFormatContext* fmt = nullptr;
    AVCodecContext*  venc = nullptr;
    AVCodecContext*  aenc = nullptr;
    AVStream*        vst = nullptr;
    AVStream*        ast = nullptr;
    SwsContext*      sws = nullptr;
    SwrContext*      swr = nullptr;
    AVAudioFifo*     fifo = nullptr;
    AVFrame*         vframe = nullptr;
    AVFrame*         aframe = nullptr;
    int64_t          audioPts = 0;
    int              sampleRate = AUDIO_SAMPLERATE;

    std::mutex              lock;
    std::condition_variable wake;
    std::deque<VideoItem>   video;
    std::vector<int16_t>    audio;          /* interleaved stereo */
    bool                    stopping = false;
    std::thread             worker;

    /* Video clock: emulated cycles since the start. */
    bool    originSet = false;
    UInt32  lastTime = 0;
    uint64_t cycles = 0;
    int64_t lastPts = -1;
    std::vector<uint8_t> scaled;            /* conversion scratch, UI thread */
    SwsContext* inputSws = nullptr;
    int inW = 0, inH = 0, inFormat = 0;
};

Recorder*         rec;
std::atomic<int>  active(0);
int               postRender = 0;

void logError(const char* what, int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = "";
    av_strerror(err, buf, sizeof(buf));
    fprintf(stderr, "blueMSX+: recording: %s: %s\n", what, buf);
}

int writePackets(Recorder* r, AVCodecContext* enc, AVStream* st)
{
    AVPacket* pkt = av_packet_alloc();
    int err;
    while ((err = avcodec_receive_packet(enc, pkt)) == 0) {
        av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
        pkt->stream_index = st->index;
        err = av_interleaved_write_frame(r->fmt, pkt);
        if (err < 0) {
            logError("write", err);
            break;
        }
    }
    av_packet_free(&pkt);
    return err == AVERROR(EAGAIN) || err == AVERROR_EOF ? 0 : err;
}

void encodeVideo(Recorder* r, const VideoItem* item)
{
    if (item != nullptr) {
        if (av_frame_make_writable(r->vframe) < 0) return;
        const uint8_t* src[1] = { item->pixels.data() };
        int stride[1] = { REC_WIDTH * 4 };
        sws_scale(r->sws, src, stride, 0, REC_HEIGHT, r->vframe->data, r->vframe->linesize);
        r->vframe->pts = item->pts;
    }
    if (avcodec_send_frame(r->venc, item ? r->vframe : nullptr) == 0) {
        writePackets(r, r->venc, r->vst);
    }
}

/* Feeds whole AAC frames from the FIFO; at the end, the rest padded. */
void encodeAudio(Recorder* r, bool flush)
{
    int frameSize = r->aenc->frame_size > 0 ? r->aenc->frame_size : 1024;
    while (av_audio_fifo_size(r->fifo) >= frameSize || (flush && av_audio_fifo_size(r->fifo) > 0)) {
        int n = av_audio_fifo_size(r->fifo) < frameSize ? av_audio_fifo_size(r->fifo) : frameSize;
        if (av_frame_make_writable(r->aframe) < 0) return;
        if (n < frameSize) {
            av_samples_set_silence(r->aframe->data, 0, frameSize, 2, r->aenc->sample_fmt);
        }
        av_audio_fifo_read(r->fifo, (void**)r->aframe->data, n);
        r->aframe->pts = r->audioPts;
        r->audioPts += frameSize;
        if (avcodec_send_frame(r->aenc, r->aframe) == 0) {
            writePackets(r, r->aenc, r->ast);
        }
    }
    if (flush && avcodec_send_frame(r->aenc, nullptr) == 0) {
        writePackets(r, r->aenc, r->ast);
    }
}

void pushAudio(Recorder* r, const int16_t* pcm, int frames)
{
    uint8_t* planes[2];
    std::vector<float> left(frames + 32), right(frames + 32);
    planes[0] = (uint8_t*)left.data();
    planes[1] = (uint8_t*)right.data();
    const uint8_t* in[1] = { (const uint8_t*)pcm };
    int got = swr_convert(r->swr, planes, frames + 32, in, frames);
    if (got > 0) av_audio_fifo_write(r->fifo, (void**)planes, got);
}

void workerLoop(Recorder* r)
{
    for (;;) {
        std::deque<VideoItem> frames;
        std::vector<int16_t>  pcm;
        bool stop;
        {
            std::unique_lock<std::mutex> l(r->lock);
            r->wake.wait(l, [r]() { return r->stopping || !r->video.empty() || r->audio.size() >= 4096; });
            frames.swap(r->video);
            pcm.swap(r->audio);
            stop = r->stopping;
        }
        if (!pcm.empty()) {
            pushAudio(r, pcm.data(), (int)(pcm.size() / 2));
            encodeAudio(r, false);
        }
        for (const VideoItem& f : frames) encodeVideo(r, &f);
        if (stop) break;
    }
    encodeVideo(r, nullptr);
    encodeAudio(r, true);
    av_write_trailer(r->fmt);
}

/* Mixer tap, emulator thread: always interleaved stereo 16 bit. */
Int32 audioTap(void*, Int16* buffer, UInt32 count)
{
    Recorder* r = rec;
    if (!active.load() || r == nullptr) return 0;
    {
        std::lock_guard<std::mutex> l(r->lock);
        r->audio.insert(r->audio.end(), buffer, buffer + count);
    }
    r->wake.notify_one();
    return 0;
}

AVCodecContext* openVideoEncoder(Recorder* r, int codec, int fps)
{
    const char* names[] = { codec == CAP_VIDEO_HEVC ? "libx265" : "libx264", nullptr };
    const AVCodec* c = avcodec_find_encoder_by_name(names[0]);
    if (c == nullptr) c = avcodec_find_encoder(codec == CAP_VIDEO_HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (c == nullptr) c = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (c == nullptr) return nullptr;

    AVCodecContext* enc = avcodec_alloc_context3(c);
    enc->width     = REC_WIDTH;
    enc->height    = REC_HEIGHT;
    enc->pix_fmt   = AV_PIX_FMT_YUV420P;
    enc->time_base = AVRational{ 1, TIME_BASE };
    enc->framerate = AVRational{ fps, 1 };
    enc->gop_size  = fps * 2;
    if (r->fmt->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (strcmp(c->name, "libx264") == 0 || strcmp(c->name, "libx265") == 0) {
        /* Fast enough to keep up while emulating; CRF keeps pixel art clean. */
        av_opt_set(enc->priv_data, "preset", "veryfast", 0);
        av_opt_set(enc->priv_data, "crf", "18", 0);
        if (strcmp(c->name, "libx265") == 0) av_opt_set(enc->priv_data, "tag", "hvc1", 0);
    }
    else {
        enc->bit_rate = 8000000;
    }
    if (avcodec_open2(enc, c, nullptr) < 0) {
        avcodec_free_context(&enc);
        return nullptr;
    }
    return enc;
}

AVCodecContext* openAudioEncoder(Recorder* r, int kbps)
{
    const AVCodec* c = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (c == nullptr) return nullptr;
    AVCodecContext* enc = avcodec_alloc_context3(c);
    enc->sample_rate = r->sampleRate;
    enc->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    enc->bit_rate    = (kbps > 0 ? kbps : 192) * 1000;
    enc->time_base   = AVRational{ 1, r->sampleRate };
    av_channel_layout_default(&enc->ch_layout, 2);
    if (r->fmt->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc, c, nullptr) < 0) {
        avcodec_free_context(&enc);
        return nullptr;
    }
    return enc;
}

void destroy(Recorder* r)
{
    if (r == nullptr) return;
    if (r->venc) avcodec_free_context(&r->venc);
    if (r->aenc) avcodec_free_context(&r->aenc);
    if (r->fmt) {
        if (!(r->fmt->oformat->flags & AVFMT_NOFILE) && r->fmt->pb) avio_closep(&r->fmt->pb);
        avformat_free_context(r->fmt);
    }
    sws_freeContext(r->sws);
    sws_freeContext(r->inputSws);
    swr_free(&r->swr);
    if (r->fifo) av_audio_fifo_free(r->fifo);
    av_frame_free(&r->vframe);
    av_frame_free(&r->aframe);
    delete r;
}

Recorder* create(const char* fileName, const Properties* p)
{
    Recorder* r = new Recorder;
    r->fileName = fileName;
    Mixer* mixer = mixerGetGlobalMixer();
    if (mixer != nullptr && mixerGetSampleRate(mixer) > 0) r->sampleRate = (int)mixerGetSampleRate(mixer);

    int err = avformat_alloc_output_context2(&r->fmt, nullptr, "mp4", fileName);
    if (err < 0 || r->fmt == nullptr) {
        logError("output", err);
        destroy(r);
        return nullptr;
    }
    r->venc = openVideoEncoder(r, p->capture.videoCodec, 60);
    r->aenc = openAudioEncoder(r, p->capture.audioBitrateKbps);
    if (r->venc == nullptr || r->aenc == nullptr) {
        fprintf(stderr, "blueMSX+: recording: no %s encoder\n", r->venc ? "AAC" : "video");
        destroy(r);
        return nullptr;
    }
    r->vst = avformat_new_stream(r->fmt, nullptr);
    avcodec_parameters_from_context(r->vst->codecpar, r->venc);
    r->vst->time_base = r->venc->time_base;
    r->ast = avformat_new_stream(r->fmt, nullptr);
    avcodec_parameters_from_context(r->ast->codecpar, r->aenc);
    r->ast->time_base = r->aenc->time_base;

    if ((err = avio_open(&r->fmt->pb, fileName, AVIO_FLAG_WRITE)) < 0) {
        logError(fileName, err);
        destroy(r);
        return nullptr;
    }
    if ((err = avformat_write_header(r->fmt, nullptr)) < 0) {
        logError("header", err);
        destroy(r);
        return nullptr;
    }

    r->sws = sws_getContext(REC_WIDTH, REC_HEIGHT, AV_PIX_FMT_BGRA, REC_WIDTH, REC_HEIGHT, AV_PIX_FMT_YUV420P,
                            SWS_BICUBIC, nullptr, nullptr, nullptr);
    AVChannelLayout stereo;
    av_channel_layout_default(&stereo, 2);
    swr_alloc_set_opts2(&r->swr, &stereo, AV_SAMPLE_FMT_FLTP, r->sampleRate,
                        &stereo, AV_SAMPLE_FMT_S16, r->sampleRate, 0, nullptr);
    swr_init(r->swr);
    r->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, r->sampleRate);

    r->vframe = av_frame_alloc();
    r->vframe->format = AV_PIX_FMT_YUV420P;
    r->vframe->width  = REC_WIDTH;
    r->vframe->height = REC_HEIGHT;
    av_frame_get_buffer(r->vframe, 0);
    r->aframe = av_frame_alloc();
    r->aframe->format      = AV_SAMPLE_FMT_FLTP;
    r->aframe->sample_rate = r->sampleRate;
    r->aframe->nb_samples  = r->aenc->frame_size > 0 ? r->aenc->frame_size : 1024;
    av_channel_layout_default(&r->aframe->ch_layout, 2);
    av_frame_get_buffer(r->aframe, 0);
    return r;
}

/* Emulated time of the frame being shown, in TIME_BASE ticks from the start.
** A hard reset restarts the board clock: a jump over half a second counts
** as one frame. */
int64_t videoPts(Recorder* r)
{
    UInt32 now = boardSystemTime();
    if (!r->originSet) {
        r->originSet = true;
        r->lastTime = now;
        return 0;
    }
    UInt32 delta = now - r->lastTime;
    if (delta > boardFrequency() / 2) delta = boardFrequency() / 60;
    r->lastTime = now;
    r->cycles += delta;
    return (int64_t)(r->cycles / boardFrequency()) * TIME_BASE +
           (int64_t)((r->cycles % boardFrequency()) * TIME_BASE / boardFrequency());
}

}

extern "C" {

int platformRecorderAvailable(void) { return 1; }
int platformRecorderActive(void)    { return active.load(); }
void platformRecorderSetPostRender(int on) { postRender = on; }
int platformRecorderPostRender(void) { return postRender; }

void platformRecorderAddFrame(const void* pixels, int width, int height, int pitch, int format)
{
    Recorder* r = rec;
    if (!active.load() || r == nullptr) return;

    int64_t pts = videoPts(r);
    if (pts <= r->lastPts) return;          /* the emulation has not moved on */

    {
        std::lock_guard<std::mutex> l(r->lock);
        if (r->video.size() >= MAX_QUEUED_FRAMES) return;   /* encoder behind: drop */
    }
    r->lastPts = pts;

    AVPixelFormat src = format == PLATFORM_PIXELS_RGBA ? AV_PIX_FMT_RGBA : AV_PIX_FMT_BGRA;
    if (r->inputSws == nullptr || r->inW != width || r->inH != height || r->inFormat != format) {
        sws_freeContext(r->inputSws);
        /* Pixel art: nearest when enlarging the plain frame; area when a
        ** shader frame already has the size. */
        r->inputSws = sws_getContext(width, height, src, REC_WIDTH, REC_HEIGHT, AV_PIX_FMT_BGRA,
                                     width < REC_WIDTH ? SWS_POINT : SWS_AREA, nullptr, nullptr, nullptr);
        r->inW = width;
        r->inH = height;
        r->inFormat = format;
    }
    VideoItem item;
    item.pixels.resize((size_t)REC_WIDTH * REC_HEIGHT * 4);
    item.pts = pts;
    const uint8_t* in[1] = { (const uint8_t*)pixels };
    int inStride[1] = { pitch };
    uint8_t* out[1] = { item.pixels.data() };
    int outStride[1] = { REC_WIDTH * 4 };
    sws_scale(r->inputSws, in, inStride, 0, height, out, outStride);
    {
        std::lock_guard<std::mutex> l(r->lock);
        r->video.push_back(std::move(item));
    }
    r->wake.notify_one();
}

void archRecordVideoStart(const char* overrideFilename)
{
    if (active.load()) return;
    Properties* p = platformGetProperties();
    std::string name;
    if (overrideFilename != NULL && overrideFilename[0]) {
        name = overrideFilename;
    }
    else {
        const char* dir = actionGetVideoCaptureDir();
        char* fn = generateSaveFilename(p, (char*)(dir && dir[0] ? dir : "."), (char*)"", (char*)".mp4", 2);
        if (fn == NULL || fn[0] == 0) return;
        name = fn;
    }
    Recorder* r = create(name.c_str(), p);
    if (r == nullptr) {
        platformFrontendMessage(FRONTEND_MESSAGE_ERROR, "Cannot record video (FFmpeg encoder or file problem).");
        return;
    }
    rec = r;
    r->worker = std::thread(workerLoop, r);
    active.store(1);
    if (Mixer* mixer = mixerGetGlobalMixer()) mixerSetTapCallback(mixer, audioTap, NULL);
}

void archRecordVideoStop(void)
{
    if (!active.load()) return;
    active.store(0);
    if (Mixer* mixer = mixerGetGlobalMixer()) mixerSetTapCallback(mixer, NULL, NULL);
    Recorder* r = rec;
    {
        std::lock_guard<std::mutex> l(r->lock);
        r->stopping = true;
    }
    r->wake.notify_one();
    r->worker.join();
    rec = nullptr;
    std::string name = r->fileName;
    destroy(r);
    archCaptureToastSaved(name.c_str());
}

int archRecordVideoIsActive(void)
{
    return active.load();
}

}

#else   /* no FFmpeg */

extern "C" {
int  platformRecorderAvailable(void) { return 0; }
int  platformRecorderActive(void) { return 0; }
void platformRecorderSetPostRender(int) {}
int  platformRecorderPostRender(void) { return 0; }
void platformRecorderAddFrame(const void*, int, int, int, int) {}
void archRecordVideoStart(const char*)
{
    platformFrontendMessage(FRONTEND_MESSAGE_WARNING, "This build has no video recording (built without FFmpeg).");
}
void archRecordVideoStop(void) {}
int  archRecordVideoIsActive(void) { return 0; }
}

#endif
