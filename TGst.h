/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *      Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *      gst base object
 */

/**
 * History:
 * ================================================================
 * 2026-09-01 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_H
#define T_GST_H
//------------------------------------------------------------------------------

#include "../my_utils.h"
#include "TGstHelper.h"

//------------------------------------------------------------------------------

#define GST_CALL_EXT_FUNC(gst, func, ...)                                      \
    if (gst->ops != NULL && gst->ops->func != NULL) {                          \
        gst->ops->func(gst, ##__VA_ARGS__);                                    \
    }

#define GST_RET_CALL_EXT_FUNC(gst, ret, func, ...)                             \
    if (gst->ops != NULL && gst->ops->func != NULL) {                          \
        ret = gst->ops->func(gst, ##__VA_ARGS__);                              \
    }

#define GST_OPS(name)                                                          \
    static tgst_ops_t s_gst_ops = {                                            \
        .type = #name,                                                         \
        .ctx = NULL,                                                           \
        .on_create = name##_on_create,                                         \
        .on_destroy = name##_on_destroy,                                       \
        .on_start = name##_on_start,                                           \
        .on_stop = name##_on_stop,                                             \
    }

// clang-format off
/**
 * 定义 GST 支持的功能
 */
#define GST_FUNC_LIST \
/*　轮询时钟，用于上报长度和位置　－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(QUERY_TIMER,      query_timer,    (1 << 0))                              \
/*　音频电平　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(AUDIO_LEVEL,      audio_level,    (1 << 1))                              \
/*　音频频谱　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(AUDIO_SPECTRUM,   audio_spectrum, (1 << 2))                              \
/*　音频输入　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(AUDIO_SRC,        audio_src,      (1 << 3))                              \
/*　音频输出　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(AUDIO_SINK,       audio_sink,     (1 << 4))                              \
/*　视频输入　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(VIDEO_SRC,        video_src,      (1 << 5))                              \
/*　视频输出　－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－－ */\
    X(VIDEO_SINK,       video_sink,     (1 << 6))

/**
 * 定义 GST 专属的事件类型
 */
#define TG_EVENT_LIST                                                          \
    /** 音频电平事件 */ \
    X(AUDIO_LEVEL,      audio_level,      gint32 left; gint32 right)           \
    /** 音频频谱事件 */ \
    X(AUDIO_SPECTRUM,   audio_spectrum,   float *bars)                         \
    /* 抓到新视频帧事件 */ \
    X(VIDEO_FRAME,      video_frame,      TGstVideoFrame * frame)              \
    /* 播放或录制结束事件，录制时超过录制时长或空间不足时触发 */ \
    X(COMPLETED,        completed,        )                                    \
    /* 警告事件 */ \
    X(WARNING,          warning,          GQuark domain; gint code)            \
    /* 错误事件 */ \
    X(ERROR,            error,            GQuark domain; gint code)            \
    /* 提取标签事件 */ \
    X(TAGS,             tags,             gchar *title; gchar *artist; gchar *album) \
    /* 媒体时长改变事件 */ \
    X(DURATION_CHANGED, duration_changed, gint64 duration)                     \
    /* 媒体位置改变事件 */ \
    X(POSITION_CHANGED, position_changed, gint64 position)                     \
    /* 媒体品质改变事件 */ \
    X(QUALITY_CHANGED,  quality_changed,  guint quality)                       \
    /* 媒体格式改变事件 */ \
    X(FORMAT_CHANGED,   format_changed,   guint format)

// clang-format on

//------------------------------------------------------------------------------

#define TG_MUTEX_LOCK(gst) g_mutex_lock(&(TGST(gst)->mutex))
#define TG_MUTEX_UNLOCK(gst) g_mutex_unlock(&(TGST(gst)->mutex))

#define TG_AUDIO_LEVEL(gst) (TGST(gst)->audio_level)
#define TG_AUDIO_SPECTRUM(gst) (TGST(gst)->audio_spectrum)
#define TG_AUDIO_SRC(gst) (TGST(gst)->audio_src)
#define TG_AUDIO_SINK(gst) (TGST(gst)->audio_sink)
#define TG_VIDEO_SRC(gst) (TGST(gst)->video_src)
#define TG_VIDEO_SINK(gst) (TGST(gst)->video_sink)
#define PV_AUDIO_CONVERT(gst) (TGST(gst)->preview_audio_convert)

#define TG_PIPELINE_SET(gst, ...)                                              \
    g_object_set(G_OBJECT(TGST(gst)->pipeline), ##__VA_ARGS__)
#define TG_PIPELINE_ADD(gst, element)                                          \
    gst_bin_add(GST_BIN(TGST(gst)->pipeline), element)
#define TG_PIPELINE_REMOVE(gst, element)                                       \
    gst_bin_remove(GST_BIN(TGST(gst)->pipeline), element)
#define TG_PIPELINE_ADD_MANY(gst, ...)                                         \
    gst_bin_add_many(GST_BIN(TGST(gst)->pipeline), ##__VA_ARGS__)
#define TG_PIPELINE_REMOVE_MANY(gst, ...)                                      \
    gst_bin_remove_many(GST_BIN(TGST(gst)->pipeline), ##__VA_ARGS__)

#ifdef WINDOWS
#define WIN_AUDIO_RESAMPLE(gst) (TGST(gst)->win_audio_resample)
#define WIN_AUDIO_CAPS(gst) (TGST(gst)->win_audio_caps)
#define WIN_VIDEO_CONVERT_SCALE(gst) (TGST(gst)->win_video_convert_scale)
#define WIN_VIDEO_CAPS(gst) (TGST(gst)->win_video_caps)
#endif

#if 0
#define TG_AUDIO_SRC_SET(gst, ...)                                             \
    g_object_set(G_OBJECT(TGST(gst)->audio_src), ##__VA_ARGS__)
#define TG_AUDIO_SINK_SET(gst, ...)                                            \
    g_object_set(G_OBJECT(TGST(gst)->audio_sink), ##__VA_ARGS__)
#define TG_VIDEO_SRC_SET(gst, ...)                                             \
    g_object_set(G_OBJECT(TGST(gst)->video_src), ##__VA_ARGS__)
#define TG_VIDEO_SINK_SET(gst, ...)                                            \
    g_object_set(G_OBJECT(TGST(gst)->video_sink), ##__VA_ARGS__)
#endif

#define TG_CALLBACKS_REQUIRED(vt)                                              \
    .on_create = vt##_on_create, .on_destroy = vt##_on_destroy,                \
    .on_start = vt##_on_start, .on_stop = vt##_on_stop

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

typedef enum {
#define X(func, name, value) TG_FUNC_##func = value,
    GST_FUNC_LIST
#undef X
        TG_FUNC_COUNT,
    /** 音乐播放功能集 */
    TG_FUNC_MUSIC_PLAYBACK =
        (TG_FUNC_QUERY_TIMER | TG_FUNC_AUDIO_SPECTRUM | TG_FUNC_AUDIO_SINK),
    /** 图片查看功能集 */
    TG_FUNC_PICTURE_VIEW = (TG_FUNC_VIDEO_SINK),
    /** 视频播放功能集 */
    TG_FUNC_VIDEO_PLAYBACK =
        (TG_FUNC_QUERY_TIMER | TG_FUNC_AUDIO_SINK | TG_FUNC_VIDEO_SINK),
    /** 音频采集功能集 */
    TG_FUNC_AUDIO_CAPTURE = (TG_FUNC_QUERY_TIMER | TG_FUNC_AUDIO_SRC |
                             TG_FUNC_AUDIO_LEVEL | TG_FUNC_AUDIO_SINK),
    /** 视频采集功能集 */
    TG_FUNC_VIDEO_CAPTURE =
        (TG_FUNC_QUERY_TIMER | TG_FUNC_AUDIO_SRC | TG_FUNC_AUDIO_LEVEL |
         TG_FUNC_AUDIO_SINK | TG_FUNC_VIDEO_SRC | TG_FUNC_VIDEO_SINK),
} TGstFuncFlags;

#define TG_IS_FUNC(gst, func) ((TGST(gst)->funcs & func) != 0)

typedef enum {
#define X(type, name, value_def) TG_EVENT_##type,
    TG_EVENT_LIST
#undef X
        TG_EVENT_COUNT,
    TG_EVENT_USER_START = TG_EVENT_COUNT,
} tgst_event_type_t;
DECL_KEY_NAME_GET_NAME(tgst_event_type)

#define X(type, name, value_def)                                               \
    typedef struct {                                                           \
        event_t e;                                                             \
        value_def;                                                             \
    } tgst_event_##name##_t;
TG_EVENT_LIST
#undef X

//------------------------------------------------------------------------------
/**
 * TGst 对象正在创建。
 *
 * @param gst       TGst 对象
 * @param funcs     功能标志。
 */
typedef void (*TGstOnCreate)(TGst *gst, uint32_t funcs);

/**
 * TGst 对象正在销毁。
 *
 * @param gst       TGst 对象
 */
typedef void (*TGstOnDestroy)(TGst *gst);

/**
 * TGst 对象正准备开始播放或录制。
 *
 * @param gst   TGst 对象。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnStart)(TGst *gst);

/**
 * TGst 对象正在销毁。
 *
 * @param gst   TGst 对象。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnStop)(TGst *gst);

/**
 * TGst 对象暂停。
 *
 * @param gst       TGst 对象。
 * @param paused    是否暂停。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnPaused)(TGst *gst, gboolean paused);

/**
 * 通知异步状态变更操作已完成。
 *
 * @param gst   TGst 对象。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnASyncDone)(TGst *gst);

/**
 * 管线结束。
 *
 * @param gst   TGst 对象。
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnEos)(TGst *gst);

/**
 * 警告错误，管线可以继续流转。
 *
 * @param gst       TGst 对象。
 * @param err       错误类型等详细信息
 * @param debug 字符串形式的错误信息，utf_8，根据系统语言设定，自动输出多语言
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnWarning)(TGst *gst, GError *err, gchar *debug);

/**
 * 严重错误，管线需立即停止流转。
 *
 * @param gst       TGst 对象。
 * @param err       错误类型等详细信息
 *                  err->domain 为错误大的类别
 *                      GST_CORE_ERROR
 *                      GST_LIBRARY_ERROR
 *                      GST_RESOURCE_ERROR
 *                      GST_STREAM_ERROR
 *                  err->code 错误的子分支
 *                      详细内容
 *
 * @param debug 字符串形式的错误信息，utf_8，根据系统语言设定，自动输出多语言
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnError)(TGst *gst, GError *err, gchar *debug);

/**
 * 读取到媒体的标签信息
 *
 * @param gst       TGst 对象。
 * @param tags      标签信息
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnTags)(TGst *gst, GstTagList *tags);

typedef ret_t (*TGstOnStreamCollection)(TGst *gst, GstStreamCollection *coll);
typedef ret_t (*TGstOnStreamSelected)(TGst *gst, GstStreamCollection *coll);

/**
 * 管线状态发生改变
 *
 * @GST_STATE_VOID_PENDING: 无待处理状态。
 * @GST_STATE_NULL        : 元素的 NULL 状态或初始状态。
 * @GST_STATE_READY       : 元素已准备好进入 PAUSED 状态。
 * @GST_STATE_PAUSED      : 元素处于 PAUSED 状态，已准备好接收和处理数据。
 *                          但 sink 元素仅接受一个缓冲区，之后会阻塞。
 * @GST_STATE_PLAYING     : 元素处于 PLAYING 状态，#GstClock 正在运行，
 *                          数据正在流转。
 * @param gst           TGst 对象。
 * @param old_state     旧状态（切换前的状态）
 * @param new_state     新状态（切换后的最终状态）
 * @param pending_state 待处理状态（正在切换的目标状态）
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnStateChanged)(TGst *gst,
                                    GstState old_state,
                                    GstState new_state,
                                    GstState pending_state);

/**
 * 流状态发生改变
 *
 * typedef enum {
 *     GST_STREAM_STATUS_TYPE_CREATE    = 0,  // 流线程即将被创建
 *     GST_STREAM_STATUS_TYPE_ENTER     = 1,  // 流线程进入主循环
 *     GST_STREAM_STATUS_TYPE_LEAVE     = 2,  // 流线程离开主循环
 *     GST_STREAM_STATUS_TYPE_DESTROY   = 3,  // 流线程被销毁
 *     GST_STREAM_STATUS_TYPE_START     = 8,  // 流线程开始运行（播放）
 *     GST_STREAM_STATUS_TYPE_PAUSE     = 9,  // 流线程暂停
 *     GST_STREAM_STATUS_TYPE_STOP      = 10  // 流线程停止
 * } GstStreamStatusType;
 *
 * @param gst       TGst 对象。
 * @param type      流状态
 * @param owner     发生状态改变的元素
 *
 * @return  返回 RET_OK 表示成功，否则表示失败。
 */
typedef ret_t (*TGstOnStreamStatus)(TGst *gst,
                                    GstStreamStatusType type,
                                    GstElement *owner);

typedef ret_t (*TGstOnGetCaptureBps)(TGst *gst);

//------------------------------------------------------------------------------

typedef const TGstVTable *(*TGstGetVTable)(void);

struct _TGstVTable {
    size_t size;
    const gchar *type;

    /**
     * 该属性用于静态继承的时候的获取父类虚表指针。
     */
    TGstGetVTable get_parent_vt;

    /* 以下为必须实现的接口 */
    TGstOnCreate on_create;
    TGstOnDestroy on_destroy;
    TGstOnStart on_start;
    TGstOnStop on_stop;

    /* 以下为可以忽略的接口 */
    TGstOnPaused on_paused;

    /** 通知异步状态变更操作已完成 */
    TGstOnASyncDone on_async_done;

    TGstOnEos on_eos;

    TGstOnWarning on_warning;
    TGstOnWarning on_error;

    TGstOnTags on_tags;

    TGstOnStreamCollection on_stream_collection;
    TGstOnStreamSelected on_stream_selected;

    TGstOnStateChanged on_state_changed;
    TGstOnStreamStatus on_stream_status;

    TGstOnGetCaptureBps on_get_capture_bps;
};

typedef struct {
    /**
     * TGst 对象正在创建。
     *
     * @param gst       TGst 对象
     * @param funcs     功能标志。
     */
    void (*on_create)(TGst *gst, uint32_t funcs);

    /**
     * TGst 对象正在销毁。
     *
     * @param gst       TGst 对象
     */
    void (*on_destroy)(TGst *gst);

    /**
     * TGst 对象正准备开始播放或录制。
     *
     * @param gst   TGst 对象。
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_start)(TGst *gst);

    /**
     * TGst 对象正在销毁。
     *
     * @param gst   TGst 对象。
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_stop)(TGst *gst);

    /**
     * TGst 对象暂停。
     *
     * @param gst       TGst 对象。
     * @param paused    是否暂停。
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_paused)(TGst *gst, gboolean paused);

    /**
     * 通知异步状态变更操作已完成。
     *
     * @param gst   TGst 对象。
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_async_done)(TGst *gst);

    /**
     * 管线结束。
     *
     * @param gst   TGst 对象。
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_eos)(TGst *gst);

    /**
     * 警告错误，管线可以继续流转。
     *
     * @param gst       TGst 对象。
     * @param err       错误类型等详细信息
     * @param debug 字符串形式的错误信息，utf_8，根据系统语言设定，自动输出多语言
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_warning)(TGst *gst, GError *err, gchar *debug);

    /**
     * 严重错误，管线需立即停止流转。
     *
     * @param gst       TGst 对象。
     * @param err       错误类型等详细信息
     *                  err->domain 为错误大的类别
     *                      GST_CORE_ERROR
     *                      GST_LIBRARY_ERROR
     *                      GST_RESOURCE_ERROR
     *                      GST_STREAM_ERROR
     *                  err->code 错误的子分支
     *                      详细内容
     *
     * @param debug 字符串形式的错误信息，utf_8，根据系统语言设定，自动输出多语言
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_error)(TGst *gst, GError *err, gchar *debug);

    /**
     * 读取到媒体的标签信息
     *
     * @param gst       TGst 对象。
     * @param tags      标签信息
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_tags)(TGst *gst, GstTagList *tags);

    ret_t (*on_stream_collection)(TGst *gst, GstStreamCollection *coll);
    ret_t (*on_stream_selected)(TGst *gst, GstStreamCollection *coll);

    /**
     * 管线状态发生改变
     *
     * @GST_STATE_VOID_PENDING: 无待处理状态。
     * @GST_STATE_NULL        : 元素的 NULL 状态或初始状态。
     * @GST_STATE_READY       : 元素已准备好进入 PAUSED 状态。
     * @GST_STATE_PAUSED      : 元素处于 PAUSED 状态，已准备好接收和处理数据。
     *                          但 sink 元素仅接受一个缓冲区，之后会阻塞。
     * @GST_STATE_PLAYING     : 元素处于 PLAYING 状态，#GstClock 正在运行，
     *                          数据正在流转。
     * @param gst           TGst 对象。
     * @param old_state     旧状态（切换前的状态）
     * @param new_state     新状态（切换后的最终状态）
     * @param pending_state 待处理状态（正在切换的目标状态）
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_state_changed)(TGst *gst,
                              GstState old_state,
                              GstState new_state,
                              GstState pending_state);

    /**
     * 流状态发生改变
     *
     * typedef enum {
     *     GST_STREAM_STATUS_TYPE_CREATE    = 0,  // 流线程即将被创建
     *     GST_STREAM_STATUS_TYPE_ENTER     = 1,  // 流线程进入主循环
     *     GST_STREAM_STATUS_TYPE_LEAVE     = 2,  // 流线程离开主循环
     *     GST_STREAM_STATUS_TYPE_DESTROY   = 3,  // 流线程被销毁
     *     GST_STREAM_STATUS_TYPE_START     = 8,  // 流线程开始运行（播放）
     *     GST_STREAM_STATUS_TYPE_PAUSE     = 9,  // 流线程暂停
     *     GST_STREAM_STATUS_TYPE_STOP      = 10  // 流线程停止
     * } GstStreamStatusType;
     *
     * @param gst       TGst 对象。
     * @param type      流状态
     * @param owner     发生状态改变的元素
     *
     * @return  返回 RET_OK 表示成功，否则表示失败。
     */
    ret_t (*on_stream_status)(TGst *gst,
                              GstStreamStatusType type,
                              GstElement *owner);

    ret_t (*on_get_capture_bps)(TGst *gst);
} tgst_ops_t;

struct _TGstVideoFrame {
    int32_t fd;
    int32_t w;
    int32_t h;
    int32_t ws;
    int32_t hs;
    int32_t fmt;
    float ar;

    union {
        void *data;
        void *y_data;
    };
    void *uv_data;
};

struct _TGst {
    GMutex mutex;

    /** 是否在销毁过程中，为 1 时表示正在销毁 */
    gint is_destroying;

    /** 当前是否为播放器管线 */
    gboolean is_playback;

    /** 管线对象 */
    GstElement *pipeline;

    GstElement *audio_level;
    GstElement *audio_spectrum;

    GstElement *audio_src;
    GstElement *audio_sink;
    GstElement *preview_audio_convert;

    GstElement *video_src;
    GstElement *video_sink;

#ifdef WINDOWS
    GstElement *win_audio_resample;
    GstElement *win_audio_caps;
    GstElement *win_video_convert_scale;
    GstElement *win_video_caps;
#endif

    /** 虚函数表。*/
    const TGstVTable *vt;

    /* 以下为 TGst 私有属性 */

    gboolean is_prepared;

    /** 事件分发器 */
    emitter_t *emitter;

    /** 消息循环句柄 */
    guint bus_watch;

    /* 当前播放状态 */
    GstState current_state;
    /* 目标播放状态 */
    GstState target_state;

    /** 缺失插件的列表 */
    GList *missing;

    /** 流缓冲中标志 */
    gboolean buffering;

    /** 实时流标志 */
    gboolean is_live;

    gboolean initial_file;

    /** 管线支持的功能 */
    TGstFuncFlags funcs;

    /* 位置和总时长 */
    /** 轮询时钟句柄 */
    guint query_timer;
    /** 总时长，播放时为音视频时长，录制时为录制时长 */
    gint64 duration;
    /** 位置 */
    gint64 position;

    /** 视频帧回调句柄 */
    guint new_video_sample;

    gchar *filename;
};

//------------------------------------------------------------------------------
/**
 * 创建 TGst 对象。仅在子类对象构造函数中使用。
 *
 * @param vt        虚表。
 * @param funcs     功能标志。
 *
 * @return TGst 对象本身，不会返回无效值，出错时会断言抛出错误。
 */
TGst *gstCreate(const TGstVTable *vt, uint32_t funcs);

/**
 * 销毁 TGst 对象。
 *
 * @param gst       TGst 对象。
 */
void gstDestroy(TGst *gst);

/**
 * 获取GST对象名。
 *
 * @param gst       TGst 对象。
 *
 * @return 返回对象名。
 */
const gchar *gstGetType(TGst *gst);

/**
 * 管线开始。
 *
 * @param gst       TGst 对象。
 * @param filename  播放或录制的文件。
 *
 * @return 返回 RET_OK 表示成功，否则表示失败。
 */
ret_t gstStart(TGst *gst, const gchar *filename, ...);

/**
 * 管线停止。
 *
 * @param gst       TGst 对象。
 *
 * @return 返回 RET_OK 表示成功，否则表示失败。
 */
ret_t gstStop(TGst *gst);

/**
 * 管线暂停。
 *
 * @param gst       TGst 对象。
 * @param paused    是否暂停。
 *
 * @return 返回 RET_OK 表示成功，否则表示失败。
 */
ret_t gstSetPaused(TGst *gst, gboolean paused);

/**
 * 订阅/绑定事件的接口。
 *
 * @param gst       TGst 对象。
 * @param type      事件类型。
 * @param on_event  事件回调函数。
 * @param ctx       用户自定义数据指针。
 *
 * @return 返回事件接口 ID，为零时表示绑定失败。
 */
uint32_t gstOn(TGst *gst, uint32_t type, event_func_t on_event, void *ctx);

/**
 * 注销/解绑事件的接口。
 * @param gst        TGst 对象。
 * @param id         事件接口 ID。
 */
void gstOff(TGst *gst, uint32_t id);

void gstSetState(TGst *gst, GstState state);
GstState gstGetState(TGst *gst);

uint32_t gstGetCaptuareBps(TGst *gst);

/**
 * 通知播放或录制结束事件。
 */
void gstNotifyCompleted(TGst *gst);

/**
 * 播放或捕捉位置发生变化。
 *
 * @param gst       TGst 对象。
 * @param position  位置。
 */
void gstNotifyPositionChanged(TGst *gst, gint64 position);

/**
 * 播放或捕捉总时长发生变化。
 *
 * @param gst       TGst 对象。
 * @param duration  总时长。
 */
void gstNotifyDurationChanged(TGst *gst, gint64 duration);

void tgst_func_to_str(gchar *text_buf, uint32_t size, uint32_t funcs);

//------------------------------------------------------------------------------

TG_EXTERN_VTABLE(Gst);

#define TG_TYPE_NONE "Gst"
#define TGST(p) ((TGst *)(p))

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif /*T_GST_H*/
//==============================================================================
