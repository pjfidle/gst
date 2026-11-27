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
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TMusicPlayer"
#include <log.h>
//==============================================================================

#include "TMusicPlayer.h"

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_MUSIC_PLAYER

/*------------------------------------------------------------------------------
 * Windows 环境下宏定义
 */
#ifdef WINDOWS

#define TG_MAIN_ELEMENTS TG_FILE_SRC(gst), player->decode_bin

#define TG_AUDIO_ELEMENTS                                                      \
    PV_AUDIO_CONVERT(gst), WIN_AUDIO_RESAMPLE(gst), WIN_AUDIO_CAPS(gst),       \
        TG_AUDIO_SPECTRUM(gst), TG_AUDIO_SINK(gst)

#define TG_ALL_ELEMENTS TG_MAIN_ELEMENTS, TG_AUDIO_ELEMENTS

#endif // WINDOWS

/*------------------------------------------------------------------------------
 * Linux 环境下宏定义
 */
#ifdef LINUX

#define TG_MAIN_ELEMENTS TG_FILE_SRC(gst), player->decode_bin

#define TG_AUDIO_ELEMENTS                                                      \
    PV_AUDIO_CONVERT(gst), TG_AUDIO_SPECTRUM(gst), TG_AUDIO_SINK(gst)

#define TG_ALL_ELEMENTS TG_MAIN_ELEMENTS, TG_AUDIO_ELEMENTS

#endif // LINUX

//==============================================================================
/**
 * decodebin "pad-added" 回调
 */
static void
pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data) {
    TMusicPlayer *player = TMUSIC_PLAYER(user_data);
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

    if (g_str_has_prefix(media_type, "audio/")) {
        if (player->linked_audio) {
            LOG_W("音频流已连接，忽略多余的音频 Pad: %s", media_type);
        } else {
            player->linked_audio =
                tgst_static_pad_link(new_pad, PV_AUDIO_CONVERT(player), "sink");
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

    TMusicPlayer *player = TMUSIC_PLAYER(gst);

    player->linked_audio = FALSE;

    // 初始化音频管线
    tgst_element_factory_make(&player->decode_bin,
                              GST_PLUGINS_DECODE_BIN,
                              "decode_bin");

    TG_PIPELINE_ADD_MANY(gst, TG_ALL_ELEMENTS, NULL);

    // 链接 file_src 和 decode_bin
    if (!gst_element_link_many(TG_MAIN_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("file_src 和 decode_bin 链接失败");
    }
    // 链接音频分支：音频队列 -> 音频格式转换 -> 音量控制 -> 音频输出
    if (!gst_element_link_many(TG_AUDIO_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("链接链接音频失败")
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

    TG_VTABLE_FUNC(GstPlayback, on_destroy, (gst));

    LOG_D("对象 'Gst:%s'：销毁成功。", TOBJ_NAME);
}

static ret_t player_on_start(TGst *gst) {
    LOG_V("对象 'Gst:%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    //TMusicPlayer *player = TMUSIC_PLAYER(gst);

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

    LOG_D("对象 'Gst:%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

TG_DECL_VTABLE(MusicPlayer) = {
    .size = sizeof(TMusicPlayer),
    .type = TG_TYPE_MUSIC_PLAYER,
    .get_parent_vt = TG_GET_PARENT_VTABLE(GstPlayback),

    TG_CALLBACKS_REQUIRED(player),
};

//------------------------------------------------------------------------------

TGst *musicPlayerCreate(gdouble start_position,
                        gboolean accurate_seeks,
                        gboolean instant_rate_changes) {
    LOG_V("对象 'Gst:%s'：创建开始 (start_position=%lf, accurate_seeks=%s, "
          "instant_rate_changes=%s) ......",
          TOBJ_NAME,
          start_position,
          t_bool2str(accurate_seeks),
          t_bool2str(instant_rate_changes));
    TGst *gst = gstCreate(TG_REF_VTABLE(MusicPlayer), TG_FUNC_MUSIC_PLAYBACK);

    LOG_D("对象 'Gst:%s'：创建成功：%p", TOBJ_NAME, gst);
    return gst;
}

//==============================================================================
