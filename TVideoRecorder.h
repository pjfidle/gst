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
//==============================================================================
#ifndef T_GST_VIDEO_RECORDER_H
#define T_GST_VIDEO_RECORDER_H
//------------------------------------------------------------------------------

#include "TGstCapture.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

/** 视频输入源 X 宏 */
#define INPUT_SOURCE_LIST                                                      \
    X(INPUT_SOURCE_VIDEO_AUTO, "video_auto")                                   \
    X(INPUT_SOURCE_VIDEO_CVBS, "video_cvbs")                                   \
    X(INPUT_SOURCE_VIDEO_S_VIDEO, "video_s_video")                             \
    X(INPUT_SOURCE_AUDIO_AUX, "audio_aux")
/** 视频输入源枚举声明 */
typedef enum {
    INPUT_SOURCE_UNKNOWN = -1,
#define X(key, value) key,
    INPUT_SOURCE_LIST
#undef X
        INPUT_SOURCE_COUNT
} input_source_t;
DECL_KEY_NAME_GET_ALL(input_source)

/** 视频输入制式 X 宏 */
#define VIDEO_IN_STANDARD_LIST                                                 \
    X(VIDEO_IN_STANDARD_NTSC_358, "ntsc_358")                                  \
    X(VIDEO_IN_STANDARD_NTSC_443, "ntsc_443")                                  \
    X(VIDEO_IN_STANDARD_PAL, "pal")                                            \
    X(VIDEO_IN_STANDARD_PAL_M, "pal_m")                                        \
    X(VIDEO_IN_STANDARD_PAL_NC, "pal_nc")                                      \
    X(VIDEO_IN_STANDARD_SECAM, "secam")                                        \
    X(VIDEO_IN_STANDARD_PAL_60, "pal_60")                                      \
    X(VIDEO_IN_STANDARD_UNKNOWN, "unknown")
/** 视频输入制式枚举声明 */
typedef enum {
#define X(key, value) key,
    VIDEO_IN_STANDARD_LIST
#undef X
        VIDEO_IN_STANDARD_COUNT
} video_in_standard_t;
DECL_KEY_NAME_GET_ALL(video_in_standard)

/** 视频录制品质 X 宏 */
#define VIDEO_RECORD_QUALITY_LIST                                              \
    X(VIDEO_RECORD_QUALITY_LOW, "low")                                         \
    X(VIDEO_RECORD_QUALITY_MEDIUM, "medium")                                   \
    X(VIDEO_RECORD_QUALITY_HIGH, "high")
/** 视频录制品质枚举声明 */
typedef enum {
#define X(key, value) key,
    VIDEO_RECORD_QUALITY_LIST
#undef X
        VIDEO_RECORD_QUALITY_COUNT
} video_record_quality_t;
DECL_KEY_NAME_GET_ALL(video_record_quality)

/** 视频录制格式 X 宏 */
#define VIDEO_RECORD_FORMAT_LIST                                               \
    X(VIDEO_RECORD_FORMAT_MP4, "mp4")                                          \
    X(VIDEO_RECORD_FORMAT_MKV, "mkv")
/** 视频录制格式枚举声明 */
typedef enum {
#define X(key, value) key,
    VIDEO_RECORD_FORMAT_LIST
#undef X
        VIDEO_RECORD_FORMAT_COUNT
} video_record_format_t;
DECL_KEY_NAME_GET_ALL(video_record_format)

/** 视频录制时间戳 X 宏 */
#define VIDEO_RECORD_TIMESTAMP_LIST                                            \
    X(VIDEO_RECORD_TIMESTAMP_OFF, "off")                                       \
    X(VIDEO_RECORD_TIMESTAMP_LEFT_TOP, "left_top")                             \
    X(VIDEO_RECORD_TIMESTAMP_LEFT_BOTTOM, "left_bottom")                       \
    X(VIDEO_RECORD_TIMESTAMP_RIGHT_TOP, "right_top")                           \
    X(VIDEO_RECORD_TIMESTAMP_RIGHT_BOTTOM, "right_bottom")
/** 视频录制时间戳枚举声明 */
typedef enum {
#define X(key, value) key,
    VIDEO_RECORD_TIMESTAMP_LIST
#undef X
        VIDEO_RECORD_TIMESTAMP_COUNT
} video_record_timestamp_t;
DECL_KEY_NAME_GET_ALL(video_record_timestamp)

#undef X

enum {
    TG_EVENT_VIDEO_IN_SOURCE_CHANGED = TG_EVENT_USER_START,
    TG_EVENT_VIDEO_IN_STANDARD_CHANGED,
};

typedef struct {
    /** 必须是第一个成员，继承 TEvent 标准事件头 */
    event_t e;

    uint32_t source;
} video_in_source_changed_t;

typedef struct {
    /** 必须是第一个成员，继承 TEvent 标准事件头 */
    event_t e;

    uint32_t standard;
} video_in_standard_changed_t;

struct _TVideoRecorder {
    TGstCapture capture;

    /** 视频输入源 */
    uint32_t vin_source;
    /** 视频输入制式 */
    uint32_t vin_standard;

    uint32_t quality;
    uint32_t format;

    GstElement *audio_enc;
    GstElement *audio_parse;

    GstElement *video_enc;
    GstElement *video_parse;

#ifdef WINDOWS
    GstElement *audio_convert;
    GstElement *audio_resample;
    GstElement *audio_caps2;
    GstElement *video_convert;
    GstElement *video_caps2;
#endif

    GstElement *record_mux;

    LeaveContext *leave_ctx;
};

//------------------------------------------------------------------------------
/**
 * 创建 GST 视频录像机对象。
 *
 * @param quality   录制品质。
 * @param format    录制格式。
 *
 * @return TGstCapture 对象实例。
 */
TGst *videoRecorderCreate(uint32_t quality, uint32_t format);

//------------------------------------------------------------------------------

#define TG_TYPE_VIDEO_RECORDER "VideoRecorder"
#define TVIDEO_RECORDER(p) ((TVideoRecorder *)p)

TG_EXTERN_VTABLE(VideoRecorder);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_VIDEO_RECORDER_H
//==============================================================================
