/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     gst types def
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_TYPES_DEF_H
#define T_GST_TYPES_DEF_H
//==============================================================================

#include "awtk.h"
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/video/video.h>

//------------------------------------------------------------------------------

#define TG_QUERY_TIMER_INTERVAL (500) // 位置和时长上报时钟间隔，单位为毫秒

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY((x))

#define GST_SPECTRUM_BANDS (31)
#define GST_SPECTRUM_BAR_SIZE (GST_SPECTRUM_BANDS * sizeof(float))

//------------------------------------------------------------------------------

#define GST_PLUGINS_AAC_PARSE "aacparse"
#define GST_PLUGINS_ALSA_SINK "alsasink"
#define GST_PLUGINS_ALSA_SRC "alsasrc"
#define GST_PLUGINS_APP_SINK "appsink"
#define GST_PLUGINS_APP_SRC "appsrc"
#define GST_PLUGINS_AUDIO_RATE "audiorate"
#define GST_PLUGINS_AUTO_AUDIO_SINK "autoaudiosink"
#define GST_PLUGINS_AUDIO_CONVERT "audioconvert"
#define GST_PLUGINS_AUDIO_RESAMPLE "audioresample"
#define GST_PLUGINS_AUDIO_VOLUME "volume"
#define GST_PLUGINS_AVENC_AAC "avenc_aac"
#define GST_PLUGINS_BIN "bin"
#define GST_PLUGINS_BMP_DEC "avdec_bmp"
#define GST_PLUGINS_CAMERABIN "camerabin"
#define GST_PLUGINS_CAPS_FILTER "capsfilter"
#define GST_PLUGINS_DECODE_BIN "decodebin"
#define GST_PLUGINS_ENCODE_BIN "encodebin"
#define GST_PLUGINS_FAKE_SINK "fakesink"
#define GST_PLUGINS_FAKE_SRC "fakesrc"
#define GST_PLUGINS_FILE_SINK "filesink"
#define GST_PLUGINS_FILE_SRC "filesrc"
#define GST_PLUGINS_GIF_DEC "avdec_gif"
#define GST_PLUGINS_GIF_DEMUX "avdemux_gif"
#define GST_PLUGINS_H264_PARSE "h264parse"
#define GST_PLUGINS_IDENTITY "identity"
#define GST_PLUGINS_JPEG_DEC "jpegdec"
#define GST_PLUGINS_KSVIDEO_SRC "ksvideosrc"
#define GST_PLUGINS_LAME_MP3_ENC "lamemp3enc"
#define GST_PLUGINS_LEVEL "level"
#define GST_PLUGINS_MATROSKA_MUX "matroskamux"
#define GST_PLUGINS_MFVIDEOSRC "mfvideosrc"
#define GST_PLUGINS_MP4_MUX "mp4mux"
#define GST_PLUGINS_MPEG4_VIDEO_PARSE "mpeg4videoparse"
#define GST_PLUGINS_MPEG_TS_MUX "mpegtsmux"
#define GST_PLUGINS_MPP_H264_ENC "mpph264enc"
#define GST_PLUGINS_MPP_JPEG_DEC "mppjpegdec"
#define GST_PLUGINS_OPEN_H264_ENC "openh264enc"
#define GST_PLUGINS_PIPELINE "pipeline"
#define GST_PLUGINS_PLAYBIN "playbin"
#define GST_PLUGINS_PNG_DEC "avdec_png"
#define GST_PLUGINS_PULSE_SINK "pulsesink"
#define GST_PLUGINS_PULSE_SRC "pulsesrc"
#define GST_PLUGINS_QT_MUX "qtmux"
#define GST_PLUGINS_QUEUE "queue"
#define GST_PLUGINS_QUEUE2 "queue2"
#define GST_PLUGINS_RATE "rate"
#define GST_PLUGINS_SPECTRUM "spectrum"
#define GST_PLUGINS_TEE "tee"
#define GST_PLUGINS_TIFF_DEC "avdec_tiff"
#define GST_PLUGINS_V4L2_SRC "v4l2src"
#define GST_PLUGINS_VALVE "valve"
#define GST_PLUGINS_VIDEO_CONVERT "videoconvert"
#define GST_PLUGINS_VIDEO_CONVERT_SCALE "videoconvertscale"
#define GST_PLUGINS_VIDEO_SCALE "videoscale"
#define GST_PLUGINS_VIDEO_IN "videoin"
#define GST_PLUGINS_VIDEO_RATE "videorate"
#define GST_PLUGINS_VO_AAC_ENC "voaacenc"
#define GST_PLUGINS_VOLUME "volume"
#define GST_PLUGINS_WASAPI_SINK "wasapisink"
#define GST_PLUGINS_WASAPI_SRC "wasapisrc"
#define GST_PLUGINS_WAV_ENC "wavenc"
#define GST_PLUGINS_WEBP_DEC "avdec_webp"
#define GST_PLUGINS_X264_ENC "x264enc"

#define NEW_SAMPLE "new-sample"
#define PULL_SAMPLE "pull-sample"
#define PAD_ADDED "pad-added"

//------------------------------------------------------------------------------

#define T_EVENT_MAKE(_target, _event, _type)                                   \
    _event evt;                                                                \
    event_t *e = (event_t *)&evt;                                              \
    memset(e, 0, sizeof(_event));                                              \
    e->size = sizeof(_event);                                                  \
    e->type = _type;                                                           \
    e->target = _target;                                                       \
    e->time = time_now_ms();

// 1. 保留原本的底层拼接宏（作为内部实现）
#define TG_REF_VTABLE(vt) &(s##vt##VTable)
#define TG_GET_VTABLE(vt) vt##GetGstVTable()
#define TG_GET_PARENT_VTABLE(vt) vt##GetGstVTable

#define TG_DECL_VTABLE(vt)                                                     \
    extern const TGstVTable s##vt##VTable;                                     \
    const TGstVTable *vt##GetGstVTable(void) {                                 \
        return &s##vt##VTable;                                                 \
    }                                                                          \
    const TGstVTable s##vt##VTable

#define TG_EXTERN_VTABLE(vt) const TGstVTable *vt##GetGstVTable(void)

#define TG_VTABLE_FUNC(vt, func, arges_list)                                   \
    {                                                                          \
        const TGstVTable *iter = TG_GET_VTABLE(vt);                            \
        if (iter->func) {                                                      \
            iter->func arges_list;                                             \
        }                                                                      \
    }

#define TG_VTABLE_FUNC_RET(vt, func, ret, arges_list)                          \
    {                                                                          \
        const TGstVTable *iter = TG_GET_VTABLE(vt);                            \
        if (iter->func) {                                                      \
            ret = iter->func arges_list;                                       \
        }                                                                      \
    }

//==============================================================================

typedef struct _TGst TGst;

typedef struct _TGstVTable TGstVTable;

typedef struct _TGstObject TGstObject;

typedef struct _TGstGroup TGstGroup;

typedef struct _TGstVideoFrame TGstVideoFrame;

typedef const TGstVTable *(*TGstGetVTable)(void);

typedef struct _TGstCapture TGstCapture;

typedef struct _TGstPlayback TGstPlayback;

typedef struct _TVideoPlayer TVideoPlayer;

typedef struct _TMusicPlayer TMusicPlayer;

typedef struct _TPictureViewer TPictureViewer;

typedef struct _TVideoRecorder TVideoRecorder;

typedef struct _TAudioRecorder TAudioRecorder;

//==============================================================================
#endif // T_GST_TYPES_DEF_H
//==============================================================================
