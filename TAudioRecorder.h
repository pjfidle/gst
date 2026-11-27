/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 录音机
 */

/**
 * History:
 * ================================================================
 * 2026-09-14 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_AUDIO_RECORDER_H
#define T_GST_AUDIO_RECORDER_H
//------------------------------------------------------------------------------

#include "TGstCapture.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

//------------------------------------------------------------------------------

/** 音频录制品质 X 宏 */
#define AUDIO_RECORD_QUALITY_LIST                                              \
    X(AUDIO_RECORD_QUALITY_LOW, "low")                                         \
    X(AUDIO_RECORD_QUALITY_MEDIUM, "medium")                                   \
    X(AUDIO_RECORD_QUALITY_HIGH, "high")
/** 音频录制品质枚举声明 */
typedef enum {
#define X(key, value) key,
    AUDIO_RECORD_QUALITY_LIST
#undef X
        AUDIO_RECORD_QUALITY_COUNT
} audio_record_quality_t;
DECL_KEY_NAME_GET_ALL(audio_record_quality)

/** 音频录制格式 X 宏 */
#define AUDIO_RECORD_FORMAT_LIST                                               \
    X(AUDIO_RECORD_FORMAT_WAV, "wav")                                          \
    X(AUDIO_RECORD_FORMAT_MP3, "mp3")
/** 音频录制格式枚举声明 */
typedef enum {
#define X(key, value) key,
    AUDIO_RECORD_FORMAT_LIST
#undef X
        AUDIO_RECORD_FORMAT_COUNT
} audio_record_format_t;
DECL_KEY_NAME_GET_ALL(audio_record_format)

struct _TAudioRecorder {
    TGstCapture capture;

    uint32_t quality;
    uint32_t format;

#ifdef WINDOWS
    GstElement *audio_convert;
    GstElement *audio_resample;
    GstElement *audio_caps2;
#endif

    GstElement *audio_enc;
};

//------------------------------------------------------------------------------
/**
 * 创建 GST 录音机对象。
 *
 * @param quality   录制品质。
 * @param format    录制格式。
 *
 * @return 录音机对象本身，不会返回无效值，出错时会断言抛出错误。
 */
TGst *audioRecorderCreate(int quality, int format);

//------------------------------------------------------------------------------

#define TG_TYPE_AUDIO_RECORDER "AudioRecorder"
#define TAUDIO_RECORDER(p) ((TAudioRecorder *)p)

TG_EXTERN_VTABLE(AudioRecorder);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_AUDIO_RECORDER_H
//==============================================================================
