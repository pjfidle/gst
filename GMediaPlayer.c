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

#include "GMediaPlayer.h"
#include "GMediaCommon.h"
#include <stdio.h>
#include <gst/gstinfo.h>
#include <gst/audio/audio.h>

#define DEBUG 1
#if DEBUG
#define MY_DBG(fmt, ...) printf("%s(%d) [调试] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define MY_DBG(fmt, ...)
#endif

#define MY_WAR(fmt, ...) printf("%s(%d) [警告] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define MY_ERR(fmt, ...) printf("%s(%d) [错误] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#define BUS_MSG_DBG 1
#if BUS_MSG_DBG
#define BUS_CALL_LOG(fmt, ...) printf("[GST_BUS_MSG] " fmt "\n", ##__VA_ARGS__)
#else
#define BUS_CALL_LOG(fmt, ...)
#endif

#define AUDIOSINK_AUTO 1

#define SAFE_CALLBACK(name, ...) \
    { \
        if (ctx->name) { \
            ctx->name(ctx->userdata, ##__VA_ARGS__); \
        } \
    }

#define GET_GST_ENUM_NAME(enum_name) \
    const char* get##enum_name##Name(enum_name code) { \
        int i = 0; \
        while (i < (sizeof(s##enum_name##NameMap)/sizeof(GEnumName))) { \
            GEnumName it = s##enum_name##NameMap[i]; \
            if (it.id == code) \
                return it.name; \
            i++; \
        } \
        return enum_name##Unknown; \
    }

#define IS_PLAYBIN_MSG(msg) \
    GST_MESSAGE_SRC(msg) == GST_OBJECT(player->playbin)

#define IS_TARGET_STATE(state) \
    (player->target_state == state)

#define SET_DESIRED_STATE(state) \
    player->target_state = state


static gboolean player_do_seek (GMediaPlayerPtr player, gint64 pos, gdouble rate,
    GstPlayTrickMode mode);

//------------------------------------------------------------------------------

typedef struct {
    const int id;
    const char *name;
} GEnumName;

static GEnumName sGstStreamStatusTypeNameMap[] = {
    // 需要创建一个新线程。
    { GST_STREAM_STATUS_TYPE_CREATE,  "GST_STREAM_STATUS_TYPE_CREATE" },
    // 线程进入其循环函数
    { GST_STREAM_STATUS_TYPE_ENTER,   "GST_STREAM_STATUS_TYPE_ENTER" },
    // 线程离开其循环函数
    { GST_STREAM_STATUS_TYPE_LEAVE,   "GST_STREAM_STATUS_TYPE_LEAVE" },
    // 线程被销毁
    { GST_STREAM_STATUS_TYPE_DESTROY, "GST_STREAM_STATUS_TYPE_DESTROY" },
    // 线程启动
    { GST_STREAM_STATUS_TYPE_START,   "GST_STREAM_STATUS_TYPE_START" },
    // 线程暂停
    { GST_STREAM_STATUS_TYPE_PAUSE,   "GST_STREAM_STATUS_TYPE_PAUSE" },
    // 线程停止
    { GST_STREAM_STATUS_TYPE_STOP,    "GST_STREAM_STATUS_TYPE_STOP" },
};
static const char *GstStreamStatusTypeUnknown = "GST_STREAM_STATUS_TYPE_UNKNOWN";
GET_GST_ENUM_NAME(GstStreamStatusType)

static GstStateChangeReturn player_set_state(GElementPtr element, GstState state)
{
    g_assert(element != NULL);

    const gchar *name = gst_object_get_name(GST_OBJECT(element));
    const char* state_name = gst_element_state_get_name(state);

    GstStateChangeReturn ret = gst_element_set_state(element, state);
    switch (ret) {
        case GST_STATE_CHANGE_SUCCESS:
            MY_DBG("对象 %s 状态已成功设置为 %s", name, state_name);
            break;
        case GST_STATE_CHANGE_FAILURE:
            MY_DBG("无法设置对象 %s 状态为 %s", name, state_name);
            break;
        case GST_STATE_CHANGE_NO_PREROLL:
            MY_DBG("对象 %s 状态已设置为 %s (无预渲染)", name, state_name);
            break;
        case GST_STATE_CHANGE_ASYNC:
            MY_DBG("对象 %s 状态正在异步转换到 %s", name, state_name);
            break;
    }
    return ret;
}

static GstFlowReturn new_video_sample(GElementPtr sink, gpointer user_data)
{
    GMediaContextPtr ctx = (GMediaContextPtr)user_data;

    if (!ctx) {
        return GST_FLOW_ERROR;
    }

    // 获取视频样本（包含帧数据+时间戳PTS）
    GstSample *sample = NULL;
    g_signal_emit_by_name(sink, PULL_SAMPLE, &sample);
    if (!sample) {
        MY_ERR("[RK3506_ERROR] 提取视频样本失败或样本已被提前释放\n");
        return GST_FLOW_ERROR;
    }

    SAFE_CALLBACK(video_sample, sample);

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static void player_reset(GMediaPlayerPtr player)
{
    g_list_foreach (player->missing, (GFunc) gst_message_unref, NULL);
    player->missing = NULL;

    player->buffering = FALSE;
    player->is_live = FALSE;
}


static gboolean queryPlaybinPosition(GMediaPlayerPtr player, gint64 *position)
{
    gint64 value;

    if (gst_element_query_position(player->playbin, GST_FORMAT_TIME, &value)) {
        if (*position != value) {
            *position = value;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean queryPlaybinDuration(GMediaPlayerPtr player, gint64 *duration)
{
    gint64 value;

    if (gst_element_query_duration(player->playbin, GST_FORMAT_TIME, &value)) {
        if (*duration != value) {
            *duration = value;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean player_timeout(gpointer user_data)
{
    GMediaContextPtr ctx = (GMediaContextPtr)user_data;
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    gint64 position = player->position;
    if (IS_TARGET_STATE(GST_STATE_PLAYING) && queryPlaybinPosition(player, &position) == TRUE) {
        //LOG_V("position=%ld", position);
        player->position = position;
        SAFE_CALLBACK(position_changed, position);
    }
    gint64 duration = player->duration;
    if (queryPlaybinDuration(player, &duration) == TRUE) {
        //LOG_V("duration=%ld", duration);
        player->duration = duration;
        SAFE_CALLBACK(duration_changed, duration);
    }

    return G_SOURCE_CONTINUE;
}

#if BUS_MSG_DBG
static const char* gstElementName(GElementPtr element)
{
    static char buffer[64];
    auto name = gst_element_get_name(element);
    strcpy(buffer, name);
    g_free(name);
    return buffer;
}
#endif

static gboolean player_bus_msg(GstBus* bus, GstMessage* msg, gpointer userdata)
{
    GMediaContextPtr ctx = (GMediaContextPtr)userdata;
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    switch (GST_MESSAGE_TYPE(msg)) {
        /**
         * 通知异步状态变更操作已完成
         * 
         * 核心作用：
         *      当元素（element）或管道（pipeline）执行异步状态切换（如从 
         *      GST_STATE_READY 切换到 GST_STATE_PAUSED，或从 GST_STATE_PAUSED 
         *      切换到 GST_STATE_PLAYING）时，操作不会立即完成，而是会异步处理。
         *      当整个异步操作完成后，就会发送 GST_MESSAGE_ASYNC_DONE 消息，
         *      告知应用程序状态切换已彻底完成。
         * 常见场景：
         *      管道启动（从 PAUSED 到 PLAYING）时，需要等待所有元素准备就绪（如缓冲数据、
         *      初始化硬件等），完成后会发送此消息。
         *      动态修改管道结构后，状态重新同步完成时。
         * 处理方式：
         *      应用程序通常通过消息总线（GstBus）监听此消息，以确认状态切换已完成，
         *      进而执行后续操作（如开始处理媒体数据、更新 UI 状态等）。
         */
        case GST_MESSAGE_ASYNC_DONE: {
            /* dump graph on preroll */
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (player->playbin),
                GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.async-done");

            BUS_CALL_LOG("Prerolled.");
            if (player->start_position > 0.0) {
                player_do_seek (player, player->start_position * GST_SECOND,
                    player->rate, player->trick_mode);
                player->start_position = 0;
            }
            player->seeking = FALSE;
            break;
        }
        /**
         * 管道正在缓冲。
         * 当应用程序在非直播管道的PLAYING状态下收到缓冲消息时，必须将管道暂停（PAUSE），
         * 直到缓冲完成（即消息中的百分比字段达到100%）。
         * 对于直播管道，无需执行任何操作，缓冲百分比可用于向用户显示进度。
         */
        case GST_MESSAGE_BUFFERING: {
            gint percent;
            gst_message_parse_buffering (msg, &percent);
            if (percent == 100) {
                /* 100%消息表示缓冲已完成 */
                if (player->buffering) {
                    player->buffering = FALSE;

                    /**
                     * 直播类的实时流管道，不需要像本地文件播放管道那样，手动管控管道的状态切换、
                     * 暂停 / 停止逻辑，GStreamer 会自动处理直播管道的状态生命周期，无需开发者额
                     * 外编写状态管理代码。
                     */
                    if (!player->is_live)
                        player_set_state(player->playbin, player->target_state);
                }
            } else {
                /* 缓冲中... */
                if (!player->buffering) {
                    if (!player->is_live)
                        player_set_state(player->playbin, GST_STATE_PAUSED);
                    player->buffering = TRUE;
                }
            }
            break;
        }
        /**
         * 管道当前选择的时钟变得不可用。
         * 管道将在下一次切换到PLAYING状态时选择新时钟。
         * 应用程序收到此消息时应将管道设为PAUSED，再恢复为PLAYING。
         */
        case GST_MESSAGE_CLOCK_LOST: {
            BUS_CALL_LOG("时钟丢失，正在选择新时钟");
            player_set_state(player->playbin, GST_STATE_PAUSED);
            player_set_state(player->playbin, GST_STATE_PLAYING);
            break;
        }
        /**
         * 通知管道（pipeline）延迟（latency）信息已更新或需要重新计算的消息类型，主要与音视频同步和实时处理相关。
         * 
         * 核心作用：
         *      在 GStreamer 管道中，延迟是指数据从进入管道到离开管道的总时间（如视频从读取到显示、
         *      音频从解码到播放的耗时）。
         *      当管道结构发生变化（如添加 / 移除元素）、元素延迟属性修改，或系统环境变化导致延迟改变时，
         *      会发送 GST_MESSAGE_LATENCY 消息，提示应用程序需要重新计算或调整管道的延迟设置，以确保音视频同步和实时性。
         * 触发场景：
         *      管道中添加了具有固有延迟的元素（如网络传输元素、缓冲元素）。
         *      元素的延迟属性（如 latency）被动态修改。
         *      管道状态变更（如从 PAUSED 切换到 PLAYING）时，元素初始化完成后报告自身延迟。
         *      硬件设备（如声卡、显卡）的延迟特性发生变化。
         * 处理方式：
         *      收到此消息后，应用程序通常需要触发管道重新计算总延迟，并将计算结果应用到相关元素
         *      （尤其是实时元素，如音频输出），以保证同步。典型处理流程是调用 gst_bin_recalculate_latency() 
         *      重新计算延迟，并让管道应用新的延迟设置。
         */
        case GST_MESSAGE_LATENCY: {
            BUS_CALL_LOG("重新分发延迟...");
            gst_bin_recalculate_latency(GST_BIN(player->playbin));
            break;
        }
        /**
         * 当元素希望管道改变状态时发布。
         * 此消息是对应用程序的建议，应用程序可以决定是否对（部分）管道执行状态更改。
         */
        case GST_MESSAGE_REQUEST_STATE: {
            GstState state;
            gchar *name;

            name = gst_object_get_path_string (GST_MESSAGE_SRC (msg));

            gst_message_parse_request_state (msg, &state);

            BUS_CALL_LOG("正在按 %s 的请求将状态设置为 %s...",
                gst_element_state_get_name (state), name);

            player_set_state (player->playbin, state);
            g_free (name);
            break;
        }
        /**
         * 管道已到达流结束（EOS）。
         * 应用程序仅在PLAYING状态下会收到此消息，并且每次将处于EOS状态的管道设置为
         * PLAYING时也会收到。
         * 应用程序可以执行刷新定位（flushing seek）来取消EOS状态。
         */
        case GST_MESSAGE_EOS: {
            BUS_CALL_LOG("Got EOS from element \"%s\".", GST_MESSAGE_SRC_NAME (msg));
            /*  final position at end */
            player_timeout (ctx);
            if (IS_PLAYBIN_MSG(msg)) {
                SAFE_CALLBACK(eos);
            }
            break;
        }
        /**
         * 发生警告。
         */
        case GST_MESSAGE_WARNING: {
            GError *err;
            gchar *dbg = NULL;

            /* 发生警告时转储管线图 */
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (player->playbin),
                GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.error");

            gst_message_parse_warning(msg, &err, &dbg);
            BUS_CALL_LOG("WARNING: %s : %s", err->message, dbg ? dbg : "none");

            SAFE_CALLBACK(warning, err, dbg);

            g_error_free(err);
            g_free(dbg);
            break;
        }
        /**
         * 发生错误。
         * 当应用程序收到错误消息时，应停止管道播放，且不应假定会有更多数据播放。
         * 可通过在错误消息中设置redirect-location字段来指定重定向URL，
         * 应用程序或高层容器可根据需要使用此信息。
         */
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *dbg = NULL;

            /* 发生错误时转储管线图 */
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (player->playbin),
                GST_DEBUG_GRAPH_SHOW_ALL, "gst-play.error");

            gst_message_parse_error(msg, &err, &dbg);
            BUS_CALL_LOG("ERROR: %s : %s", err->message, dbg ? dbg : "none");

            /* 清空总线上的所有其他错误消息并执行资源清理 */
            player_set_state(player->playbin, GST_STATE_NULL);

            SAFE_CALLBACK(error, err, dbg);

            g_error_free(err);
            g_free(dbg);
            break;
        }
        /**
         * 发现标签（如元数据）。
         */
        case GST_MESSAGE_TAG: {
            GstTagList *tags = NULL;
            // 从消息中提取标签列表（GstTagList）
            gst_message_parse_tag(msg, &tags);
            if (tags) {
                SAFE_CALLBACK(tags, tags);
                gst_tag_list_unref(tags);
            }
            break;
        }
        case GST_MESSAGE_PROPERTY_NOTIFY: {
            const GValue *val;
            const gchar *name;
            GstObject *obj;
            gchar *val_str = NULL;
            gchar *obj_name;

            gst_message_parse_property_notify (msg, &obj, &name, &val);

            obj_name = gst_object_get_path_string (GST_OBJECT (obj));
            if (val != NULL) {
                if (G_VALUE_HOLDS_STRING (val))
                val_str = g_value_dup_string (val);
                else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
                val_str = gst_caps_to_string (g_value_get_boxed (val));
                else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
                val_str = gst_tag_list_to_string (g_value_get_boxed (val));
                else
                val_str = gst_value_serialize (val);
            } else {
                val_str = g_strdup ("(no value)");
            }

            MY_DBG ("%s: %s = %s\n", obj_name, name, val_str);
            g_free (obj_name);
            g_free (val_str);
            break;
        }
        /**
         * 表示有新的#GstStreamCollection可用的消息（自1.10版本起）。
         */
        case GST_MESSAGE_STREAM_COLLECTION: {
            GstStreamCollection *collection = NULL;
            gst_message_parse_stream_collection (msg, &collection);

            if (collection) {
                g_mutex_lock (&player->selection_lock);
                if (player->collection)
                gst_object_unref (player->collection);
                player->collection = collection;
                g_mutex_unlock (&player->selection_lock);
            }
            break;
        }
        /**
         * 表示活动的#GstStreams选择已更改的消息（自1.10版本起）。
         */
        case GST_MESSAGE_STREAMS_SELECTED: {
            GstStreamCollection *collection = NULL;
            guint i, len;

            gst_message_parse_streams_selected (msg, &collection);
            if (collection) {
                g_mutex_lock (&player->selection_lock);
                gst_object_replace ((GstObject **) & player->collection,
                    (GstObject *) collection);

                /* Free all last stream-ids */
                g_free (player->cur_audio_sid);
                g_free (player->cur_video_sid);
                g_free (player->cur_text_sid);
                player->cur_audio_sid = NULL;
                player->cur_video_sid = NULL;
                player->cur_text_sid = NULL;

                len = gst_message_streams_selected_get_size (msg);
                for (i = 0; i < len; i++) {
                GstStream *stream = gst_message_streams_selected_get_stream (msg, i);
                if (stream) {
                    GstStreamType type = gst_stream_get_stream_type (stream);
                    const gchar *stream_id = gst_stream_get_stream_id (stream);

                    if (type & GST_STREAM_TYPE_AUDIO) {
                        player->cur_audio_sid = g_strdup (stream_id);
                    } else if (type & GST_STREAM_TYPE_VIDEO) {
                        player->cur_video_sid = g_strdup (stream_id);
                    } else if (type & GST_STREAM_TYPE_TEXT) {
                        player->cur_text_sid = g_strdup (stream_id);
                    } else {
                        gst_print ("Unknown stream type with stream-id %s\n", stream_id);
                    }
                    gst_object_unref (stream);
                }
                }

                gst_object_unref (collection);
                g_mutex_unlock (&player->selection_lock);
            }
            break;
        }
        /**
         * 发生了状态变化
         * 
         * @GST_STATE_VOID_PENDING: 无待处理状态。
         * @GST_STATE_NULL        : 元素的 NULL 状态或初始状态。
         * @GST_STATE_READY       : 元素已准备好进入 PAUSED 状态。
         * @GST_STATE_PAUSED      : 元素处于 PAUSED 状态，已准备好接收和处理数据。
         *                          但 sink 元素仅接受一个缓冲区，之后会阻塞。
         * @GST_STATE_PLAYING     : 元素处于 PLAYING 状态，#GstClock 正在运行，
         *                          数据正在流转。
         * 元素可能处于的状态。可以使用 gst_element_set_state () 更改状态，
         * 使用 gst_element_get_state () 检查状态。
        */
        case GST_MESSAGE_STATE_CHANGED: {
            BUS_CALL_LOG("对象 %s 状态改变", GST_MESSAGE_SRC_NAME (msg));
            /* 我们只关心管道状态更改消息 */
            if (IS_PLAYBIN_MSG(msg)) {
                GstState old_state, new_state, pending_state;

                gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                BUS_CALL_LOG("'%s' State Changed: %s -> %s", 
                        GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), 
                        gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

                if ((pending_state == GST_STATE_VOID_PENDING) && 
                        (player->current_state != new_state)) {
                    player->current_state = new_state;
                }
                SAFE_CALLBACK(state_changed, old_state, new_state, pending_state);
            }
            break;
        }
        /**
         * 关于流的状态信息（如开始、停止、出错等时触发）。
         */
        case GST_MESSAGE_STREAM_STATUS: {
            GstStreamStatusType type;
            GstElement *owner;
            gst_message_parse_stream_status(msg, &type, &owner);
            BUS_CALL_LOG("Stream Status: %s from %s", getGstStreamStatusTypeName(type), gst_object_get_name(GST_OBJECT(owner)));
            SAFE_CALLBACK(stream_status, type, owner);
            break;
        }
        /**
         * 由应用程序发布的消息（可能通过应用特定元素发布）。
         */
        case GST_MESSAGE_APPLICATION: {
            break;
        }
        /**
         * 管道的时长发生了变化。
         * 应用程序可以通过时长查询获取新的时长。
         */
        case GST_MESSAGE_DURATION_CHANGED: {
            /*
            if (isPlayer() && isPipelineMsg(msg)) {
                gint64 duration = mDuration.load();
                if (queryPipelineDuration(duration)) {
                    mDuration.store(duration);
                    SAFE_CALLBACK(duration_changed, duration);
                }
            }
            */
            break;
        }
        default: {
            if (gst_is_missing_plugin_message (msg)) {
                gchar *desc;

                desc = gst_missing_plugin_message_get_description (msg);
                MY_WAR ("缺失插件：%s\n", desc);
                g_free (desc);
                player->missing = g_list_append (player->missing, gst_message_ref (msg));
            }
            break;
        }
    }

    return TRUE;
}

//------------------------------------------------------------------------------

static gboolean
player_set_rate_and_trick_mode (GMediaPlayerPtr player, gdouble rate,
    GstPlayTrickMode mode)
{
  gint64 pos = -1;

  g_return_val_if_fail (rate != 0, FALSE);

  if (!gst_element_query_position (player->playbin, GST_FORMAT_TIME, &pos))
    return FALSE;

  return player_do_seek (player, pos, rate, mode);
}

static gboolean 
player_do_seek (GMediaPlayerPtr player, gint64 pos, gdouble rate, GstPlayTrickMode mode)
{
    GstSeekFlags seek_flags;
    GstQuery *query;
    GstEvent *seek;
    gboolean seekable = FALSE;

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    if (!gst_element_query (player->playbin, query)) {
        gst_query_unref (query);
        return FALSE;
    }

    gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
    gst_query_unref (query);

    if (!seekable)
        return FALSE;

    seek_flags = 0;

    switch (mode) {
        case GST_PLAY_TRICK_MODE_DEFAULT:
            seek_flags |= GST_SEEK_FLAG_TRICKMODE;
            break;
        case GST_PLAY_TRICK_MODE_DEFAULT_NO_AUDIO:
            seek_flags |= GST_SEEK_FLAG_TRICKMODE | GST_SEEK_FLAG_TRICKMODE_NO_AUDIO;
            break;
        case GST_PLAY_TRICK_MODE_KEY_UNITS:
            seek_flags |= GST_SEEK_FLAG_TRICKMODE_KEY_UNITS;
            break;
        case GST_PLAY_TRICK_MODE_KEY_UNITS_NO_AUDIO:
            seek_flags |=
                GST_SEEK_FLAG_TRICKMODE_KEY_UNITS | GST_SEEK_FLAG_TRICKMODE_NO_AUDIO;
            break;
        case GST_PLAY_TRICK_MODE_NONE:
        default:
            break;
    }

    /* 检查是否可以执行即时速率切换（不改变播放方向） */
    if (mode & GST_PLAY_TRICK_MODE_INSTANT_RATE && rate * player->rate > 0) {
        seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
            seek_flags | GST_SEEK_FLAG_INSTANT_RATE_CHANGE,
            GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE,
            GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        if (gst_element_send_event (player->playbin, seek)) {
            goto done;
        }
    }

    /* 不支持即时速率切换，需要执行带刷新的查找操作 */
    seek_flags |= GST_SEEK_FLAG_FLUSH;

    /* 如果未启用精确定位，则查找定位到关键帧 */
    seek_flags |=
        player->accurate_seeks ? GST_SEEK_FLAG_ACCURATE : GST_SEEK_FLAG_KEY_UNIT;

    if (rate >= 0)
        seek = gst_event_new_seek (rate, GST_FORMAT_TIME, seek_flags,
            /* start */ GST_SEEK_TYPE_SET, pos,
            /* stop */ GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
    else
        seek = gst_event_new_seek (rate, GST_FORMAT_TIME, seek_flags,
            /* start */ GST_SEEK_TYPE_SET, 0,
            /* stop */ GST_SEEK_TYPE_SET, pos);

    if (!gst_element_send_event (player->playbin, seek))
        return FALSE;

done:
    player->rate = rate;
    player->trick_mode = mode & ~GST_PLAY_TRICK_MODE_INSTANT_RATE;
    return TRUE;
}

static const gchar *
trick_mode_get_description (GstPlayTrickMode mode)
{
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

static void
player_switch_trick_mode (GMediaPlayerPtr player)
{
    GstPlayTrickMode new_mode = ++player->trick_mode;
    const gchar *mode_desc;

    if (new_mode == GST_PLAY_TRICK_MODE_LAST)
        new_mode = GST_PLAY_TRICK_MODE_NONE;

    mode_desc = trick_mode_get_description (new_mode);

    if (player_set_rate_and_trick_mode (player, player->rate, new_mode)) {
        MY_DBG ("速率: %.2f (%s)", player->rate, mode_desc);
    } else {
        MY_DBG ("无法将特技播放模式切换为 % s。", mode_desc);
    }
}

static void
player_cycle_track_selection (GMediaContextPtr ctx, GstPlayTrackType track_type,
    gboolean forward)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    const gchar *prop_cur, *prop_n, *prop_get, *name;
    gint cur = -1, n = -1;
    guint flag, cur_flags;

    /* playbin3 variables */
    GList *selected_streams = NULL;
    gint cur_audio_idx = -1, cur_video_idx = -1, cur_text_idx = -1;
    gint nb_audio = 0, nb_video = 0, nb_text = 0;
    guint len, i;

    g_mutex_lock (&player->selection_lock);

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

    g_object_get (player->playbin, prop_cur, &cur, prop_n, &n, "flags",
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
        gst_print ("No %s tracks.\n", name);
        g_mutex_unlock (&player->selection_lock);
    } else {
        gchar *lcode = NULL, *lname = NULL;
        const gchar *lang = NULL;
        GstTagList *tags = NULL;

        if (cur >= n && track_type != GST_PLAY_TRACK_TYPE_VIDEO) {
        cur = -1;
        gst_print ("Disabling %s.           \n", name);
        if (cur_flags & flag) {
            cur_flags &= ~flag;
            g_object_set (player->playbin, "flags", cur_flags, NULL);
        }
        } else {
        /* For video we only want to switch between streams, not disable it altogether */
        if (cur >= n)
            cur = 0;

        if (!(cur_flags & flag) && track_type != GST_PLAY_TRACK_TYPE_VIDEO) {
            cur_flags |= flag;
            g_object_set (player->playbin, "flags", cur_flags, NULL);
        }
        g_signal_emit_by_name (player->playbin, prop_get, cur, &tags);

        if (tags != NULL) {
            if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &lcode))
            lang = gst_tag_get_language_name (lcode);
            else if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_NAME, &lname))
            lang = lname;
            gst_tag_list_unref (tags);
        }
        if (lang != NULL)
            gst_print ("Switching to %s track %d of %d (%s).\n", name, cur + 1, n,
                lang);
        else
            gst_print ("Switching to %s track %d of %d.\n", name, cur + 1, n);
        }
        g_free (lcode);
        g_free (lname);
        g_mutex_unlock (&player->selection_lock);

        g_object_set (player->playbin, prop_cur, cur, NULL);
    }

    if (selected_streams)
        g_list_free (selected_streams);
}

static void
player_set_relative_volume (GMediaPlayerPtr player, gint volume)
{
    if (volume < 0)     volume = 0;
    if (volume > 100)   volume = 100;

    gst_stream_volume_set_volume (GST_STREAM_VOLUME (player->playbin),
        GST_STREAM_VOLUME_FORMAT_CUBIC, ((gdouble)volume)/100);
}

//------------------------------------------------------------------------------

void gst_time_to_string(gint64 time, gchar *buffer)
{
    if (time == GST_CLOCK_TIME_NONE) {
        sprintf(buffer, "--:--:--");
        return;
    }

    // 计算时分秒
    guint64 total_seconds = time / GST_SECOND;
    guint hours = total_seconds / 3600;
    guint minutes = (total_seconds % 3600) / 60;
    guint seconds = (total_seconds % 60);

    if (hours > 99) {
        hours = 99;
        minutes = 59;
        seconds = 59;
    }

    sprintf(buffer, "%2u:%2u:%2u", hours, minutes, seconds);
}

//------------------------------------------------------------------------------

GResult gst_media_init(GMediaContextPtr ctx, 
                       GstPlayFlags flags, 
                       gint initial_volume,
                       gdouble start_position,
                       gboolean accurate_seeks,
                       gboolean instant_rate_changes)
{
    GResult ret = RESULT_SUCCESS;
    GElementPtr sink;

    if (!gst_is_initialized()) {
        GError* err = NULL;
        if (!gst_init_check(NULL, NULL, &err)) {
            MY_ERR("gst_init failed: %s", err ? err->message : "unknown error");
            ret = ERROR_GST_INITIAL;
            goto fail_exit;
        }
    }

    MY_DBG("BEGIN");

    /* 分配私有变量 */
    GMediaPlayerPtr player = calloc(1, sizeof(GMediaPlayer));
    if (!player) {
        MY_ERR("ERROR_MEM_ALLOC");
        ret = ERROR_MEM_ALLOC;
        goto fail_exit;
    }


    /* 创建播放管线 */
    player->playbin = gst_element_factory_make (GST_PLAY_BIN, "playbin");
    if (!player->playbin) {
        MY_ERR("ERROR_PLAYBIN_NEW");
        ret = ERROR_PLAYBIN_NEW;
        goto fail_playbin_make;
    }

    /* 初始化播放互斥锁 */
    g_mutex_init (&player->selection_lock);

#if 0
    sink = gst_element_factory_make (GST_ALSA_SINK, "audio_sink");
    g_object_set(G_OBJECT(sink), 
            "device", "hw:0,0", 
            "sync", TRUE, 
            "async", TRUE, 
            "provide-clock", FALSE, 
            NULL);
    if (sink != NULL)
        g_object_set (player->playbin, "audio-sink", sink, NULL);
    else
        g_warning ("Couldn't create specified audio sink");
#endif

    if (ctx->use_kmssink) {
        MY_DBG("videosink=kmssink plane-id=%d", ctx->plane_id);
        sink = gst_element_factory_make (GST_KMS_SINK, "video_sink");
        g_object_set(G_OBJECT(sink), 
                "plane-id", ctx->plane_id, 
                NULL);

    } else {
        MY_DBG("videosink=appsink");
        sink = gst_element_factory_make (GST_APP_SINK, "new_sample");
        player->new_sample = g_signal_connect(sink, NEW_SAMPLE, G_CALLBACK(new_video_sample), ctx);
        g_object_set(G_OBJECT(sink), 
                "sync", TRUE, 
                "async", TRUE, 
                "emit-signals", TRUE, 
                "qos", TRUE, 
                NULL);
    }

    if (sink != NULL)
        g_object_set (player->playbin, "video-sink", sink, NULL);
    else
        g_warning ("Couldn't create specified video sink");

    g_object_set(G_OBJECT(player->playbin), 
            "flags", (guint)flags,
            NULL); 

    /* 设置消息总线监听 */
    player->bus_watch = gst_bus_add_watch (GST_ELEMENT_BUS (player->playbin), 
            player_bus_msg, ctx);

    player->timeout = g_timeout_add(500, player_timeout, ctx);

    player->missing = NULL;
    player->buffering = FALSE;
    player->is_live = FALSE;

    player->current_state = GST_STATE_NULL;
    player->target_state = GST_STATE_PLAYING;

    player->initial_file = TRUE;

    player_set_relative_volume (player, initial_volume);

    player->rate = 1.0;
    player->trick_mode = GST_PLAY_TRICK_MODE_NONE;
    player->start_position = start_position;
    player->accurate_seeks = accurate_seeks;
    player->instant_rate_changes = instant_rate_changes;

    ctx->player = player;

    MY_DBG("SUCCESS");

    return RESULT_SUCCESS;

fail_bus_add_watch:
    gst_object_unref(player->playbin);
fail_playbin_make:
    g_mutex_clear (&player->selection_lock);
    free(player);
fail_exit:
    ctx->player = NULL;
    return ret;
}

void gst_media_deinit(GMediaContextPtr ctx)
{
    if (!ctx || !ctx->player)
        return;

    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    player_reset(player);

    /* 停止播放管道 */
    player_set_state(player->playbin, GST_STATE_NULL);
    gst_object_unref(player->playbin);

    /* 移除总线监视器 */
    g_source_remove(player->bus_watch);
    if (player->timeout != 0)
        g_source_remove (player->timeout);

    if (player->collection)
        gst_object_unref (player->collection);

    g_free (player->cur_audio_sid);
    g_free (player->cur_video_sid);
    g_free (player->cur_text_sid);

    g_mutex_clear (&player->selection_lock);
    free(player);
    ctx->player = NULL;
}

void gst_media_play(GMediaContextPtr ctx, const gchar *filename)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    if (player->initial_file)
        player_set_state(player->playbin, GST_STATE_READY);

    gchar *uri = gst_filename_to_uri(filename, NULL);
    g_object_set (player->playbin, "uri", uri, NULL);
    g_free(uri);

    if (player->initial_file) {
        switch (player_set_state(player->playbin, GST_STATE_PAUSED)) {
        case GST_STATE_CHANGE_FAILURE:
            MY_DBG("无需处理，消息总线会上报一条错误信息。");
            break;

        case GST_STATE_CHANGE_NO_PREROLL:
            MY_DBG("管线是实时流。");
            player->is_live = TRUE;
            break;

        case GST_STATE_CHANGE_ASYNC:
            MY_DBG("预滚中...");
            break;
        default:
            break;
        }
        if (!IS_TARGET_STATE(GST_STATE_PAUSED))
            player_set_state(player->playbin, player->target_state);
    }

    player->initial_file = FALSE;
}

void gst_media_stop(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    player_reset(player);

    /* 停止播放管道 */
    player_set_state(player->playbin, GST_STATE_NULL);
}

GstState gst_media_get_state(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    return player->current_state;
}

gboolean gst_media_is_playing(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    return IS_TARGET_STATE(GST_STATE_PLAYING);
}

void gst_media_toggle_paused(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;

    if (IS_TARGET_STATE(GST_STATE_PLAYING))
        player->target_state = GST_STATE_PAUSED;
    else
        player->target_state = GST_STATE_PLAYING;

    if (!player->buffering) {
        gst_element_set_state (player->playbin, player->target_state);

    } else if (IS_TARGET_STATE(GST_STATE_PLAYING)) {
        MY_DBG ("缓冲完成后将立即播放");
    }
}

gboolean gst_media_seek(GMediaContextPtr ctx, gint64 position)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    GstQuery *query;
    GstEvent *seek;
    gboolean seekable = FALSE;

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    if (!gst_element_query (player->playbin, query)) {
        gst_query_unref (query);
        return FALSE;
    }

    gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
    gst_query_unref (query);

    if (!seekable)
        return FALSE;

    if (player->seeking == FALSE) {
        player->seeking = TRUE;

        int flags = GST_SEEK_FLAG_NONE
                  | GST_SEEK_FLAG_FLUSH
                  //| GST_SEEK_FLAG_ACCURATE;
                  | GST_SEEK_FLAG_KEY_UNIT;

        seek = gst_event_new_seek (1.0, GST_FORMAT_TIME,
            (GstSeekFlags)flags,    // seek 标志
            GST_SEEK_TYPE_SET,      // 跳转类型（绝对位置）
            position,               // 目标位置
            GST_SEEK_TYPE_NONE,     // 结束位置（不使用）
            GST_CLOCK_TIME_NONE     // 无结束时间
        );

        if (gst_element_send_event (player->playbin, seek)) {
            MY_DBG("Seek successful to %" GST_TIME_FORMAT "", GST_TIME_ARGS(position));
        } else {
            g_warning("Seek failed to %" GST_TIME_FORMAT, GST_TIME_ARGS(position));
        }
    }
    return TRUE;
}

gint gst_media_get_volume(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    gdouble volume = gst_stream_volume_get_volume (GST_STREAM_VOLUME (player->playbin),
                            GST_STREAM_VOLUME_FORMAT_CUBIC);
    return volume * 100;
}

void gst_media_set_volume(GMediaContextPtr ctx, gint volume)
{
    player_set_relative_volume((GMediaPlayerPtr)ctx->player, volume);
}

gdouble gst_media_get_rate(GMediaContextPtr ctx)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    return player->rate;
}

void gst_media_set_playback_rate (GMediaContextPtr ctx, gdouble rate)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    GstPlayTrickMode mode = player->trick_mode;

    if (player->instant_rate_changes)
        mode |= GST_PLAY_TRICK_MODE_INSTANT_RATE;

    if (player_set_rate_and_trick_mode (player, rate, mode)) {
        MY_DBG ("播放速率: %.2f", rate);
    } else {
        MY_DBG ("无法将播放速率更改为 %.2f", rate);
    }
}

void gst_media_set_relative_playback_rate (GMediaContextPtr ctx, gdouble rate_step,
    gboolean reverse_direction)
{
    GMediaPlayerPtr player = (GMediaPlayerPtr)ctx->player;
    gdouble new_rate = player->rate + rate_step;

    if (reverse_direction)
        new_rate *= -1.0;

    gst_media_set_playback_rate (player, new_rate);
}

//------------------------------------------------------------------------------
