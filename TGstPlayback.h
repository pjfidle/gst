/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 播放
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_PLAYER_H
#define T_GST_PLAYER_H
//------------------------------------------------------------------------------

#include "TGst.h"

/*------------------------------------------------------------------------------
 * Windows 环境下宏定义
 */
#ifdef WINDOWS

#define TG_PLAYBACK_AUDIO_SINK_ELEMENTS                                        \
    PV_AUDIO_CONVERT(gst), WIN_AUDIO_RESAMPLE(gst), WIN_AUDIO_CAPS(gst),       \
        TG_AUDIO_SINK(gst)

#define TG_PLAYBACK_VIDEO_SINK_ELEMENTS                                        \
    WIN_VIDEO_CONVERT_SCALE(gst), WIN_VIDEO_CAPS(gst), TG_VIDEO_SINK(gst)

#endif /*WINDOWS*/

/*------------------------------------------------------------------------------
 * Linux 环境下宏定义
 */
#ifdef LINUX

#define TG_PLAYBACK_AUDIO_SINK_ELEMENTS                                        \
    PV_AUDIO_CONVERT(gst), TG_AUDIO_SINK(gst)

#define TG_PLAYBACK_VIDEO_SINK_ELEMENTS TG_VIDEO_SINK(gst)

#endif

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

typedef enum {
    GST_PLAY_TRICK_MODE_NONE = 0,
    GST_PLAY_TRICK_MODE_DEFAULT,
    GST_PLAY_TRICK_MODE_DEFAULT_NO_AUDIO,
    GST_PLAY_TRICK_MODE_KEY_UNITS,
    GST_PLAY_TRICK_MODE_KEY_UNITS_NO_AUDIO,
    GST_PLAY_TRICK_MODE_LAST,

    /**
     * 即时速率（instant-rate）设置是一个标志位，
     * 它叠加在 trick-mode（特殊播放模式）的枚举值之上。
     * 它的值必须是大于所有枚举值的 2 的幂次方（2^n），
     * 这样设置该标志时，才不会影响到原有的 trickmode 值
     */
    GST_PLAY_TRICK_MODE_INSTANT_RATE = (1 << 3)
} TPlayTrickMode;

typedef enum {
    GST_PLAY_TRACK_TYPE_INVALID = 0,
    GST_PLAY_TRACK_TYPE_AUDIO,
    GST_PLAY_TRACK_TYPE_VIDEO,
    GST_PLAY_TRACK_TYPE_SUBTITLE
} TPlayTrackType;

struct _TGstPlayback {
    TGst gst;

    TPlayTrickMode trick_mode;
    gdouble rate;
    gdouble start_position;
    gboolean accurate_seeks;
    gboolean instant_rate_changes;
    gboolean seeking;

    GstElement *file_src;
};

//------------------------------------------------------------------------------

#define TG_FILE_SRC(p) (TGST_PLAYBACK(p)->file_src)

#define TG_FILE_SRC_SET(gst, ...)                                              \
    g_object_set(G_OBJECT(TGST_PLAYBACK(gst)->file_src), ##__VA_ARGS__)

//------------------------------------------------------------------------------

#define TG_TYPE_GST_PLAYBACK "GstPlayback"
#define TGST_PLAYBACK(p) ((TGstPlayback *)(p))

TG_EXTERN_VTABLE(GstPlayback);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_PLAYER_H
//==============================================================================
