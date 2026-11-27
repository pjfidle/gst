/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 录像机
 */

/**
 * History:
 * ================================================================
 * 2026-09-14 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TVideoRecorder"
#include <log.h>
//==============================================================================

#include "TVideoRecorder.h"

//------------------------------------------------------------------------------
/**
 * 调试 PTS 开关，设置为 1 时，将实时显示音视频流 PTS 值
 */
#define DEBUG_PTS 0

#define TOBJ_NAME TG_TYPE_VIDEO_RECORDER

#define MEDIA_TYPE_VIDEO_RAW "video/x-raw"
#define MEDIA_TYPE_IMAGE_JPEG "image/jpeg"

//------------------------------------------------------------------------------

#define TG_VIDEO_RECORD_MUX rec->record_mux, TG_FILE_SINK(gst)

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

/** 音频录制格式过滤器 */
#define TG_AUDIO_CAPS2_PROPS                                                   \
    "audio/x-raw", "format", G_TYPE_STRING, "S16LE", "rate", G_TYPE_INT,       \
        48000, "channels", G_TYPE_INT, 2

#define TG_AUDIO_RECORD_ELEMENT                                                \
    TG_AUDIO_VALVE(gst), TG_AUDIO_QUEUE2(gst), rec->audio_convert,             \
        rec->audio_resample, rec->audio_caps2, rec->audio_enc,                 \
        rec->audio_parse

#define TG_VIDEO_MAIN_ELEMENTS                                                 \
    TG_VIDEO_SRC(gst), TG_VIDEO_CAPS(gst), TG_VIDEO_IN(gst), TG_VIDEO_TEE(gst)

#define TG_VIDEO_PREVIEW_ELEMENT                                               \
    TG_VIDEO_QUEUE1(gst), WIN_VIDEO_CONVERT_SCALE(gst), WIN_VIDEO_CAPS(gst),   \
        TG_VIDEO_SINK(gst)

/** 视频录制格式过滤器 */
#define TG_VIDEO_CAPS2_PROPS "video/x-raw", "format", G_TYPE_STRING, "I420"

#define TG_VIDEO_RECORD_ELEMENT                                                \
    TG_VIDEO_VALVE(gst), TG_VIDEO_QUEUE2(gst), rec->video_convert,             \
        rec->video_caps2, rec->video_enc, rec->video_parse

#define TG_VIDEO_ENC_PROPS                                                     \
    "bitrate", props->video.kbps, "key-int-max", props->video.gop, "pass", 5,  \
        "quantizer", 23, "tune", 0x00000004

#endif // WINDOWS

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
    TG_AUDIO_VALVE(gst), TG_AUDIO_QUEUE2(gst), rec->audio_enc, rec->audio_parse

#define TG_VIDEO_MAIN_ELEMENTS                                                 \
    TG_VIDEO_SRC(gst), TG_VIDEO_CAPS(gst), TG_VIDEO_IN(gst), TG_VIDEO_TEE(gst)

#define TG_VIDEO_PREVIEW_ELEMENT TG_VIDEO_QUEUE1(gst), TG_VIDEO_SINK(gst)

#define TG_VIDEO_RECORD_ELEMENT                                                \
    TG_VIDEO_VALVE(gst), TG_VIDEO_QUEUE2(gst), rec->video_enc, rec->video_parse

#define TG_VIDEO_ENC_PROPS                                                     \
    "bps", props->video.kbps * 1000, "bps-max", props->video.max_kbps * 1000,  \
        "gop", props->video.gop, "header-mode", 1, "rc-mode", 1, "sei-mode", 1

#endif // LINUX

//------------------------------------------------------------------------------

typedef void (*GstVideoInCallback)(guint video_in, gpointer user_data);
typedef struct {
    GstVideoInCallback func;
    gpointer user_data;
} GstVideoInCallbackInfo;

/**
 * 视频输入格式过滤
 */
typedef struct {
    const gchar *media_type;
    const gchar *format;
    gint width;
    gint height;
    struct {
        int numerator;
        int denominator;
    } fps;
} TVideoInCaps;

#define VIDEO_IN_CAPS(type, fmt, w, h, f)                                      \
    {                                                                          \
        .media_type = type,                                                    \
        .format = fmt,                                                         \
        .width = w,                                                            \
        .height = h,                                                           \
        .fps = {.numerator = 1, .denominator = f},                             \
    }

/**
 * 视频录制属性
 */
typedef struct {
    struct { // AAC encode
        uint32_t kbps;
    } audio;

    struct { // H264 encode
        uint32_t kbps;
        uint32_t max_kbps;

        /**
         * Group of pictures starting with I frame (-1 = FPS, 1 = all I frames)
         */
        uint32_t gop;
    } video;
} TVideoRecordProps;

#define VIDEO_RECORD_PROPS(audio_rate, video_rate, max_video_rate, video_gop)  \
    {                                                                          \
        .audio =                                                               \
            {                                                                  \
                .kbps = audio_rate,                                            \
            },                                                                 \
        .video = {.kbps = video_rate,                                          \
                  .max_kbps = max_video_rate,                                  \
                  .gop = video_gop}                                            \
    }

typedef struct {
    const TVideoInCaps *caps;
    const TVideoRecordProps *props;
} TVideoRecordParam;

//==============================================================================

static const key_name_t s_input_source_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    INPUT_SOURCE_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(input_source)

static const key_name_t s_video_in_standard_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    VIDEO_IN_STANDARD_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(video_in_standard)

static const key_name_t s_video_record_quality_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    VIDEO_RECORD_QUALITY_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(video_record_quality)

static const key_name_t s_video_record_format_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    VIDEO_RECORD_FORMAT_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(video_record_format)

static const key_name_t s_video_record_timestamp_list[] = {
#define X(key, value) KEY_NAME_MAKE(key, value),
    VIDEO_RECORD_TIMESTAMP_LIST
#undef X
};
FUNC_KEY_NAME_GET_ALL(video_record_timestamp)

// clang-format off
static const TVideoInCaps s_caps_ntsc_p30 = 
    VIDEO_IN_CAPS(MEDIA_TYPE_VIDEO_RAW,  "YUY2", 720, 480, 30);
static const TVideoInCaps s_caps_pal_p25  = 
    VIDEO_IN_CAPS(MEDIA_TYPE_VIDEO_RAW,  "YUY2", 720, 576, 25);

static const TVideoRecordProps s_ntsc_props[] = {
    VIDEO_RECORD_PROPS( 96, 1200, 1500, 60), // 低码率档 (1.2 Mbps)：GOP=60
    VIDEO_RECORD_PROPS(128, 3000, 3800, 60), // 中码率档 (3.0 Mbps)：GOP=60
    VIDEO_RECORD_PROPS(192, 4000, 6000, 60), // 高码率档 (5.0 Mbps)：GOP=60
};

static const TVideoRecordProps s_pal_props[] = {
    VIDEO_RECORD_PROPS( 96, 1000, 1300, 50), // 低码率档 (1.0 Mbps)：画面静态时极省空间，GOP=50 保证压缩率
    VIDEO_RECORD_PROPS(128, 2500, 3200, 50), // 中码率档 (2.5 Mbps)：画质与体积的平衡点，GOP=50
    VIDEO_RECORD_PROPS(192, 4500, 5500, 50), // 高码率档 (4.5 Mbps)：标清画质天花板，GOP=50
};

static const TVideoRecordParam s_normal_params[] = {
    {.caps = &s_caps_ntsc_p30, .props = s_ntsc_props}, // NTSC_358
    {.caps = &s_caps_ntsc_p30, .props = s_ntsc_props}, // NTSC_443
    {.caps = &s_caps_pal_p25,  .props = s_pal_props},  // PAL
    {.caps = &s_caps_ntsc_p30, .props = s_ntsc_props}, // PAL_M
    {.caps = &s_caps_pal_p25,  .props = s_pal_props},  // PAL_NC
    {.caps = &s_caps_pal_p25,  .props = s_pal_props},  // SECAM
    {.caps = &s_caps_pal_p25,  .props = s_pal_props},  // PAL_60
    {.caps = &s_caps_ntsc_p30, .props = s_ntsc_props}, // 默认 NTSC_358
};
// clang-format on

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

static void notify_video_in_source_changed(TGst *gst, guint source) {
    if (gst->emitter != NULL) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst,
                     video_in_source_changed_t,
                     TG_EVENT_VIDEO_IN_SOURCE_CHANGED);

        evt.source = source;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(gst->emitter, (event_t *)&evt);
    }
}

static void notify_video_in_standard_changed(TGst *gst, guint standard) {
    if (gst->emitter != NULL) {
        /* 初始化标准事件头 */
        T_EVENT_MAKE(gst,
                     video_in_standard_changed_t,
                     TG_EVENT_VIDEO_IN_STANDARD_CHANGED);

        evt.standard = standard;

        /* 触发分发，所有订阅了该事件的 UI 控件都会收到通知 */
        emitter_dispatch(gst->emitter, (event_t *)&evt);
    }
}

static void on_input_source_changed(guint source, gpointer user_data) {
    TGst *gst = TGST(user_data);
    TVideoRecorder *rec = TVIDEO_RECORDER(user_data);
    LOG_D("source: %s", get_input_source_name(source));

    g_mutex_lock(&gst->mutex);

    if (rec->vin_source != source) {
        rec->vin_source = source;
        notify_video_in_source_changed(gst, source);
    }

    g_mutex_unlock(&gst->mutex);
}

static void on_video_in_standard_changed(guint standard, gpointer user_data) {
    TGst *gst = TGST(user_data);
    TVideoRecorder *rec = TVIDEO_RECORDER(user_data);
    LOG_D("standard: %s", get_video_in_standard_name(standard));

    g_mutex_lock(&gst->mutex);

    if (rec->vin_standard != standard) {
        rec->vin_standard = standard;
        notify_video_in_standard_changed(TGST(user_data), standard);
    }

    g_mutex_unlock(&gst->mutex);
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
static GstPadProbeReturn video_buffer_debug_pts_cb(GstPad *pad,
                                                   GstPadProbeInfo *info,
                                                   gpointer user_data) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    gchar *name = gst_pad_get_name(pad);
    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        LOG_D("'vodeo:%s' 输入端: PTS=%" GST_TIME_FORMAT,
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
    TVideoRecorder *rec = TVIDEO_RECORDER(gst);

    LOG_V("[MUX] 构造开始......");
    notify_quality_changed(gst, rec->quality);

    if (rec->format == VIDEO_RECORD_FORMAT_MP4) {
        tgst_element_factory_make(&rec->record_mux,
                                  GST_PLUGINS_MP4_MUX,
                                  "record_mux");
    } else if (rec->format == VIDEO_RECORD_FORMAT_MKV) {
        tgst_element_factory_make(&rec->record_mux,
                                  GST_PLUGINS_MATROSKA_MUX,
                                  "record_mux");
    }
    notify_format_changed(gst, rec->format);

    TG_CAPTURE_ADD(capture, TG_VIDEO_RECORD_MUX, NULL);
    LOG_V("[MUX] 链接开始......");
    if (!gst_element_link_many(TG_VIDEO_RECORD_MUX, NULL)) {
        THROW_RUNTIME_ERROR("[MUX] 链接失败！");
    }

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
    LOG_V("[AUDIO] 设置录制格式：" TO_STRING(TG_AUDIO_CAPS2_PROPS));
    GstCaps *a_caps = gst_caps_new_simple(TG_AUDIO_CAPS2_PROPS, NULL);
    g_object_set(G_OBJECT(rec->audio_caps2), "caps", a_caps, NULL);
    gst_caps_unref(a_caps);
#endif
    tgst_element_factory_make(&rec->audio_enc,
                              GST_PLUGINS_VO_AAC_ENC,
                              "audio_enc");

    tgst_element_factory_make(&rec->audio_parse,
                              GST_PLUGINS_AAC_PARSE,
                              "audio_parse");

    LOG_V("[AUDIO] 链接开始......");
    TG_CAPTURE_ADD(capture, TG_AUDIO_RECORD_ELEMENT, NULL);
    if (!gst_element_link_many(TG_AUDIO_RECORD_ELEMENT, NULL)) {
        THROW_RUNTIME_ERROR("[AUDIO] 链接失败！");
    }

#if DEBUG_PTS
    // 在进入 Muxer 之前再次检查音频 PTS（确保 Parser 没有丢掉时间戳）
    tgst_add_buffer_probe(gst,
                          rec->audio_parse,
                          "src",
                          audio_buffer_debug_pts_cb);
#endif

    LOG_V("[AUDIO] 请求复用器衬垫并链接编码后的音频流");
    if (!tgst_request_pad_link(rec->record_mux,
                               "audio_%u",
                               rec->audio_parse,
                               "src")) {
        THROW_RUNTIME_ERROR("[AUDIO] 复用器衬垫链接失败！");
    }

    LOG_V("[AUDIO] 增加幽灵衬垫\n");
    if (!tgst_add_ghost_pad(TG_CAPTURE_BIN(gst),
                            "audio_sink",
                            TG_AUDIO_VALVE(gst),
                            "sink")) {
        THROW_RUNTIME_ERROR("[AUDIO] 增加幽灵衬垫失败！");
    }

    LOG_V("[VIDEO] 构造开始......");
#ifdef WINDOWS
    tgst_element_factory_make(&rec->video_convert,
                              GST_PLUGINS_VIDEO_CONVERT,
                              "video_convert");

    tgst_element_factory_make(&rec->video_caps2,
                              GST_PLUGINS_CAPS_FILTER,
                              "video_caps2");

    tgst_element_factory_make(&rec->video_enc,
                              GST_PLUGINS_X264_ENC,
                              "video_enc");
    LOG_V("[VIDEO] 设置录制格式：" TO_STRING(TG_VIDEO_CAPS2_PROPS));
    GstCaps *v_caps = gst_caps_new_simple(TG_VIDEO_CAPS2_PROPS, NULL);
    g_object_set(G_OBJECT(rec->video_caps2), "caps", v_caps, NULL);
    gst_caps_unref(v_caps);
#endif
#ifdef LINUX
    tgst_element_factory_make(&rec->video_enc,
                              GST_PLUGINS_MPP_H264_ENC,
                              "video_enc");
#endif

    tgst_element_factory_make(&rec->video_parse,
                              GST_PLUGINS_H264_PARSE,
                              "video_parse");

#if DEBUG_PTS
    // 在进入 Muxer 之前再次检查视频 PTS（确保 Parser 没有丢掉时间戳）
    tgst_add_buffer_probe(gst,
                          rec->video_parse,
                          "src",
                          video_buffer_debug_pts_cb);
#endif

    TG_CAPTURE_ADD(capture, TG_VIDEO_RECORD_ELEMENT, NULL);
    LOG_V("[VIDEO] 链接开始......");
    if (!gst_element_link_many(TG_VIDEO_RECORD_ELEMENT, NULL)) {
        THROW_RUNTIME_ERROR("[VIDEO] 链接失败！");
    }
    LOG_V("[VIDEO] 请求复用器衬垫并链接编码后的视频流");
    if (!tgst_request_pad_link(rec->record_mux,
                               "video_%u",
                               rec->video_parse,
                               "src")) {
        THROW_RUNTIME_ERROR("[VIDEO] 复用器衬垫链接失败！");
    }
    LOG_V("[VIDEO] 增加幽灵衬垫\n");
    if (!tgst_add_ghost_pad(TG_CAPTURE_BIN(gst),
                            "video_sink",
                            TG_VIDEO_VALVE(gst),
                            "sink")) {
        THROW_RUNTIME_ERROR("[VIDEO] 增加幽灵衬垫失败！");
    }

    LOG_D("对象 '%s:capture_bin' 构造成功。", TOBJ_NAME);
}

static void capture_bin_deinit(TGst *gst) {
    LOG_V("对象 '%s:capture_bin'：析构开始 (gst=%p) ......", TOBJ_NAME, gst);

    TVideoRecorder *rec = TVIDEO_RECORDER(gst);

    tgst_element_pad_unlink(rec->record_mux, rec->audio_parse, "src");
    tgst_element_pad_unlink(rec->record_mux, rec->video_parse, "src");

    tgst_set_state(TG_CAPTURE_BIN(gst), GST_STATE_NULL);
    TG_PIPELINE_REMOVE(gst, TG_CAPTURE_BIN(gst));

#ifdef WINDOWS
    rec->audio_convert = NULL;
    rec->audio_resample = NULL;
    rec->audio_caps2 = NULL;
    rec->video_convert = NULL;
    rec->video_caps2 = NULL;
#endif

    rec->audio_enc = NULL;
    rec->audio_parse = NULL;
    rec->video_enc = NULL;
    rec->video_parse = NULL;

    rec->record_mux = NULL;

    TG_CAPTURE_BIN(gst) = NULL;

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

    TVideoRecorder *rec = TVIDEO_RECORDER(gst);

    LOG_V("[AUDIO] 初始化开始......");

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

    LOG_V("[VIDEO] 初始化开始......");
    gint value;
    TG_VIDEO_IN_GET(gst, "source", &value, NULL);
    rec->vin_source = value;
    TG_VIDEO_IN_GET(gst, "standard", &value, NULL);
    LOG_V("vin_standard=%s", get_video_in_standard_name(value));
    rec->vin_standard =
        value != VIDEO_IN_STANDARD_UNKNOWN ? value : VIDEO_IN_STANDARD_NTSC_358;
    GstVideoInCallbackInfo info;
    info.func = on_input_source_changed;
    info.user_data = rec;
    TG_VIDEO_IN_SET(gst, "source-changed", (gpointer)&info, NULL);
    info.func = on_video_in_standard_changed;
    info.user_data = rec;
    TG_VIDEO_IN_SET(gst, "standard-changed", (gpointer)&info, NULL);

    const TVideoRecordParam *param = &s_normal_params[rec->vin_standard];
    const TVideoInCaps *caps = param->caps;

    LOG_V("[VIDEO] 设置输入格式：%s, format=%s, width=%d, height=%d, "
          "framerate=%d/%d",
          caps->media_type,
          caps->format,
          caps->width,
          caps->height,
          caps->fps.numerator,
          caps->fps.denominator);

    GstCaps *v_caps = gst_caps_new_empty_simple(caps->media_type);
    // clang-format off
    gst_caps_set_simple(v_caps,
        "format",    G_TYPE_STRING,     caps->format, // 像素格式
        "width",     G_TYPE_INT,        caps->width, // 宽度
        "height",    G_TYPE_INT,        caps->height, // 高度
        "framerate", GST_TYPE_FRACTION, caps->fps.denominator, caps->fps.numerator, // 帧率
        NULL);
    // clang-format on

    TG_VIDEO_CAPS_SET(gst, "caps", v_caps, NULL);
    gst_caps_unref(v_caps);

    LOG_V("[VIDEO] 添加主分支元素：" TO_STRING(TG_VIDEO_MAIN_ELEMENTS));
    TG_PIPELINE_ADD_MANY(gst, TG_VIDEO_MAIN_ELEMENTS, NULL);
    LOG_V("[VIDEO] 链接主分支元素：" TO_STRING(TG_VIDEO_MAIN_ELEMENTS));
    if (!gst_element_link_many(TG_VIDEO_MAIN_ELEMENTS, NULL)) {
        THROW_RUNTIME_ERROR("[VIDEO] 链接主分支元素失败！");
    }

    LOG_V("[VIDEO] 添加预览分支元素：" TO_STRING(TG_VIDEO_PREVIEW_ELEMENT));
    TG_PIPELINE_ADD_MANY(gst, TG_VIDEO_PREVIEW_ELEMENT, NULL);
    LOG_V("[VIDEO] 预览分支衬垫接链接：video_tee:src_%%u -> video_queue1:sink");
    if (!tgst_request_pad_link(TG_VIDEO_TEE(gst),
                               "src_%u",
                               TG_VIDEO_QUEUE1(gst),
                               "sink")) {
        THROW_RUNTIME_ERROR("[VIDEO] 预览分支衬垫接链接失败")
    }

    LOG_V("[VIDEO] 链接预览分支元素：" TO_STRING(TG_VIDEO_PREVIEW_ELEMENT));
    if (!gst_element_link_many(TG_VIDEO_PREVIEW_ELEMENT, NULL)) {
        THROW_RUNTIME_ERROR("[VIDEO] 链接预览分支元素失败！");
    }

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void recorder_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("对象 'Gst:%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    TG_VTABLE_FUNC(GstCapture, on_destroy, (gst));

    LOG_D("对象 'Gst:%s'：销毁成功。", TOBJ_NAME);
}

static ret_t recorder_on_start(TGst *gst) {
    LOG_V("对象 'Gst:%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstCapture, on_start, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：启动失败！", TOBJ_NAME);

    TVideoRecorder *rec = TVIDEO_RECORDER(gst);

    capture_bin_init(gst);

    const TVideoRecordParam *param = &s_normal_params[rec->vin_standard];
    const TVideoRecordProps *props = &param->props[rec->quality];

    LOG_V("audio(kbps=%u) video(kbps=%u max-kbps=%u gop=%u)",
          props->audio.kbps,
          props->video.kbps,
          props->video.max_kbps,
          props->video.gop);

    // clang-format off
    g_object_set(G_OBJECT(rec->audio_enc),
        "bitrate", props->audio.kbps * 1000,     // 比特率(bps)
        NULL);

    g_object_set(G_OBJECT(rec->video_enc), TG_VIDEO_ENC_PROPS, NULL);
#if 0
    g_object_set(G_OBJECT(rec->record_mux), 
        "start-time-selection", 0, 
        "fragment-duration", 2000,
        "streamable", FALSE, 
        NULL);
#endif
    // clang-format on

    gstCaptureStart(gst);

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

static uint32_t recorder_get_bps(TGst *gst) {
    TVideoRecorder *rec = TVIDEO_RECORDER(gst);
    const TVideoRecordParam *param = &s_normal_params[rec->vin_standard];
    return (param->props->audio.kbps + param->props->video.kbps) * 1000;
}

TG_DECL_VTABLE(VideoRecorder) = {
    .size = sizeof(TVideoRecorder),
    .type = TG_TYPE_VIDEO_RECORDER,
    .get_parent_vt = TG_GET_PARENT_VTABLE(GstCapture),

    TG_CALLBACKS_REQUIRED(recorder),
    .on_get_capture_bps = recorder_get_bps,
};

//------------------------------------------------------------------------------

TGst *videoRecorderCreate(uint32_t quality, uint32_t format) {
    LOG_V("对象 'Gst:%s'：创建开始 (quality=%s, format=%s) ......",
          TOBJ_NAME,
          get_video_record_quality_name(quality),
          get_video_record_format_name(format));
    TGst *gst = gstCreate(TG_REF_VTABLE(VideoRecorder), TG_FUNC_VIDEO_CAPTURE);

    TVideoRecorder *rec = TVIDEO_RECORDER(gst);

    rec->quality = quality;
    rec->format = format;

    LOG_D("对象 'Gst:%s'：创建成功：%p", TOBJ_NAME, gst);
    return gst;
}

//==============================================================================
