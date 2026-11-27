/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 音视频捕获
 */

/**
 * History:
 * ================================================================
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TAudioRecorder"
#include <log.h>
//==============================================================================

#include "TAudioRecorder.h"

//------------------------------------------------------------------------------
/**
 * 调试 PTS 开关，设置为 1 时，将实时显示音频流 PTS 值
 */
#define DEBUG_PTS 0

#define TOBJ_NAME TG_TYPE_AUDIO_RECORDER

#define AUDIO_RAW "audio/x-raw"
#define IMAGE_JPEG "image/jpeg"

/*------------------------------------------------------------------------------
 * Windows 环境下宏定义
 */
#ifdef WINDOWS

#define TG_AUDIO_MAIN_ELEMENTS                                                 \
    TG_AUDIO_SRC(gst), TG_AUDIO_CAPS(gst), TG_AUDIO_TEE(gst)

#define TG_AUDIO_PREVIEW_ELEMENTS                                              \
    TG_AUDIO_QUEUE1(gst), PV_AUDIO_CONVERT(gst), WIN_AUDIO_RESAMPLE(gst),      \
        WIN_AUDIO_CAPS(gst), TG_AUDIO_LEVEL(gst), TG_AUDIO_SINK(gst)

/** 音频输入格式过滤器 */
#define TG_AUDIO_CAPS_PROPS                                                    \
    "audio/x-raw", "format", G_TYPE_STRING, "F32LE", "rate", G_TYPE_INT,       \
        48000, "channels", G_TYPE_INT, 2

#define TG_AUDIO_CAPS2_PROPS                                                   \
    "audio/x-raw", "format", G_TYPE_STRING, "S16LE", "rate", G_TYPE_INT,       \
        48000, "channels", G_TYPE_INT, 2

#define TG_AUDIO_RECORD_ELEMENT                                                \
    TG_AUDIO_VALVE(gst), TG_AUDIO_QUEUE2(gst), rec->audio_convert,             \
        rec->audio_resample, rec->audio_caps2, rec->audio_enc,                 \
        TG_FILE_SINK(gst)

#endif

/*------------------------------------------------------------------------------
 * Linux 环境下宏定义
 */
#ifdef LINUX

#define TG_AUDIO_MAIN_ELEMENTS                                                 \
    TG_AUDIO_SRC(gst), TG_AUDIO_CAPS(gst), TG_AUDIO_TEE(gst)

#define TG_AUDIO_PREVIEW_ELEMENTS                                              \
    TG_AUDIO_QUEUE1(gst), TG_AUDIO_LEVEL(gst), TG_AUDIO_SINK(gst)

/** 音频输入格式过滤器 */
#define TG_AUDIO_CAPS_PROPS                                                    \
    "audio/x-raw", "format", G_TYPE_STRING, "S16LE", "rate", G_TYPE_INT,       \
        48000, "channels", G_TYPE_INT, 2

#define TG_AUDIO_RECORD_ELEMENT                                                \
    TG_AUDIO_VALVE(gst), TG_AUDIO_QUEUE2(gst), rec->audio_enc, TG_FILE_SINK(gst)

#endif

//------------------------------------------------------------------------------

/**
 * 音频输入格式过滤
 */
typedef struct _TAudioInCaps {
    const gchar *media_type;
    const gchar *format;
    gint sample_rate;
    gint channels;
} TAudioInCaps;

/**
 * 音频录制属性
 */
typedef struct _TAudioRecordProps {
    uint32_t kbps;
} TAudioRecordProps;

#define AUDIO_IN_CAPS(type, fmt, rate, ch)                                     \
    {                                                                          \
        .media_type = type,                                                    \
        .format = fmt,                                                         \
        .rate = rate,                                                          \
        .channels = ch,                                                        \
    }

#define AUDIO_RECORD_PROPS(audio_rate)                                         \
    {                                                                          \
        .kbps = audio_rate,                                                    \
    }

//==============================================================================

static const key_name_t s_audio_record_quality_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    AUDIO_RECORD_QUALITY_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(audio_record_quality)

static const key_name_t s_audio_record_format_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    AUDIO_RECORD_FORMAT_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(audio_record_format)

//------------------------------------------------------------------------------

static void notify_quality_changed(TGst *gst, guint quality) {
    if (gst->emitter != NULL) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst,
                     tgst_event_quality_changed_t,
                     TG_EVENT_QUALITY_CHANGED);

        /* 填充附加的自定义参数 */
        evt.quality = quality;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(gst->emitter, (event_t *)&evt);
    }
}

static void notify_format_changed(TGst *gst, guint format) {
    if (gst->emitter != NULL) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst, tgst_event_format_changed_t, TG_EVENT_FORMAT_CHANGED);

        /* 填充附加的自定义参数 */
        evt.format = format;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(gst->emitter, (event_t *)&evt);
    }
}

static const char *get_enc(TAudioRecorder *rec) {
    static const char *lame_mp3_enc = GST_PLUGINS_LAME_MP3_ENC;
    static const char *wav_enc = GST_PLUGINS_WAV_ENC;

    if (rec->format == AUDIO_RECORD_FORMAT_MP3) {
        return lame_mp3_enc;
    } else if (rec->format == AUDIO_RECORD_FORMAT_WAV) {
        return wav_enc;
    } else {
        THROW_RUNTIME_ERROR("录音文件格式设置出错！");
    }
}

static uint32_t get_bps(TGst *gst) {
    TAudioRecorder *rec = TAUDIO_RECORDER(gst);
    if (rec->quality == AUDIO_RECORD_QUALITY_LOW) {
        return 96;
    } else if (rec->quality == AUDIO_RECORD_QUALITY_MEDIUM) {
        return 128;
    } else if (rec->quality == AUDIO_RECORD_QUALITY_HIGH) {
        return 192;
    } else {
        THROW_RUNTIME_ERROR("录音质量设置出错！");
    }
}

//------------------------------------------------------------------------------

#if DEBUG_PTS
static GstPadProbeReturn audio_buffer_debug_pts_cb(GstPad *pad,
                                                   GstPadProbeInfo *info,
                                                   gpointer user_data) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    gchar *name = gst_pad_get_name(pad);
    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        LOG_D("'audio:%s' 输入端: PTS=%" GST_TIME_FORMAT,
              name,
              GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));
    }
    g_free(name);
    return GST_PAD_PROBE_OK;
}
#endif

/**
 * 初始化 capture_bin 对象。出错时会断言抛出错误。
 */
static void capture_bin_init(TGst *gst) {
    LOG_V("对象 '%s:capture_bin'：构造开始 (gst=%p) ......", TOBJ_NAME, gst);

    TGstCapture *capture = TGST_CAPTURE(gst);
    TAudioRecorder *rec = TAUDIO_RECORDER(gst);

    LOG_V("[AUDIO] 构造开始......");

#ifdef WINDOWS
    tgst_element_factory_make(&rec->audio_convert,
                              GST_PLUGINS_AUDIO_CONVERT,
                              "audio_convert");

    tgst_element_factory_make(&rec->audio_resample,
                              GST_PLUGINS_AUDIO_RESAMPLE,
                              "audio_resample");

    tgst_element_factory_make(&rec->audio_caps2,
                              GST_PLUGINS_CAPS_FILTER,
                              "audio_caps2");

    tgst_element_factory_make(&rec->audio_enc, get_enc(rec), "audio_enc");

    LOG_V("[AUDIO] 设置录制格式：" TO_STRING(TG_AUDIO_CAPS2_PROPS));
    GstCaps *caps = gst_caps_new_simple(TG_AUDIO_CAPS2_PROPS, NULL);
    g_object_set(G_OBJECT(rec->audio_caps2), "caps", caps, NULL);
    gst_caps_unref(caps);
#endif
    if (rec->format == AUDIO_RECORD_FORMAT_MP3) {
        g_object_set(G_OBJECT(rec->audio_enc),
                     "target",
                     1,
                     "bitrate",
                     get_bps(gst),
                     "cbr",
                     TRUE,
                     NULL);
    }
    notify_quality_changed(gst, rec->quality);
    notify_format_changed(gst, rec->format);
    LOG_V("[AUDIO] 链接开始......");
    TG_CAPTURE_ADD(capture, TG_AUDIO_RECORD_ELEMENT, NULL);
    if (!gst_element_link_many(TG_AUDIO_RECORD_ELEMENT, NULL)) {
        THROW_RUNTIME_ERROR("[AUDIO] 链接失败！");
    }

#if DEBUG_PTS
    // 在进入 enc 之前再次检查音频 PTS（确保 enc 没有丢掉时间戳）
    tgst_add_buffer_probe(gst,
                          rec->audio_enc,
                          "src",
                          audio_buffer_debug_pts_cb);
#endif

    LOG_V("[AUDIO] 增加幽灵衬垫\n");
    if (!tgst_add_ghost_pad(TG_CAPTURE_BIN(gst),
                            "audio_sink",
                            TG_AUDIO_VALVE(gst),
                            "sink")) {
        THROW_RUNTIME_ERROR("[AUDIO] 增加幽灵衬垫失败！");
    }

    LOG_D("对象 '%s:capture_bin' 构造成功。", TOBJ_NAME);
}

static void capture_bin_deinit(TGst *gst) {
    LOG_V("对象 '%s:capture_bin'：析构开始 (gst=%p) ......", TOBJ_NAME, gst);

    TAudioRecorder *rec = TAUDIO_RECORDER(gst);

    tgst_set_state(TG_CAPTURE_BIN(gst), GST_STATE_NULL);
    TG_PIPELINE_REMOVE(gst, TG_CAPTURE_BIN(gst));

    rec->audio_enc = NULL;

    LOG_D("对象 '%s:capture_bin' 析构成功。", TOBJ_NAME);
}

//------------------------------------------------------------------------------

static void recorder_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("对象 'Gst:%s'：创建开始 (gst=%p, funcs=%s) ......",
          TOBJ_NAME,
          gst,
          text_buf);

    TG_VTABLE_FUNC(GstCapture, on_create, (gst, funcs));

    LOG_V("[AUDIO] 初始化开始......");

    LOG_V("[AUDIO] 设置输入格式：" TO_STRING(TG_AUDIO_CAPS_PROPS));

    GstCaps *caps = gst_caps_new_simple(TG_AUDIO_CAPS_PROPS, NULL);
    g_object_set(G_OBJECT(TG_AUDIO_CAPS(gst)), "caps", caps, NULL);
    gst_caps_unref(caps);

    LOG_V("[AUDIO] 添加主分支元素：" TO_STRING(TG_AUDIO_MAIN_ELEMENTS));
    TG_PIPELINE_ADD_MANY(gst, TG_AUDIO_MAIN_ELEMENTS, NULL);

    LOG_V("[AUDIO] 链接主分支元素：" TO_STRING(TG_AUDIO_MAIN_ELEMENTS));
    if (!gst_element_link_many(TG_AUDIO_MAIN_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("[AUDIO] 链接主分支元素失败！");
    }

    LOG_V("[AUDIO] 添加预览分支元素：" TO_STRING(TG_AUDIO_PREVIEW_ELEMENTS));
    TG_PIPELINE_ADD_MANY(gst, TG_AUDIO_PREVIEW_ELEMENTS, NULL);

    LOG_V("[AUDIO] 预览分支衬垫接链接：audio_tee:src_%%u -> audio_queue1:sink");
    if (!tgst_request_pad_link(TG_AUDIO_TEE(gst),
                               "src_%u",
                               TG_AUDIO_QUEUE1(gst),
                               "sink")) {
        THROW_RUNTIME_ERROR("[AUDIO] 预览分支衬垫接链接失败")
    }

    LOG_V("[AUDIO] 链接预览分支元素：" TO_STRING(TG_AUDIO_PREVIEW_ELEMENTS));
    if (!gst_element_link_many(TG_AUDIO_PREVIEW_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("[AUDIO] 链接预览分支元素失败！");
    }

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void recorder_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("对象 'Gst:%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    TG_VTABLE_FUNC(GstCapture, on_destroy, (gst));

    LOG_D("对象 'Gst:%s'：销毁成功。", TOBJ_NAME);
}

/**
 * 音视频捕捉预览开始事件
 */
static ret_t recorder_on_start(TGst *gst) {
    LOG_V("对象 'Gst:%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstCapture, on_start, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：启动失败！", TOBJ_NAME);

    capture_bin_init(gst);

    gstCaptureStart(gst);

#if 0
    /**
     * 初始化音频捕捉管线
     */
    tgst_element_factory_make(&rec->audio_convert, GST_PLUGINS_AUDIO_CONVERT,  "audio_convert");
    tgst_element_factory_make(&rec->audio_resample,GST_PLUGINS_AUDIO_RESAMPLE, "audio_resample");
    tgst_element_factory_make(&rec->audio_caps,    GST_PLUGINS_CAPS_FILTER,    "audio_caps");
    tgst_element_factory_make(&rec->audio_tee,     GST_PLUGINS_TEE,            "audio_tee");
    tgst_element_factory_make(&rec->audio_queue1,  GST_PLUGINS_QUEUE,          "audio_queue1");

    GstCaps* a_caps = gst_caps_new_simple(TGC_AUDIO_CAPS);
    g_object_set(G_OBJECT(rec->audio_caps), "caps", a_caps, NULL);
    gst_caps_unref(a_caps);

    TG_PIPELINE_ADD_MANY(gst, TGC_AUDIO_MAIN_ELEMENTS);
    gst_element_link_many(TGC_AUDIO_MAIN_ELEMENTS);

    TG_PIPELINE_ADD_MANY(gst, TGC_AUDIO_PREVIEW_ELEMENTS);
    if (!tgst_request_pad_link(capture->audio_tee, "src_%u", capture->audio_queue1, "sink")) {
        THROW_RUNTIME_ERROR("音频分支衬垫接链接失败")
    }
    gst_element_link_many(TGC_AUDIO_PREVIEW_ELEMENTS);

    TG_AUDIO_SRC_SET(gst, TGC_AUDIO_SRC_PROPS);
    g_object_set(G_OBJECT(capture->audio_queue1), TGC_AUDIO_PREVIEW_QUEUE_PROPS);
#endif
    //gstCaptureStart(gst, 4194304, rec->video_parse);

    LOG_D("对象 'Gst:%s'：启动成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t recorder_on_stop(TGst *gst) {
    LOG_V("对象 'Gst:%s'：停止开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstCapture, on_stop, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：停止失败！", TOBJ_NAME);

    capture_bin_deinit(gst);

    LOG_D("对象 'Gst:%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

TG_DECL_VTABLE(AudioRecorder) = {
    .size = sizeof(TAudioRecorder),
    .type = TG_TYPE_AUDIO_RECORDER,
    .get_parent_vt = TG_GET_PARENT_VTABLE(GstCapture),

    TG_CALLBACKS_REQUIRED(recorder),
    .on_get_capture_bps = get_bps,
};

//------------------------------------------------------------------------------

TGst *audioRecorderCreate(int quality, int format) {
    LOG_V("对象 'Gst:%s'：创建开始 (quality=%s, format=%s) ......",
          TOBJ_NAME,
          get_audio_record_quality_name(quality),
          get_audio_record_format_name(format));
    TGst *gst = gstCreate(TG_REF_VTABLE(AudioRecorder), TG_FUNC_AUDIO_CAPTURE);

    TAudioRecorder *rec = TAUDIO_RECORDER(gst);

    rec->quality = quality;
    rec->format = format;

    LOG_D("对象 'Gst:%s'：创建成功：%p", TOBJ_NAME, gst);
    return gst;
}

//==============================================================================
