/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     gstreamer-1.0 core
 */

/**
 * History:
 * ================================================================
 * 2026-09-01 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TGst"
#include <log.h>
//==============================================================================

#include <inttypes.h>

#include "TGst.h"
#include "TGstVTable.h"
#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/gstvideofilter.h>

#ifdef LINUX
#include <linux/dma-buf.h>
#include <rga/rga.h>
#endif

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_NONE

/*------------------------------------------------------------------------------
 * Windows 环境下宏定义
 */
#ifdef WINDOWS

#define TG_AUDIO_SRC_PLUGINS GST_PLUGINS_WASAPI_SRC
#define TG_AUDIO_SRC_DEVICE ""

#define TG_AUDIO_SINK_PLUGINS GST_PLUGINS_WASAPI_SINK
#define TG_AUDIO_SINK_DEVICE ""

#define TG_VIDEO_SRC_PLUGINS GST_PLUGINS_KSVIDEO_SRC
#define TG_VIDEO_SRC_DEVICE ""

#define TG_AUDIO_SRC_PROPS "do-timestamp", TRUE

/** 音频输入格式过滤器 */
#define TG_AUDIO_CAPS_PROPS                                                    \
    "audio/x-raw", "format", G_TYPE_STRING, "F32LE", "rate", G_TYPE_INT,       \
        48000, "channels", G_TYPE_INT, 2

#define TG_AUDIO_SINK_PROPS "sync", TRUE, "async", TRUE

#define TG_VIDEO_SRC_PROPS "device-index", 0, "do-timestamp", TRUE

#define TG_VIDEO_CAPS_PROPS "video/x-raw", "format", G_TYPE_STRING, "RGBA"

#define TG_VIDEO_SINK_PROPS                                                    \
    "emit-signals", TRUE, "sync", TRUE, "max-buffers", 2, "drop", TRUE

#endif /*WINDOWS*/

/*------------------------------------------------------------------------------
 * Linux 环境下宏定义
 */
#ifdef LINUX

#define TG_AUDIO_SRC_PLUGINS GST_PLUGINS_ALSA_SRC
#define TG_AUDIO_SRC_DEVICE "sysdefault:CARD=MS210x"

#define TG_AUDIO_SINK_PLUGINS GST_PLUGINS_ALSA_SINK
#define TG_AUDIO_SINK_DEVICE "sysdefault:CARD=rockchiprk312xc"

#define TG_VIDEO_SRC_PLUGINS GST_PLUGINS_V4L2_SRC
#define TG_VIDEO_SRC_DEVICE "/dev/video0"

#define TG_AUDIO_SRC_PROPS "device", TG_AUDIO_SRC_DEVICE, "provide-clock", FALSE

#define TG_AUDIO_SINK_PROPS                                                    \
    "device", TG_AUDIO_SINK_DEVICE, "sync", TRUE, "async", TRUE

#define TG_VIDEO_SRC_PROPS                                                     \
    "device", "/dev/video0", "io-mode", 4, "do-timestamp", TRUE

#define TG_VIDEO_SINK_PROPS                                                    \
    "emit-signals", TRUE, "sync", TRUE, "async", TRUE, "max-buffers", 2,       \
        "qos", TRUE

#endif /*LINUX*/

//==============================================================================

static const key_name_t s_tgst_event_type_list[] = {
#define X(type, name, value_def) KEY_NAME_MAKE(TG_EVENT_##type, #name),
    TG_EVENT_LIST
#undef X
};
FUNC_KEY_NAME_GET_NAME(tgst_event_type)

//------------------------------------------------------------------------------

/**
 * 管线警告
 *
 * @param gst       TGst 对象。
 * @param domain    错误域。
 * @param code      错误类型。
 */
static void gstNotifyWarning(TGst *gst, GQuark domain, gint code) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_warning_t, TG_EVENT_WARNING);

        /* 填充附加的自定义参数 */
        evt.domain = domain;
        evt.code = code;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

/**
 * 管线错误
 *
 * @param gst       TGst 对象。
 * @param domain    错误域。
 * @param code      错误类型。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
static void gstNotifyError(TGst *gst, GQuark domain, gint code) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_error_t, TG_EVENT_ERROR);

        /* 填充附加的自定义参数 */
        evt.domain = domain;
        evt.code = code;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

/**
 * 发现标签（如元数据）。
 *
 * @param gst       TGst 对象。
 * @param tags      从消息中提取的标签列表。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
static void gstNotifyTags(TGst *gst, GstTagList *tags) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_tags_t, TG_EVENT_TAGS);

        // 提取艺术家名称（GST_TAG_ARTIST 是标准标签键）
        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &evt.artist)) {
            LOG_I("解析到艺术家: %s", evt.artist);
        }

        // 提取唱片集（专辑）名称（GST_TAG_ALBUM 是标准标签键）
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &evt.album)) {
            LOG_I("解析到唱片集: %s", evt.album);
        }

        // 可选：提取歌曲标题（GST_TAG_TITLE）
        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &evt.title)) {
            LOG_I("解析到歌曲标题: %s", evt.title);
        }

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

/**
 * 音频流电平回调，单声道时上报的左右电平将相等
 *
 * @param gst       TGst 对象。
 * @param left      左声道电平，0-100
 * @param right     右声道电平，0-100
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
static void gstNotifyAudioLevel(TGst *gst, gfloat left, int32_t right) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_audio_level_t, TG_EVENT_AUDIO_LEVEL);

        /* 填充附加的自定义参数 */
        evt.left = left;
        evt.right = right;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

/**
 * 音频流频谱上报，
 *
 * @param gst       TGst 对象。
 * @param bars      10 段频谱电平，归一化为 0.0 ~ 1.0。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
static void gstNotifyAudioSpectrum(TGst *gst, float *bars) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_audio_spectrum_t, TG_EVENT_AUDIO_SPECTRUM);

        /* 填充附加的自定义参数 */
        evt.bars = bars;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

/**
 * 视频帧数据回调
 *
 * @param gst       TGst 对象。
 * @param frame     视频帧数据结构体指针。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
static void gstNotifyVideoFrame(TGst *gst, TGstVideoFrame *frame) {
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_video_frame_t, TG_EVENT_VIDEO_FRAME);

        /* 填充附加的自定义参数 */
        evt.frame = frame;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

//------------------------------------------------------------------------------

static gboolean queryPlaybinPosition(TGst *gst, gint64 *position) {
    gint64 value;

    if (gst_element_query_position(gst->pipeline, GST_FORMAT_TIME, &value)) {
        if (*position != value) {
            *position = value;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean queryPlaybinDuration(TGst *gst, gint64 *duration) {
    gint64 value;

    if (gst_element_query_duration(gst->pipeline, GST_FORMAT_TIME, &value)) {
        if (*duration != value) {
            *duration = value;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean gstQueryTimer(gpointer user_data) {
    TGst *gst = TGST(user_data);

    if (gst->is_playback) {
        gint64 position = gst->position;
        if (gst->target_state == GST_STATE_PLAYING &&
            queryPlaybinPosition(gst, &position) == TRUE) {
            gstNotifyPositionChanged(gst, position);
        }
        gint64 duration = gst->duration;
        if (queryPlaybinDuration(gst, &duration) == TRUE) {
            gstNotifyDurationChanged(gst, duration);
        }
    }
    return G_SOURCE_CONTINUE;
}

static ret_t gstAudioLevel(TGst *gst, const GstStructure *structure) {
    const GValue *val_rms = NULL;
    const GValue *val_peak = NULL;

    // 1. 从消息中提取 RMS（平均能量）和 Peak（峰值）的 GValue 数组
    val_rms = gst_structure_get_value(structure, "rms");
    val_peak = gst_structure_get_value(structure, "peak");

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS

    // 2. 直接调用 g_value_array_get_type() 避开 G_TYPE_VALUE_ARRAY 宏的预处理警告
    GType value_array_type = g_value_array_get_type();
    if (val_rms && val_peak && G_VALUE_HOLDS(val_rms, value_array_type) &&
        G_VALUE_HOLDS(val_peak, value_array_type)) {

        gfloat left_channel_level = 0.0f;
        gfloat right_channel_level = 0.0f;

        GValueArray *rms_array = (GValueArray *)g_value_get_boxed(val_rms);
        GValueArray *peak_array = (GValueArray *)g_value_get_boxed(val_peak);

        if (rms_array && peak_array) {
            guint channels = MIN(rms_array->n_values, peak_array->n_values);

            // 3. 遍历各个声道获取分贝值（只处理前两个声道：左/右）
            for (guint i = 0; i < channels && i < 2; i++) {
                const GValue *rms_chan = &rms_array->values[i];
                const GValue *peak_chan = &peak_array->values[i];

                if (!G_VALUE_HOLDS_DOUBLE(rms_chan) ||
                    !G_VALUE_HOLDS_DOUBLE(peak_chan)) {
                    continue;
                }

                gdouble peak_db = g_value_get_double(peak_chan);

                // 4. 将分贝值归一化为 0.0 - 1.0 浮点数，并限制上下界防止溢出
                const gdouble min_db = -60.0;
                gfloat peak_norm = 0.0f;
                if (peak_db > min_db) {
                    peak_norm = (gfloat)((peak_db - min_db) / (-min_db));
                    peak_norm = CLAMP(peak_norm, 0.0f, 1.0f);
                }

                // 5. 保存左右声道计算结果
                if (i == 0) {
                    left_channel_level = peak_norm;
                } else if (i == 1) {
                    right_channel_level = peak_norm;
                }
            }

            // 6. 单声道时右声道复用左声道数据，回调通知 UI 更新
            gstNotifyAudioLevel(gst,
                                left_channel_level,
                                channels == 1 ? left_channel_level
                                              : right_channel_level);
        }
    } else {
        const gchar *rms_type_name =
            val_rms ? g_type_name(G_VALUE_TYPE(val_rms)) : "NULL";
        const gchar *peak_type_name =
            val_peak ? g_type_name(G_VALUE_TYPE(val_peak)) : "NULL";

        static guint err_counter = 0;
        if (++err_counter % 20 == 0) {
            LOG_W("异态过滤: rms=%s, peak=%s\n", rms_type_name, peak_type_name);
        }
        G_GNUC_END_IGNORE_DEPRECATIONS
        return RET_FAIL;
    }

    G_GNUC_END_IGNORE_DEPRECATIONS
    return RET_OK;
}

static ret_t gstAudioSpectrum(TGst *gst, const GstStructure *structure) {
    // clang-format off
    // 基于 bands = 256 (48kHz采样率，每格约 93.75Hz) 的 31 段准对数索引区间
    // 跳过 idx=0~1 (<180Hz 超低频底噪)，平滑覆盖 187Hz ~ 16kHz 全听觉频段
    static const int bin_ranges_31[GST_SPECTRUM_BANDS + 1] = {
        2,   3,   4,   5,   6,   7,   8,   9,
        11,  13,  15,  17,  20,  23,  26,  30,
        34,  39,  44,  50,  56,  63,  70,  78,
        87,  97,  108, 120, 133, 147, 162, 178
    };

    // 31 段频率倾斜补偿 (dB)：压低低频轰鸣感，逐步抬升中高频，使音乐波形饱满均衡
    static const float band_gain_31[GST_SPECTRUM_BANDS] = {
        -12.0f, -11.0f, -10.0f, -9.0f,  -8.0f,  -7.0f,  -6.0f,  -5.0f,
        -4.0f,  -3.0f,  -2.0f,  -1.0f,   0.0f,  +1.0f,  +2.0f,  +3.0f,
        +4.0f,  +5.0f,  +6.0f,  +7.0f,  +8.0f,  +9.0f,  +9.5f, +10.0f,
        +10.5f, +11.0f, +11.5f, +12.0f, +12.5f, +13.0f, +13.5f
    };
    // clang-format on

    float out_bars[GST_SPECTRUM_BANDS];
    const GValue *magnitudes = gst_structure_get_value(structure, "magnitude");
    const float min_db = -60.0f;
    guint total_bins = gst_value_list_get_size(magnitudes);

    for (int b = 0; b < GST_SPECTRUM_BANDS; b++) {
        float max_db = min_db;
        int start_idx = bin_ranges_31[b];
        int end_idx = bin_ranges_31[b + 1];

        for (int idx = start_idx; idx < end_idx && idx < (int)total_bins;
             idx++) {
            const GValue *val = gst_value_list_get_value(magnitudes, idx);
            float db = g_value_get_float(val);
            if (db > max_db) {
                max_db = db;
            }
        }

        // 静音底噪门限过滤
        if (max_db <= min_db + 2.0f) {
            out_bars[b] = 0.0f;
            continue;
        }

        // 应用倾斜补偿并归一化到 0.0 ~ 1.0（留 4dB 余量让波形更饱满）
        float compensated_db = max_db + band_gain_31[b];
        float norm = (compensated_db - min_db) / (-4.0f - min_db);

        if (norm < 0.0f)
            norm = 0.0f;
        if (norm > 1.0f)
            norm = 1.0f;
        out_bars[b] = norm;
    }
    gstNotifyAudioSpectrum(gst, out_bars);
    return RET_OK;
}

// GStreamer 管线消息
static gboolean gstBusMsg(GstBus *bus, GstMessage *msg, gpointer userdata) {
    TGst *gst = TGST(userdata);

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
     *      管道启动（从 PAUSED 到
     * PLAYING）时，需要等待所有元素准备就绪（如缓冲数据、
     *      初始化硬件等），完成后会发送此消息。
     *      动态修改管道结构后，状态重新同步完成时。
     * 处理方式：
     *      应用程序通常通过消息总线（GstBus）监听此消息，以确认状态切换已完成，
     *      进而执行后续操作（如开始处理媒体数据、更新 UI 状态等）。
     */
    case GST_MESSAGE_ASYNC_DONE: {
        LOG_V("%s async-done", gst_object_get_name(GST_OBJECT(gst->pipeline)));
        tgst_vtable_on_async_done(gst);
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
        gst_message_parse_buffering(msg, &percent);
        if (percent == 100) {
            /* 100%消息表示缓冲已完成 */
            if (gst->buffering) {
                gst->buffering = FALSE;

                /**
                 * 直播类的实时流管道，不需要像本地文件播放管道那样，手动管控管道的状态切换、
                 * 暂停 / 停止逻辑，GStreamer
                 * 会自动处理直播管道的状态生命周期，无需开发者额
                 * 外编写状态管理代码。
                 */
                if (!gst->is_live)
                    tgst_set_state(gst->pipeline, gst->target_state);
            }
        } else {
            /* 缓冲中... */
            if (!gst->buffering) {
                if (!gst->is_live)
                    tgst_set_state(gst->pipeline, GST_STATE_PAUSED);
                gst->buffering = TRUE;
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
        LOG_V("时钟丢失，正在选择新时钟");
        tgst_set_state(gst->pipeline, GST_STATE_PAUSED);
        tgst_set_state(gst->pipeline, GST_STATE_PLAYING);
        break;
    }
    /**
     * 通知管道（pipeline）延迟（latency）信息已更新或需要重新计算的消息类型，主要与音视频同步和实时处理相关。
     *
     * 核心作用：
     *      在 GStreamer
     * 管道中，延迟是指数据从进入管道到离开管道的总时间（如视频从读取到显示、
     *      音频从解码到播放的耗时）。
     *      当管道结构发生变化（如添加 /
     * 移除元素）、元素延迟属性修改，或系统环境变化导致延迟改变时， 会发送
     * GST_MESSAGE_LATENCY
     * 消息，提示应用程序需要重新计算或调整管道的延迟设置，以确保音视频同步和实时性。
     * 触发场景：
     *      管道中添加了具有固有延迟的元素（如网络传输元素、缓冲元素）。
     *      元素的延迟属性（如 latency）被动态修改。
     *      管道状态变更（如从 PAUSED 切换到
     * PLAYING）时，元素初始化完成后报告自身延迟。
     *      硬件设备（如声卡、显卡）的延迟特性发生变化。
     * 处理方式：
     *      收到此消息后，应用程序通常需要触发管道重新计算总延迟，并将计算结果应用到相关元素
     *      （尤其是实时元素，如音频输出），以保证同步。典型处理流程是调用
     * gst_bin_recalculate_latency() 重新计算延迟，并让管道应用新的延迟设置。
     */
    case GST_MESSAGE_LATENCY: {
        LOG_V("重新分发延迟...");
        gst_bin_recalculate_latency(GST_BIN(gst->pipeline));
        break;
    }
    /**
     * 当元素希望管道改变状态时发布。
     * 此消息是对应用程序的建议，应用程序可以决定是否对（部分）管道执行状态更改。
     */
    case GST_MESSAGE_REQUEST_STATE: {
        GstState state;
        gchar *name = gst_object_get_path_string(GST_MESSAGE_SRC(msg));
        gst_message_parse_request_state(msg, &state);

        LOG_V("正在按 %s 的请求将状态设置为 %s...",
              gst_element_state_get_name(state),
              name);

        tgst_set_state(gst->pipeline, state);
        g_free(name);
        break;
    }
    /**
     * 管道已到达流结束（EOS）。
     * 应用程序仅在PLAYING状态下会收到此消息，并且每次将处于EOS状态的管道设置为
     * PLAYING时也会收到。
     * 应用程序可以执行刷新定位（flushing seek）来取消EOS状态。
     */
    case GST_MESSAGE_EOS: {
        LOG_V("Got EOS from element \"%s\".", GST_MESSAGE_SRC_NAME(msg));
        /*  final position at end */
        gstQueryTimer(gst);
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(gst->pipeline)) {
            gstNotifyCompleted(gst);
        }
        break;
    }
    /**
     * 发生警告。
     */
    case GST_MESSAGE_WARNING: {
        GError *err;
        gchar *dbg = NULL;

        gst_message_parse_warning(msg, &err, &dbg);
        LOG_V("WARNING: %s : %s", err->message, dbg ? dbg : "none");

        g_mutex_lock(&gst->mutex);
        gstNotifyWarning(gst, err->domain, err->code);
        g_mutex_unlock(&gst->mutex);

        tgst_vtable_on_warning(gst, err, dbg);

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

        gst_message_parse_error(msg, &err, &dbg);
        LOG_V("ERROR: %s : %s", err->message, dbg ? dbg : "none");
        gstNotifyError(gst, err->domain, err->code);
        tgst_vtable_on_error(gst, err, dbg);

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
            gstNotifyTags(gst, tags);
            gst_tag_list_unref(tags);
        }
        break;
    }
    case GST_MESSAGE_PROPERTY_NOTIFY: {
#if 0
        GstObject *obj;
        const gchar *name;
        const GValue *val;

        gst_message_parse_property_notify(msg, &obj, &name, &val);

        gchar *val_str = NULL;
        gchar *obj_name = gst_object_get_path_string (GST_OBJECT (obj));
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

        LOG_V ("%s: %s = %s\n", obj_name, name, val_str);
        g_free (obj_name);
        g_free (val_str);
#endif
        break;
    }
    /**
     * 表示有新的#GstStreamCollection可用的消息（自1.10版本起）。
     */
    case GST_MESSAGE_STREAM_COLLECTION: {
        LOG_V("GST_MESSAGE_STREAM_COLLECTION");
        /*
        GstStreamCollection* collection = NULL;
        gst_message_parse_stream_collection (msg, &collection);

        if (collection) {
            tgst_vtable_on_stream_collection(gst, collection);
        }
        */
        break;
    }
    /**
     * 表示活动的#GstStreams选择已更改的消息（自1.10版本起）。
     */
    case GST_MESSAGE_STREAMS_SELECTED: {
        LOG_V("GST_MESSAGE_STREAMS_SELECTED");
        /*
        GstStreamCollection *collection = NULL;
        gst_message_parse_streams_selected (msg, &collection);

        if (collection) {
            tgst_vtable_on_stream_selected(gst, collection);
        }
        */
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
        LOG_V("对象[%-*s] 状态改变", 16, GST_MESSAGE_SRC_NAME(msg));

        /* 我们只关心管道状态更改消息 */
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(gst->pipeline)) {
            GstState old_state, new_state, pending_state;

            gst_message_parse_state_changed(msg,
                                            &old_state,
                                            &new_state,
                                            &pending_state);
            LOG_D("对象[%-*s] '%s' -> '%s'",
                  16,
                  GST_MESSAGE_SRC_NAME(msg),
                  gst_element_state_get_name(old_state),
                  gst_element_state_get_name(new_state));

            if ((pending_state == GST_STATE_VOID_PENDING) &&
                (gst->current_state != new_state)) {
                gst->current_state = new_state;
            }
            tgst_vtable_on_state_changed(gst,
                                         old_state,
                                         new_state,
                                         pending_state);
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
        gchar *name = gst_element_get_name(owner);
        LOG_V("流的状态改变: %s from %s", TG_GetStreamStatusName(type), name);
        g_free(name);
        tgst_vtable_on_stream_status(gst, type, owner);
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
        /* 外部有轮询线程，这里不做处理 */
        break;
    }
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *structure = gst_message_get_structure(msg);

        if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_LEVEL) &&
            gst_structure_has_name(structure, "level")) {
            gstAudioLevel(gst, structure);

        } else if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SPECTRUM) &&
                   gst_structure_has_name(structure, "spectrum")) {
            gstAudioSpectrum(gst, structure);
        }
        break;
    }
    default: {
        if (gst_is_missing_plugin_message(msg)) {
            gchar *desc = gst_missing_plugin_message_get_description(msg);
            LOG_W("缺失插件：%s", desc);
            g_free(desc);
            gst->missing = g_list_append(gst->missing, gst_message_ref(msg));
        }
        break;
    }
    }
    return TRUE;
}

static GstFlowReturn tgst_new_video_sample(GstElement *sink,
                                           gpointer user_data) {
    // 获取视频样本（包含帧数据+时间戳PTS）
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        LOG_W("样本结束");
        return GST_FLOW_EOS;
    }
    TGstVideoFrame src;
    memset(&src, 0, sizeof(TGstVideoFrame));

    // 从样本中获取缓冲区
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E("无法获取缓冲区");
        goto exit;
    }

#if 0
    LOG_I("缓冲区 PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "",
              GST_TIME_ARGS(GST_BUFFER_PTS(buffer)),
              GST_TIME_ARGS(GST_BUFFER_DTS(buffer)));
#endif

    // 映射视频帧并获取视频帧信息
    GstVideoInfo info;
    GstVideoFrame frame;
    auto caps = gst_sample_get_caps(sample);
    gst_video_info_from_caps(&info, caps);
    if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
        LOG_E("视频帧映射失败");
        goto exit;
    }

    src.w = GST_VIDEO_INFO_WIDTH(&info);
    src.h = GST_VIDEO_INFO_HEIGHT(&info);
    src.ar = (float)(src.w * GST_VIDEO_INFO_PAR_N(&info)) /
             (float)(src.h * GST_VIDEO_INFO_PAR_D(&info));
    src.data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);

    uint32_t stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);

#ifdef WINDOWS
    src.fmt = info.finfo->format;
    switch (info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12: {
        src.uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
        src.ws = stride;
        src.hs = ((gint64)src.uv_data - (gint64)src.y_data) / stride;
        break;
    }
    case GST_VIDEO_FORMAT_NV21: {
        break;
    }
    case GST_VIDEO_FORMAT_I420: {
        src.uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
        src.ws = stride;
        src.hs = ((gint64)src.uv_data - (gint64)src.y_data) / stride;
        break;
    }
    case GST_VIDEO_FORMAT_RGB: {
        src.ws = stride / 3;
        src.hs = src.h;
        break;
    }
    case GST_VIDEO_FORMAT_RGBA: {
        src.ws = stride / 4;
        src.hs = src.h;
        break;
    }
    case GST_VIDEO_FORMAT_ARGB: {
        src.ws = stride / 4;
        src.hs = src.h;
        break;
    }
    case GST_VIDEO_FORMAT_BGRA: {
        src.ws = stride / 4;
        src.hs = src.h;
        break;
    }
    default: {
        LOG_W("不支持的 GST 视频帧格式：%s",
              gst_video_format_to_string(info.finfo->format));
        goto exit_unmap_frame;
    }
    }
#endif /* WINDOWS */

#ifdef LINUX
    src.fd = -1;
    // 获取输入 FD (通过 dmabuf)
    GstMemory *mem = gst_buffer_get_all_memory(buffer);
    if (gst_is_dmabuf_memory(mem)) {
        src.fd = gst_dmabuf_memory_get_fd(mem);
    }
    gst_memory_unref(mem);

    src.fmt = RK_FORMAT_UNKNOWN;

    switch (info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12: {
        void *uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
        src.ws = stride;
        src.hs = ((gint64)uv_data - (gint64)src.data) / stride;
        src.fmt = RK_FORMAT_YCbCr_420_SP;
        break;
    }
    case GST_VIDEO_FORMAT_NV21: {
        src.fmt = RK_FORMAT_YCrCb_420_SP;
        break;
    }
    case GST_VIDEO_FORMAT_I420: {
        void *uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
        src.ws = stride;
        src.hs = ((gint64)uv_data - (gint64)src.data) / stride;
        src.fmt = RK_FORMAT_YCbCr_420_P;
        break;
    }
    case GST_VIDEO_FORMAT_RGB: {
        src.ws = stride / 3;
        src.hs = src.h;
        src.fmt = RK_FORMAT_RGB_888;
        break;
    }
    case GST_VIDEO_FORMAT_RGBA: {
        src.ws = stride / 4;
        src.hs = src.h;
        src.fmt = RK_FORMAT_RGBA_8888;
        break;
    }
    case GST_VIDEO_FORMAT_ARGB: {
        src.ws = stride / 4;
        src.hs = src.h;
        src.fmt = RK_FORMAT_RGBA_8888;
        break;
    }
    case GST_VIDEO_FORMAT_BGRA: {
        src.ws = stride / 4;
        src.hs = src.h;
        src.fmt = RK_FORMAT_RGBA_8888;
        break;
    }
    default: {
        LOG_W("不支持的 GST 视频帧格式：%s",
              gst_video_format_to_string(info.finfo->format));
        goto exit_unmap_frame;
    }
    }
#endif /*LINUX*/

#if 0
    LOG_I("in_fmt='%s' - src: %dx%d, fmt=%d, ws=%d, hs=%d, fd=%d, data=%p",
          gst_video_format_to_string(info.finfo->format),
          src.w,
          src.h,
          src.fmt,
          src.ws,
          src.hs,
          src.fd,
          src.data);
#endif

    if (src.w <= 0 || src.h <= 0 || src.ws <= 0 || src.hs <= 0) {
        LOG_E("RGA 参数非法: src %dx%d, stride %d", src.w, src.h, src.ws);
        goto exit_unmap_frame;
    }

    // 内存边界校验：确保映射后的虚拟地址有效
    if (src.data == NULL) {
        LOG_E("RGA 源数据指针为空");
        goto exit_unmap_frame;
    }

    gstNotifyVideoFrame(TGST(user_data), &src);

exit_unmap_frame:
    gst_video_frame_unmap(&frame);
exit:
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

/**
 * 准备 TGst 对象的功能
 *
 * @param gst   TGst 对象。
 *
 * @return 返回 RET_OK 表示成功，否则表示失败。
 */
static ret_t tgst_prepare_funcs(TGst *gst) {
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);

    ret_t ret = RET_OK;

    if (TG_IS_FUNC(gst, TG_FUNC_QUERY_TIMER) && gst->query_timer == 0) {
        LOG_V("TG_FUNC_QUERY_TIMER");
        gst->query_timer =
            g_timeout_add(TG_QUERY_TIMER_INTERVAL, gstQueryTimer, gst);
        gst->duration = GST_CLOCK_TIME_NONE;
        gst->position = GST_CLOCK_TIME_NONE;
    }

    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_LEVEL) && gst->audio_level == NULL) {
        LOG_V("TG_FUNC_AUDIO_LEVEL");
        tgst_element_factory_make(&gst->audio_level,
                                  GST_PLUGINS_LEVEL,
                                  "audio_level");
        g_object_set(G_OBJECT(gst->audio_level),
                     "interval",
                     (guint64)(GST_SECOND / 20),
                     "post-messages",
                     TRUE,
                     NULL);
    }

    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SPECTRUM) &&
        gst->audio_spectrum == NULL) {
        LOG_V("TG_FUNC_AUDIO_SPECTRUM");
        tgst_element_factory_make(&gst->audio_spectrum,
                                  GST_PLUGINS_SPECTRUM,
                                  "audio_spectrum");
        g_object_set(
            G_OBJECT(gst->audio_spectrum),
            // 1. 【必选】开启总线消息发送，否则不会触发任何回调！
            "post-messages",
            TRUE,

            // 2. 【必选】发送频谱数据的间隔时间（单位：纳秒）。例如每 50ms 发送一次（约 20 FPS 刷新率）
            "interval",
            (guint64)(40 * GST_MSECOND),

            // 3. 频段数量（FFT 频带个数），常见设为 64、128 或 256
            "bands",
            256,

            // 4. 分贝阈值（低于此值的静音噪声将被截断为该值），常见设为 -60 或 -80
            "threshold",
            -60,

            // 5. 是否分别输出左右声道的独立频谱（FALSE = 混合单声道频谱；TRUE = 多声道独立数组）
            "multi-channel",
            FALSE,
            NULL);
    }

    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SRC) && gst->audio_src == NULL) {
        LOG_V("TG_FUNC_AUDIO_SRC");
        tgst_element_factory_make(&gst->audio_src,
                                  TG_AUDIO_SRC_PLUGINS,
                                  "audio_src");
        g_object_set(G_OBJECT(gst->audio_src), TG_AUDIO_SRC_PROPS, NULL);
    }

    if (TG_IS_FUNC(gst, TG_FUNC_VIDEO_SRC) && gst->video_src == NULL) {
        LOG_V("TG_FUNC_VIDEO_SRC");
        tgst_element_factory_make(&gst->video_src,
                                  TG_VIDEO_SRC_PLUGINS,
                                  "video_src");
        g_object_set(G_OBJECT(gst->video_src), TG_VIDEO_SRC_PROPS, NULL);
    }

    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SINK) && gst->audio_sink == NULL) {
        LOG_V("TG_FUNC_AUDIO_SINK");

        tgst_element_factory_make(&gst->preview_audio_convert,
                                  GST_PLUGINS_AUDIO_CONVERT,
                                  "preview_audio_convert");
#ifdef WINDOWS
        tgst_element_factory_make(&gst->win_audio_resample,
                                  GST_PLUGINS_AUDIO_RESAMPLE,
                                  "win_audio_resample");
        tgst_element_factory_make(&gst->win_audio_caps,
                                  GST_PLUGINS_CAPS_FILTER,
                                  "win_audio_caps");

        GstCaps *caps = gst_caps_new_simple(TG_AUDIO_CAPS_PROPS, NULL);
        g_object_set(G_OBJECT(gst->win_audio_caps), "caps", caps, NULL);
        gst_caps_unref(caps);
#endif
        tgst_element_factory_make(&gst->audio_sink,
                                  TG_AUDIO_SINK_PLUGINS,
                                  "audio_sink");

        g_object_set(G_OBJECT(gst->audio_sink), TG_AUDIO_SINK_PROPS, NULL);
    }

    if (TG_IS_FUNC(gst, TG_FUNC_VIDEO_SINK) && gst->video_sink == NULL) {
        LOG_V("TG_FUNC_VIDEO_SINK");
#ifdef WINDOWS
        tgst_element_factory_make(&gst->win_video_convert_scale,
                                  GST_PLUGINS_VIDEO_CONVERT_SCALE,
                                  "win_video_convert_scale");
        tgst_element_factory_make(&gst->win_video_caps,
                                  GST_PLUGINS_CAPS_FILTER,
                                  "win_video_caps");
        GstCaps *caps = gst_caps_new_simple(TG_VIDEO_CAPS_PROPS, NULL);
        g_object_set(G_OBJECT(gst->win_video_caps), "caps", caps, NULL);
        gst_caps_unref(caps);
#endif
        tgst_element_factory_make(&gst->video_sink,
                                  GST_PLUGINS_APP_SINK,
                                  "video_sink");

        g_object_set(G_OBJECT(gst->video_sink), TG_VIDEO_SINK_PROPS, NULL);
        tgst_signal_connect(gst->video_sink,
                            NEW_SAMPLE,
                            G_CALLBACK(tgst_new_video_sample),
                            gst);
    }
    return ret;
}

//------------------------------------------------------------------------------

static ret_t tgst_do_start(TGst *gst) {
    LOG_V("'%s'：正在启动 (gst=%p, filename=%s) ......",
          TOBJ_NAME,
          gst,
          gst->filename);

    gst->target_state = GST_STATE_PLAYING;

    ret_t ret = tgst_vtable_on_start(gst);
    THROW_ERROR_IF_FAIL(ret == RET_OK, "'%s'：启动失败！", TOBJ_NAME);

    LOG_D("'%s'：启动结束。", TOBJ_NAME);
    return RET_OK;
}

static void tgst_do_stop(TGst *gst) {
    LOG_V("'%s'：停止中 (gst=%p) ......", TOBJ_NAME, gst);

    return_if_fail(gst->filename != NULL);

    ret_t ret = tgst_vtable_on_stop(gst);
    THROW_ERROR_IF_FAIL(ret == RET_OK, "'%s'：停止失败！", TOBJ_NAME);

    if (gst->is_playback) {
        g_list_foreach(gst->missing, (GFunc)gst_message_unref, NULL);
        gst->missing = NULL;

        gst->buffering = FALSE;
        gst->is_live = FALSE;
        gst->initial_file = TRUE;

        gst->current_state = GST_STATE_NULL;
        gst->target_state = GST_STATE_NULL;

        gst->duration = GST_CLOCK_TIME_NONE;
        gst->position = GST_CLOCK_TIME_NONE;
    }
    g_free(gst->filename);
    gst->filename = NULL;

    LOG_D("'%s'：已停止。", TOBJ_NAME);
}

//==============================================================================

TGst *gstCreate(const TGstVTable *vt, uint32_t funcs) {
    THROW_ERROR_IF_FAIL(vt != NULL, "函数参数有空指针！(vt=%p)", vt);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("'%s'：创建开始 (vt=%p, funcs=%s) ......", TOBJ_NAME, vt, text_buf);

    TGst *gst = g_malloc0(vt->size);
    THROW_ERROR_IF_FAIL(gst != NULL, "'%s'：分配内存失败！", TOBJ_NAME);
    gst->is_destroying = 0;

    gst->vt = vt;
    gst->is_prepared = FALSE;

    gst->funcs = funcs;
    gst->is_playback =
        !TG_IS_FUNC(gst, (TG_FUNC_AUDIO_SRC | TG_FUNC_VIDEO_SRC));
    LOG_V("is_playback=%s", t_bool2str(gst->is_playback));

    g_mutex_init(&gst->mutex);

    gst->audio_level = NULL;
    gst->audio_spectrum = NULL;
    gst->audio_src = NULL;
    gst->preview_audio_convert = NULL;
    gst->audio_sink = NULL;
    gst->video_src = NULL;
    gst->video_sink = NULL;

#ifdef WINDOWS
    gst->win_audio_resample = NULL;
    gst->win_audio_caps = NULL;
    gst->win_video_convert_scale = NULL;
    gst->win_video_caps = NULL;
#endif

    gst->emitter = NULL;

    gst->bus_watch = 0;

    gst->missing = NULL;
    gst->buffering = FALSE;
    gst->is_live = FALSE;
    gst->initial_file = TRUE;

    gst->current_state = GST_STATE_NULL;
    gst->target_state = GST_STATE_NULL;

    gst->query_timer = 0;
    gst->duration = GST_CLOCK_TIME_NONE;
    gst->position = GST_CLOCK_TIME_NONE;

    tgst_element_factory_make(&gst->pipeline, GST_PLUGINS_PIPELINE, "pipeline");
    THROW_ERROR_IF_FAIL(gst->pipeline != NULL, "'pipeline' 创建失败！");
    // 强制使用系统时钟，防止音视频 PTS 出现同步错误
    GstClock *clock = gst_system_clock_obtain();
    gst_pipeline_use_clock(GST_PIPELINE(gst->pipeline), clock);
    gst_object_unref(clock);

    ret_t ret = tgst_prepare_funcs(gst);
    THROW_ERROR_IF_FAIL(ret == RET_OK, "'pipeline' 准备功能失败！");

    gst->bus_watch =
        gst_bus_add_watch(GST_ELEMENT_BUS(gst->pipeline), gstBusMsg, gst);
    THROW_ERROR_IF_FAIL(gst->bus_watch != 0, "'pipeline' 增加监视器失败！");

    tgst_vtable_on_create(gst, funcs);

    if (!gst->is_playback) {
        gst->target_state = GST_STATE_PLAYING;
        tgst_set_state(gst->pipeline, GST_STATE_READY);

        switch (tgst_set_state(gst->pipeline, GST_STATE_PAUSED)) {
        case GST_STATE_CHANGE_FAILURE:
            LOG_D("无需处理，消息总线会上报一条错误信息。");
            break;

        case GST_STATE_CHANGE_NO_PREROLL:
            LOG_D("管线是实时流。");
            gst->is_live = TRUE;
            break;

        case GST_STATE_CHANGE_ASYNC:
            LOG_D("预滚中...");
            break;

        default:
            break;
        }
        if (gst->target_state != GST_STATE_PAUSED) {
            tgst_set_state(gst->pipeline, gst->target_state);
        }
    }

    LOG_D("'%s'：创建成功 (return=%p)。", TOBJ_NAME, gst);
    return gst;
}

void gstDestroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("'%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    // 1. 立即置位销毁标志，通知所有后台回调尽快退出
    g_atomic_int_set(&gst->is_destroying, 1);

    // 2. 移除轮询时钟与总线监视器（阻止主循环再触发新的 GLib 回调）
    if (TG_IS_FUNC(gst, TG_FUNC_QUERY_TIMER) && (gst->query_timer != 0)) {
        LOG_V("移除查询时钟： query_timer=%d", gst->query_timer);
        g_source_remove(gst->query_timer);
        gst->query_timer = 0;
    }

    if (gst->bus_watch != 0) {
        LOG_V("移除总线监视器： bus_watch=%d", gst->bus_watch);
        g_source_remove(gst->bus_watch);
        gst->bus_watch = 0;
    }

    // 3. 停止并释放 GStreamer 主管线（增加判空防护，阻塞直到所有后台流线程退出）
    if (gst->pipeline != NULL) {
        LOG_V("切换 GST 主管线状态为 GST_STATE_NULL......");
        tgst_set_state(gst->pipeline, GST_STATE_NULL);
        gst_object_unref(gst->pipeline);
        gst->pipeline = NULL;
    }

    // 4. 【调整到此处】管线已彻底停止，后台线程已全部退出，此时销毁继承类和事件分发器绝对安全
    LOG_V("销毁继承类......");
    tgst_vtable_on_destroy(gst);

    LOG_V("释放事件分发器......");
    if (gst->emitter != NULL) {
        emitter_destroy(gst->emitter);
        gst->emitter = NULL;
    }

    // 5. 最后销毁互斥锁与结构体本身
    LOG_V("销毁互斥锁......");
    g_mutex_clear(&gst->mutex);
    LOG_I("'%s'：销毁成功。", TOBJ_NAME);
    g_free(gst);
}

//------------------------------------------------------------------------------

const gchar *gstGetType(TGst *gst) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    static const gchar *type_unknown = "unknown";
    return gst->vt ? gst->vt->type : type_unknown;
}

ret_t gstStart(TGst *gst, const gchar *filename, ...) {
    THROW_ERROR_IF_FAIL(gst != NULL && filename != NULL,
                        "函数参数有空指针！(gst=%p, filename=%p)",
                        gst,
                        filename);
    LOG_V("'%s'：启动开始: (gst=%p, filename='%s') ......",
          TOBJ_NAME,
          gst,
          filename);
    LOG_I("Step 01");
    if (!g_mutex_trylock(&gst->mutex)) {
        THROW_RUNTIME_ERROR("无法取得互斥锁！");
    }

    LOG_I("Step 02");
    tgst_do_stop(gst);

    LOG_I("Step 03");
    gst->filename = g_strdup(filename);

    LOG_I("Step 04");
    ret_t ret = tgst_do_start(gst);
    goto_error_if_fail(ret == RET_OK);

    va_list args;
    va_start(args, filename);

    if (gst->is_playback) {
        LOG_V("'%s'：是播放状态。", TOBJ_NAME);
        if (gst->initial_file) {
            tgst_set_state(gst->pipeline, GST_STATE_READY);
        }

        if (gst->initial_file) {
            switch (tgst_set_state(gst->pipeline, GST_STATE_PAUSED)) {
            case GST_STATE_CHANGE_FAILURE:
                LOG_D("无需处理，消息总线会上报一条错误信息。");
                break;

            case GST_STATE_CHANGE_NO_PREROLL:
                LOG_D("管线是实时流。");
                gst->is_live = TRUE;
                break;

            case GST_STATE_CHANGE_ASYNC:
                LOG_D("预滚中...");
                break;

            default:
                break;
            }
            if (gst->target_state != GST_STATE_PAUSED) {
                tgst_set_state(gst->pipeline, gst->target_state);
            }
        }

        gst->initial_file = FALSE;

    } else {
        LOG_V("'%s'：是捕捉状态。", TOBJ_NAME);
        gst->duration = va_arg(args, gint64);
    }
    va_end(args);

    g_mutex_unlock(&gst->mutex);

    LOG_D("'%s'：启动成功。", TOBJ_NAME);
    return RET_OK;

error:
    g_free(gst->filename);
    gst->filename = NULL;
    g_mutex_unlock(&gst->mutex);
    return RET_FAIL;
}

ret_t gstStop(TGst *gst) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    LOG_V("'%s'：停止开始 (gst=%p) ......", TOBJ_NAME, gst);

    g_mutex_lock(&gst->mutex);

    tgst_do_stop(gst);

    if (gst->is_playback) {
        GstStateChangeReturn ret =
            tgst_set_state(gst->pipeline, GST_STATE_NULL);
        return_value_if_fail(ret != GST_STATE_CHANGE_FAILURE, RET_FAIL);
    }

    g_mutex_unlock(&gst->mutex);

    LOG_D("'%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

ret_t gstSetPaused(TGst *gst, gboolean paused) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    return_value_if_fail(gst->target_state == GST_STATE_PAUSED ||
                             gst->target_state == GST_STATE_PLAYING,
                         RET_BAD_PARAMS);
    LOG_V("'%s'：切换暂停状态开始 (gst=%p, paused=%s) ......",
          TOBJ_NAME,
          gst,
          t_bool2str(paused));

    g_mutex_lock(&gst->mutex);

    ret_t ret = tgst_vtable_on_paused(gst, paused);
    THROW_ERROR_IF_FAIL(ret == RET_OK, "'%s'：切换暂停状态失败！", TOBJ_NAME);

    g_mutex_unlock(&gst->mutex);

    LOG_D("'%s'：切换暂停状态成功。(state=%s)",
          TOBJ_NAME,
          gst_element_state_get_name(gst->target_state));
    return RET_OK;
}

uint32_t gstOn(TGst *gst, uint32_t type, event_func_t on_event, void *ctx) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    LOG_V("'%s'：添加事件回调 (gst=%p, type=%s, on_event=%p, ctx=%p) "
          "......",
          TOBJ_NAME,
          gst,
          get_tgst_event_type_name(type),
          on_event,
          ctx);

    g_mutex_lock(&gst->mutex);

    if (gst->emitter == NULL) {
        gst->emitter = emitter_create();
        LOG_I("'%s'：创建事件分发器 emitter=%p", TOBJ_NAME, gst->emitter);
    }
    uint32_t id = emitter_on(gst->emitter, type, on_event, ctx);
    if (id == 0) {
        LOG_W("'%s'：添加事件回调失败！", TOBJ_NAME);
        g_mutex_unlock(&gst->mutex);
        return 0;
    }

    LOG_D("'%s'：添加事件回调成功。", TOBJ_NAME);
    g_mutex_unlock(&gst->mutex);
    return id;
}

/* 解绑事件：直接映射到 AWTK 的 emitter_off */
void gstOff(TGst *gst, uint32_t id) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    return_if_fail(gst->emitter != NULL && id > 0);
    LOG_V("'%s'：释放事件回调 (gst=%p, id=%u) ......", TOBJ_NAME, gst, id);
    g_mutex_lock(&gst->mutex);

    emitter_off(gst->emitter, id);

    g_mutex_unlock(&gst->mutex);
    LOG_D("'%s'：释放事件回调成功。", TOBJ_NAME);
}

void gstSetState(TGst *gst, GstState state) {
    tgst_set_state(gst->pipeline, state);
}

GstState gstGetState(TGst *gst) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    return gst->current_state;
}

uint32_t gstGetCaptuareBps(TGst *gst) {
    THROW_ERROR_IF_FAIL(gst != NULL, "函数参数有空指针！(gst=%p)", gst);
    g_mutex_lock(&gst->mutex);

    uint32_t bps = tsgt_vtable_get_capture_bps(gst);

    g_mutex_unlock(&gst->mutex);
    return bps;
}

//------------------------------------------------------------------------------

void gstNotifyCompleted(TGst *gst) {
    THROW_ERROR_IF_FAIL(gst != NULL, "gst 为空指针！");
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_completed_t, TG_EVENT_COMPLETED);
        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(gst->emitter, (event_t *)&evt);
    }
}

void gstNotifyPositionChanged(TGst *gst, gint64 position) {
    THROW_ERROR_IF_FAIL(gst != NULL, "gst 为空指针！");
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    gst->position = position;

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst,
                     tgst_event_position_changed_t,
                     TG_EVENT_POSITION_CHANGED);

        /* 填充附加的自定义参数 */
        evt.position = position;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

void gstNotifyDurationChanged(TGst *gst, gint64 duration) {
    THROW_ERROR_IF_FAIL(gst != NULL, "gst 为空指针！");
    // 1. 第一道防线：无锁快速拦截
    if (g_atomic_int_get(&gst->is_destroying)) {
        return;
    }

    // 2. 加锁安全获取 emitter 指针
    g_mutex_lock(&gst->mutex);
    emitter_t *emitter = gst->emitter;
    g_mutex_unlock(&gst->mutex);

    gst->duration = duration;

    // 3. 第二道防线：解锁后、真正分发前再次确认未处于销毁状态且 emitter 有效
    if (emitter != NULL && !g_atomic_int_get(&gst->is_destroying)) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst,
                     tgst_event_duration_changed_t,
                     TG_EVENT_DURATION_CHANGED);

        /* 填充附加的自定义参数 */
        evt.duration = duration;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(emitter, (event_t *)&evt);
    }
}

void tgst_func_to_str(gchar *text_buf, uint32_t size, uint32_t funcs) {
    gboolean is_first = TRUE;
    memset(text_buf, 0, size);
#define X(func, name, value)                                                   \
    if ((funcs & TG_FUNC_##func) != 0) {                                       \
        if (!is_first) {                                                       \
            strcat(text_buf, " | ");                                           \
        }                                                                      \
        strcat(text_buf, #name);                                               \
        is_first = FALSE;                                                      \
    }
    GST_FUNC_LIST
#undef X
}

//==============================================================================
