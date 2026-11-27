/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     音乐播放器
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_MUSIC_PLAYER_H
#define T_MUSIC_PLAYER_H
//------------------------------------------------------------------------------

#include "TGstPlayback.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

struct _TMusicPlayer {
    TGstPlayback play;

    GstElement *decode_bin;

    bool_t linked_audio;
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
TGst *musicPlayerCreate(gdouble start_position,
                        gboolean accurate_seeks,
                        gboolean instant_rate_changes);

//------------------------------------------------------------------------------

#define TG_TYPE_MUSIC_PLAYER "MusicPlayer"
#define TMUSIC_PLAYER(gst) ((TMusicPlayer *)TGST(gst))

TG_EXTERN_VTABLE(MusicPlayer);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_MUSIC_PLAYER_H
//==============================================================================
