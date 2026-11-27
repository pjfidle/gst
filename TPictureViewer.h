/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     图片查看器
 */

/**
 * History:
 * ================================================================
 * 2026-09-15 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_PICTURES_VIEWER_H
#define T_PICTURES_VIEWER_H
//------------------------------------------------------------------------------

#include "TGstPlayback.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

#ifdef WINDOWS
/**
 * Windows 环境下宏定义
 */

#define TG_PHOTO_VIEWER_SINK_PROPS                                             \
    "emit-signals", TRUE, "sync", TRUE, "max-buffers", 2, "drop", TRUE, NULL

#endif

#ifdef LINUX
/**
 * Linux 环境下宏定义
 */

#define TG_PHOTO_VIEWER_SINK_PROPS                                             \
    "emit-signals", TRUE, "sync", TRUE, "max-buffers", 2, "drop", TRUE, NULL

#endif

#define TG_PHOTO_ELEMENTS                                                      \
    play->audio_convert, play->audio_resample, play->audio_caps,               \
        TG_AUDIO_LEVEL(gst), TG_AUDIO_SINK(gst), NULL

//------------------------------------------------------------------------------

struct _TPictureViewer {
    TGstPlayback play;

    gboolean is_initialized;

#if 0
    GstElement* audio_bin;
    GstElement* audio_caps;
    GstElement* audio_convert;
    GstElement* audio_resample;

    /* 媒体区块 */
    GstStreamCollection *collection;
    /* 音频流 */
    gchar *cur_audio_sid;
    /* 视频流 */
    gchar *cur_video_sid;
    /* 字幕流 */
    gchar *cur_text_sid;
    /* 媒体区块操作锁 */
    tk_mutex_t* selection_lock;

    TPlayTrickMode  trick_mode;
    gdouble         rate;
    gdouble         start_position;
    gboolean        accurate_seeks;
    gboolean        instant_rate_changes;
    gboolean        seeking;
#endif
};

//------------------------------------------------------------------------------

/**
 * 创建 GST 播放对象。
 *
 * @param funcs                 播放支持的功能
 * @param start_position        播放起始位置，等于 0 从头播放，单位：秒
 * @param accurate_seeks        是否启用精确定位，等于 TRUE 时精确到帧。
 * @param instant_rate_changes  即时速率切换 / 无缝倍速切换
 *
 * @return TGstPlayback 对象实例。
 */
TGst *pictureViewerCreate(void);

//------------------------------------------------------------------------------

#define TG_TYPE_PICTURE_VIEWER "PictureViewer"
#define TPICTURE_VIEWER(p) ((TPictureViewer *)p)

TG_EXTERN_VTABLE(PictureViewer);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_PICTURES_VIEWER_H
//==============================================================================
