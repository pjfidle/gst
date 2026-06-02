/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     A simple and lightweight playback framework based on GStreamer 1.0.
 *     Designed for efficient media pipeline management and low-latency rendering.
 */

#ifndef __GST_COMMON_H__
#define __GST_COMMON_H__

#include <stdbool.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_ALSA_SINK       "alsasink"
#define GST_APP_SINK        "appsink"
#define GST_AUDIO_RATE      "audiorate"
#define GST_AUTO_AUDIO_SINK "autoaudiosink"
#define GST_AUDIO_CONVERT   "audioconvert"
#define GST_AUDIO_RESAMPLE  "audioresample"
#define GST_AUDIO_VOLUME    "volume"
#define GST_BMP_DEC         "avdec_bmp"
#define GST_CAPS_FILTER     "capsfilter"
#define GST_DECODE_BIN      "decodebin"
#define GST_FAKE_SINK       "fakesink"
#define GST_FILE_SINK       "filesink"
#define GST_FILE_SRC        "filesrc"
#define GST_GIF_DEC         "avdec_gif"
#define GST_GIF_DEMUX       "avdemux_gif"
#define GST_JPEG_DEC        "jpegdec"
#define GST_KMS_SINK        "kmssink"
#define GST_PLAY_BIN        "playbin"
#define GST_PNG_DEC         "avdec_png"
#define GST_PULSE_SINK      "pulsesink"
#define GST_QT_MUX          "qtmux"
#define GST_QUEUE           "queue"
#define GST_QUEUE2          "queue2"
#define GST_TEE             "tee"
#define GST_TIFF_DEC        "avdec_tiff"
#define GST_VALVE           "valve"
#define GST_VIDEO_CONVERT   "videoconvert"
#define GST_VIDEO_RATE      "videorate"
#define GST_WEBP_DEC        "avdec_webp"

#define NEW_SAMPLE          "new-sample"
#define PULL_SAMPLE         "pull-sample"
#define PAD_ADDED           "pad-added"

typedef GstElement* GElementPtr;
typedef GstPad*     GPadPtr;
typedef GstCaps*    GCapsPtr;

typedef struct {
    GElementPtr     playbin;

    /* 媒体区块 */
    GstStreamCollection *collection;
    /* 音频流 */
    gchar *cur_audio_sid;
    /* 视频流 */
    gchar *cur_video_sid;
    /* 字幕流 */
    gchar *cur_text_sid;
    /* 媒体区块操作锁 */
    GMutex selection_lock;

    guint               bus_watch;
    guint               timeout;
    guint               new_sample;

    /* 当前播放状态 */
    GstState            current_state;
    /* 目标播放状态 */
    GstState            target_state;

    /* 缺失插件的消息 */
    GList              *missing;

    /* 缓冲中标志 */
    gboolean            buffering;

    /* 实时流标志 */
    gboolean            is_live;

    GstPlayTrickMode    trick_mode;
    gdouble             rate;
    gdouble             start_position;
    gboolean            accurate_seeks;
    gboolean            instant_rate_changes;
    gboolean            seeking;

    // 位置和总时长监听标识
    gint64          duration;
    gint64          position;

    gdouble         initial_file;
    gdouble         volume;

} GMediaPlayer;

typedef GMediaPlayer *GMediaPlayerPtr;

GPadPtr g_get_static_pad(GElementPtr element, const char* pad_name);
gboolean g_static_pad_link(GElementPtr element, const char* pad_name, GPadPtr target);

void g_audio_sink_init(GMediaContextPtr ctx, GElementPtr audio_volume, GElementPtr audio_sink);

#endif /* __GST_COMMON_H__ */
