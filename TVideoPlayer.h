/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     视频播放器
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_VIDEO_PLAYER_H
#define T_VIDEO_PLAYER_H
//------------------------------------------------------------------------------

#include "TGstPlayback.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

struct _TVideoPlayer {
    TGstPlayback play;

    GstElement *decode_bin;
    GstElement *audio_queue;
    GstElement *video_queue;

    bool_t linked_audio;
    bool_t linked_video;

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
TGst *videoPlayerCreate(gdouble start_position,
                        gboolean accurate_seeks,
                        gboolean instant_rate_changes);

//------------------------------------------------------------------------------

#define TG_TYPE_VIDEO_PLAYER "VideoPlayer"
#define TVIDEO_PLAYER(p) ((TVideoPlayer *)p)

TG_EXTERN_VTABLE(VideoPlayer);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_VIDEO_PLAYER_H
//==============================================================================
