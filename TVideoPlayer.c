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
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
// #define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TVideoPlayer"
#include <log.h>
//==============================================================================

#include "TVideoPlayer.h"

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_VIDEO_PLAYER

#define TG_MAIN_ELEMENTS TG_FILE_SRC(gst), player->decode_bin
#define TG_AUDIO_ELEMENTS player->audio_queue, TG_PLAYBACK_AUDIO_SINK_ELEMENTS
#define TG_VIDEO_ELEMENTS player->video_queue, TG_PLAYBACK_VIDEO_SINK_ELEMENTS
#define TG_ALL_ELEMENTS TG_MAIN_ELEMENTS, TG_AUDIO_ELEMENTS, TG_VIDEO_ELEMENTS

//==============================================================================

static void play_cycle_track_selection(TGst *gst,
                                       TPlayTrackType track_type,
                                       gboolean forward) {
#if 0
    TGstPlayback* player = TGST_PLAYER(gst);

    const gchar *prop_cur, *prop_n, *prop_get, *name;
    gint cur = -1, n = -1;
    guint flag, cur_flags;

    /* playbin3 variables */
    GList *selected_streams = NULL;

    g_mutex_lock (&play->selection_lock);

    switch (track_type) {
        case GST_PLAY_TRACK_TYPE_AUDIO:
            prop_get = "get-audio-tags";
            prop_cur = "current-audio";
            prop_n = "n-audio";
            name = "audio";
            flag = 0x2;
            break;
        case GST_PLAY_TRACK_TYPE_VIDEO:
            prop_get = "get-video-tags";
            prop_cur = "current-video";
            prop_n = "n-video";
            name = "video";
            flag = 0x1;
            break;
        case GST_PLAY_TRACK_TYPE_SUBTITLE:
            prop_get = "get-text-tags";
            prop_cur = "current-text";
            prop_n = "n-text";
            name = "subtitle";
            flag = 0x4;
            break;
        default:
            return;
    }

    g_object_get (gst->mainline, prop_cur, &cur, prop_n, &n, "flags",
        &cur_flags, NULL);

    if (forward) {
        if (!(cur_flags & flag))
        cur = 0;
        else
        cur = (cur + 1) % (n + 1);

    } else {
        if (cur <= 0)
        cur = n;
        else
        cur = (cur - 1) % (n + 1);
    }

    if (n < 1) {
        LOG_D("No %s tracks.", name);
        g_mutex_unlock (&play->selection_lock);
    } else {
        gchar *lcode = NULL, *lname = NULL;
        const gchar *lang = NULL;
        GstTagList *tags = NULL;

        if (cur >= n && track_type != GST_PLAY_TRACK_TYPE_VIDEO) {
            cur = -1;
            LOG_D("Disabling %s.", name);
            if (cur_flags & flag) {
                cur_flags &= ~flag;
                g_object_set (gst->mainline, "flags", cur_flags, NULL);
            }
        } else {
            /* For video we only want to switch between streams, not disable it altogether */
            if (cur >= n)
                cur = 0;

            if (!(cur_flags & flag) && track_type != GST_PLAY_TRACK_TYPE_VIDEO) {
                cur_flags |= flag;
                g_object_set (gst->mainline, "flags", cur_flags, NULL);
            }
            g_signal_emit_by_name (gst->mainline, prop_get, cur, &tags);

            if (tags != NULL) {
                if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &lcode))
                lang = gst_tag_get_language_name (lcode);
                else if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_NAME, &lname))
                lang = lname;
                gst_tag_list_unref (tags);
            }
            if (lang != NULL)
                LOG_D("Switching to %s track %d of %d (%s).", name, cur + 1, n,
                    lang);
            else
                LOG_D("Switching to %s track %d of %d.", name, cur + 1, n);
        }
        g_free (lcode);
        g_free (lname);
        g_mutex_unlock (&play->selection_lock);

        g_object_set (gst->mainline, prop_cur, cur, NULL);
    }

    if (selected_streams)
        g_list_free (selected_streams);
#endif
}

/**
 * decodebin "pad-added" 回调
 */
static void
pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data) {
    TVideoPlayer *player = TVIDEO_PLAYER(user_data);
    GstCaps *caps = NULL;
    GstStructure *structure = NULL;
    const gchar *media_type =
        NULL; // 注意：gst_structure_get_name 返回的是 const gchar*，不要释放

    TG_MUTEX_LOCK(player);

    // 1. 获取当前 Pad 的 Caps（如果还没协商出格式，尝试获取模板 Caps）
    caps = gst_pad_get_current_caps(new_pad);
    if (!caps) {
        caps = gst_pad_query_caps(new_pad, NULL);
    }
    if (!caps || gst_caps_is_empty(caps)) {
        LOG_E("获取 pad caps 失败，可能流格式无法识别");
        goto cleanup;
    }

    structure = gst_caps_get_structure(caps, 0);
    media_type = gst_structure_get_name(structure);

    // 2. 根据媒体类型分发到对应的 queue
    if (g_str_has_prefix(media_type, "video/")) {
        if (player->linked_video) {
            LOG_W("视频流已连接，忽略多余的视频 Pad: %s", media_type);

        } else {
            player->linked_video =
                tgst_static_pad_link(new_pad, player->video_queue, "sink");
        }

    } else if (g_str_has_prefix(media_type, "audio/")) {
        if (player->linked_audio) {
            LOG_W("音频流已连接，忽略多余的音频 Pad: %s", media_type);
        } else {
            player->linked_audio =
                tgst_static_pad_link(new_pad, player->audio_queue, "sink");
        }

    } else {
        LOG_W("忽略未知媒体类型: %s", media_type);
    }

cleanup:
    if (caps) {
        gst_caps_unref(caps);
    }

    TG_MUTEX_UNLOCK(player);
}

//------------------------------------------------------------------------------

static void player_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("对象 'Gst:%s'：创建开始 (gst=%p, funcs=%s) ......",
          TOBJ_NAME,
          gst,
          text_buf);

    TG_VTABLE_FUNC(GstPlayback, on_create, (gst, funcs));

    TVideoPlayer *player = TVIDEO_PLAYER(gst);

    player->linked_audio = FALSE;
    player->linked_video = FALSE;

    /* 初始化播放互斥锁 */
    g_mutex_init(&player->selection_lock);

    // 初始化音频管线
    tgst_element_factory_make(&player->decode_bin,
                              GST_PLUGINS_DECODE_BIN,
                              "decode_bin");

    tgst_element_factory_make(&player->audio_queue,
                              GST_PLUGINS_QUEUE,
                              "audio_queue");

    tgst_element_factory_make(&player->video_queue,
                              GST_PLUGINS_QUEUE,
                              "video_queue");

    TG_PIPELINE_ADD_MANY(gst, TG_ALL_ELEMENTS, NULL);

    // 链接 file_src 和 decode_bin
    if (!gst_element_link_many(TG_MAIN_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("file_src 和 decode_bin 链接失败");
    }
    // 链接音频分支：音频队列 -> 音频格式转换 -> 音量控制 -> 音频输出
    if (!gst_element_link_many(TG_AUDIO_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("链接链接音频失败")
    }
    // 链接视频分支：视频队列 -> APP输出
    if (!gst_element_link_many(TG_VIDEO_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("链接视频分支失败")
    }

    // 连接解码器接口，pad_added_handler 为解码器接口回调
    if (g_signal_connect(player->decode_bin,
                         PAD_ADDED,
                         G_CALLBACK(pad_added_handler),
                         player) == 0) {
        THROW_RUNTIME_ERROR("'decode_bin' '" PAD_ADDED "' 信号连接失败")
    }

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void player_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("对象 'Gst:%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    TVideoPlayer *player = TVIDEO_PLAYER(gst);

    g_mutex_clear(&player->selection_lock);

    TG_VTABLE_FUNC(GstPlayback, on_destroy, (gst));

    LOG_D("对象 'Gst:%s'：销毁成功。", TOBJ_NAME);
}

static ret_t player_on_start(TGst *gst) {
    LOG_V("对象 'Gst:%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstPlayback, on_start, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：启动失败！", TOBJ_NAME);

    LOG_D("对象 'Gst:%s'：启动成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t player_on_stop(TGst *gst) {
    LOG_V("对象 'Gst:%s'：停止开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstPlayback, on_stop, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：停止失败！", TOBJ_NAME);

    TVideoPlayer *player = TVIDEO_PLAYER(gst);

    if (player->collection != NULL) {
        gst_object_unref(player->collection);
        g_free(player->cur_audio_sid);
        g_free(player->cur_video_sid);
        g_free(player->cur_text_sid);

        player->collection = NULL;
        player->cur_audio_sid = NULL;
        player->cur_video_sid = NULL;
        player->cur_text_sid = NULL;
    }

    LOG_D("对象 'Gst:%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

TG_DECL_VTABLE(VideoPlayer) = {
    .size = sizeof(TVideoPlayer),
    .type = TG_TYPE_VIDEO_PLAYER,
    .get_parent_vt = TG_GET_PARENT_VTABLE(GstPlayback),

    TG_CALLBACKS_REQUIRED(player),
};

//------------------------------------------------------------------------------

TGst *videoPlayerCreate(gdouble start_position,
                        gboolean accurate_seeks,
                        gboolean instant_rate_changes) {
    LOG_V("对象 'Gst:%s'：创建开始 (start_position=%lf, accurate_seeks=%s, "
          "instant_rate_changes=%s) ......",
          TOBJ_NAME,
          start_position,
          t_bool2str(accurate_seeks),
          t_bool2str(instant_rate_changes));

    TGst *gst = gstCreate(TG_REF_VTABLE(VideoPlayer), TG_FUNC_VIDEO_PLAYBACK);

    TVideoPlayer *player = TVIDEO_PLAYER(gst);

    player->collection = NULL;
    player->cur_audio_sid = NULL;
    player->cur_video_sid = NULL;
    player->cur_text_sid = NULL;

    LOG_D("对象 'Gst:%s'：创建成功：%p", TOBJ_NAME, gst);
    return gst;
}

//==============================================================================
