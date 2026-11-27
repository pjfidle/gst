/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 音视频和图片播放器
 */

/**
 * History:
 * ================================================================
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TGstPlayback"
#include <log.h>
//==============================================================================

#include "TGstPlayback.h"
#include <gst/gstinfo.h>
#include <gst/pbutils/pbutils.h>

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_GST_PLAYBACK

//==============================================================================

static gboolean
play_do_seek(TGst *gst, gint64 pos, gdouble rate, TPlayTrickMode mode) {
    TGstPlayback *play = TGST_PLAYBACK(gst);

    GstSeekFlags seek_flags;
    GstQuery *query;
    GstEvent *seek;
    gboolean seekable = FALSE;

    query = gst_query_new_seeking(GST_FORMAT_TIME);
    if (!gst_element_query(gst->pipeline, query)) {
        gst_query_unref(query);
        return FALSE;
    }

    gst_query_parse_seeking(query, NULL, &seekable, NULL, NULL);
    gst_query_unref(query);

    if (!seekable)
        return FALSE;

    seek_flags = 0;

    switch (mode) {
    case GST_PLAY_TRICK_MODE_DEFAULT:
        seek_flags |= GST_SEEK_FLAG_TRICKMODE;
        break;
    case GST_PLAY_TRICK_MODE_DEFAULT_NO_AUDIO:
        seek_flags |=
            GST_SEEK_FLAG_TRICKMODE | GST_SEEK_FLAG_TRICKMODE_NO_AUDIO;
        break;
    case GST_PLAY_TRICK_MODE_KEY_UNITS:
        seek_flags |= GST_SEEK_FLAG_TRICKMODE_KEY_UNITS;
        break;
    case GST_PLAY_TRICK_MODE_KEY_UNITS_NO_AUDIO:
        seek_flags |= GST_SEEK_FLAG_TRICKMODE_KEY_UNITS |
                      GST_SEEK_FLAG_TRICKMODE_NO_AUDIO;
        break;
    case GST_PLAY_TRICK_MODE_NONE:
    default:
        break;
    }

    /* 检查是否可以执行即时速率切换（不改变播放方向） */
    if (mode & GST_PLAY_TRICK_MODE_INSTANT_RATE && rate * play->rate > 0) {
        seek =
            gst_event_new_seek(rate,
                               GST_FORMAT_TIME,
                               seek_flags | GST_SEEK_FLAG_INSTANT_RATE_CHANGE,
                               GST_SEEK_TYPE_NONE,
                               GST_CLOCK_TIME_NONE,
                               GST_SEEK_TYPE_NONE,
                               GST_CLOCK_TIME_NONE);
        if (gst_element_send_event(gst->pipeline, seek)) {
            goto done;
        }
    }

    /* 不支持即时速率切换，需要执行带刷新的查找操作 */
    seek_flags |= GST_SEEK_FLAG_FLUSH;

    /* 如果未启用精确定位，则查找定位到关键帧 */
    seek_flags |=
        play->accurate_seeks ? GST_SEEK_FLAG_ACCURATE : GST_SEEK_FLAG_KEY_UNIT;

    if (rate >= 0)
        seek = gst_event_new_seek(rate,
                                  GST_FORMAT_TIME,
                                  seek_flags,
                                  /* start */ GST_SEEK_TYPE_SET,
                                  pos,
                                  /* stop */ GST_SEEK_TYPE_SET,
                                  GST_CLOCK_TIME_NONE);
    else
        seek = gst_event_new_seek(rate,
                                  GST_FORMAT_TIME,
                                  seek_flags,
                                  /* start */ GST_SEEK_TYPE_SET,
                                  0,
                                  /* stop */ GST_SEEK_TYPE_SET,
                                  pos);

    if (!gst_element_send_event(gst->pipeline, seek))
        return FALSE;

done:
    play->rate = rate;
    play->trick_mode = mode & ~GST_PLAY_TRICK_MODE_INSTANT_RATE;
    return TRUE;
}

static gboolean
play_set_rate_and_trick_mode(TGst *gst, gdouble rate, TPlayTrickMode mode) {
    gint64 pos = -1;

    g_return_val_if_fail(rate != 0, FALSE);

    if (!gst_element_query_position(gst->pipeline, GST_FORMAT_TIME, &pos))
        return FALSE;

    return play_do_seek(gst, pos, rate, mode);
}

static const gchar *trick_mode_get_description(TPlayTrickMode mode) {
    switch (mode) {
    case GST_PLAY_TRICK_MODE_NONE:
        return "正常播放，禁用特技模式";
    case GST_PLAY_TRICK_MODE_DEFAULT:
        return "特技模式：默认";
    case GST_PLAY_TRICK_MODE_DEFAULT_NO_AUDIO:
        return "特技模式：默认（无音频）";
    case GST_PLAY_TRICK_MODE_KEY_UNITS:
        return "特技模式：仅关键帧";
    case GST_PLAY_TRICK_MODE_KEY_UNITS_NO_AUDIO:
        return "特技模式：仅关键帧（无音频）";
    default:
        break;
    }
    return "未知特技模式";
}

static void play_switch_trick_mode(TGst *gst) {
    TGstPlayback *play = TGST_PLAYBACK(gst);

    TPlayTrickMode new_mode = ++play->trick_mode;

    if (new_mode == GST_PLAY_TRICK_MODE_LAST)
        new_mode = GST_PLAY_TRICK_MODE_NONE;

    if (play_set_rate_and_trick_mode(gst, play->rate, new_mode)) {
        LOG_D("速率: %.2f (%s)",
              play->rate,
              trick_mode_get_description(new_mode));
    } else {
        LOG_W("无法将特技播放模式切换为 %s。",
              trick_mode_get_description(new_mode));
    }
}

//------------------------------------------------------------------------------

static void playback_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("对象 'Gst:%s'：创建开始 (gst=%p, funcs=%s) ......",
          TOBJ_NAME,
          gst,
          text_buf);

    TGstPlayback *play = TGST_PLAYBACK(gst);

    play->rate = 1.0;
    play->trick_mode = GST_PLAY_TRICK_MODE_NONE;
    play->start_position = 0;
    play->accurate_seeks = FALSE;
    play->instant_rate_changes = FALSE;
    play->seeking = FALSE;

    tgst_element_factory_make(&play->file_src,
                              GST_PLUGINS_FILE_SRC,
                              "file_src");

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void playback_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("'%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    LOG_D("'%s'：销毁成功。", TOBJ_NAME);
}

static ret_t playback_on_start(TGst *gst) {
    LOG_V("'%s'：启动开始 (gst=%p, filename=%s) ......",
          TOBJ_NAME,
          gst,
          gst->filename);

    TG_FILE_SRC_SET(gst, "location", gst->filename, NULL);

    LOG_D("'%s'：启动成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t playback_on_stop(TGst *gst) {
    LOG_V("'%s'：停止开始 (%p) ......", TOBJ_NAME, gst);

    TGstPlayback *play = TGST_PLAYBACK(gst);

    play->seeking = FALSE;

    LOG_D("'%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t playback_on_async_done(TGst *gst) {
    LOG_V("'%s'：异步开始 (%p) ......", TOBJ_NAME, gst);

    TGstPlayback *play = TGST_PLAYBACK(gst);

    if (play->start_position > 0.0) {
        play_do_seek(gst,
                     play->start_position * GST_SECOND,
                     play->rate,
                     play->trick_mode);
        play->start_position = 0;
    }
    play->seeking = FALSE;

    LOG_D("'%s'：异步成功。", TOBJ_NAME);
    return RET_OK;
}

ret_t playback_on_state_changed(TGst *gst,
                                GstState old_state,
                                GstState new_state,
                                GstState pending_state) {
    LOG_V("'%s'：状态改变开始 (%p, old_state=%s, new_state=%s, "
          "pending_state=%s) ......",
          TOBJ_NAME,
          gst,
          gst_element_state_get_name(old_state),
          gst_element_state_get_name(new_state),
          gst_element_state_get_name(pending_state));

    LOG_D("'%s'：状态改变成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t playback_on_paused(TGst *gst, gboolean paused) {
    gst->target_state = paused ? GST_STATE_PAUSED : GST_STATE_PLAYING;

    if (!gst->buffering) {
        tgst_set_state(gst->pipeline, gst->target_state);

    } else if (gst->target_state == GST_STATE_PLAYING) {
        LOG_V("缓冲完成后将立即播放");
    }
    return RET_OK;
}

TG_DECL_VTABLE(GstPlayback) = {
    .size = sizeof(TGstPlayback),
    .type = TG_TYPE_GST_PLAYBACK,
    .get_parent_vt = TG_GET_PARENT_VTABLE(Gst),

    TG_CALLBACKS_REQUIRED(playback),
    .on_paused = playback_on_paused,

    .on_async_done = playback_on_async_done,
    .on_state_changed = playback_on_state_changed,
};

//==============================================================================
