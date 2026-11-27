/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 捕捉
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_CAPTURE_H
#define T_GST_CAPTURE_H
//------------------------------------------------------------------------------

#include "TGst.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

//------------------------------------------------------------------------------

typedef struct {
    GMutex mutex;
    GCond cond;
    gint idle_count;
    gboolean eos;
    TGstCapture *self;
} LeaveContext;

struct _TGstCapture {
    TGst gst;

    gboolean audio_only;
    gboolean is_stopping;

    GMutex capturing_mutex;
    GstClockTime base_anchor_time;      // 全局唯一的实时时钟锚点（音视频共用）
    GstClockTime last_pause_time;       // 暂停起始点
    GstClockTime total_gap_accumulated; // 累积的暂停时间
    GstClockTime last_notification_pts; // 最后上报时间
    /** 
     * 捕捉视频计数
     */
    guint64 video_frame_count;
    /**
     * 捕捉开始的时间点
     */
    gint64 capture_position;

    GstElement *audio_caps;
    GstElement *audio_tee;
    GstElement *audio_queue1;
    GstElement *audio_valve;
    GstElement *audio_queue2;

    GstElement *video_caps;
    GstElement *video_in;
    GstElement *video_tee;
    GstElement *video_queue1;
    GstElement *video_valve;
    GstElement *video_queue2;

    GstElement *capture_bin;
    GstElement *file_sink;

    LeaveContext *leave_ctx;
};

//------------------------------------------------------------------------------
/**
 * 开始进行录制。
 *
 * @param gst           已分配内存的捕捉对象，不能为空指针。
 *
 * @return 返回 TRUE 表示成功，否则表示失败。
 */
gboolean gstCaptureStart(TGst *gst);

//------------------------------------------------------------------------------

#define TG_AUDIO_CAPS(p) (TGST_CAPTURE(p)->audio_caps)
#define TG_AUDIO_TEE(p) (TGST_CAPTURE(p)->audio_tee)
#define TG_AUDIO_QUEUE1(p) (TGST_CAPTURE(p)->audio_queue1)
#define TG_AUDIO_VALVE(p) (TGST_CAPTURE(p)->audio_valve)
#define TG_AUDIO_QUEUE2(p) (TGST_CAPTURE(p)->audio_queue2)

#define TG_VIDEO_CAPS(p) (TGST_CAPTURE(p)->video_caps)
#define TG_VIDEO_IN(p) (TGST_CAPTURE(p)->video_in)
#define TG_VIDEO_TEE(p) (TGST_CAPTURE(p)->video_tee)
#define TG_VIDEO_QUEUE1(p) (TGST_CAPTURE(p)->video_queue1)
#define TG_VIDEO_VALVE(p) (TGST_CAPTURE(p)->video_valve)
#define TG_VIDEO_QUEUE2(p) (TGST_CAPTURE(p)->video_queue2)

#define TG_CAPTURE_BIN(p) (TGST_CAPTURE(p)->capture_bin)
#define TG_FILE_SINK(p) (TGST_CAPTURE(p)->file_sink)

#define TG_AUDIO_CAPS_SET(gst, ...)                                            \
    g_object_set(G_OBJECT(TGST_CAPTURE(gst)->audio_caps), ##__VA_ARGS__)
#define TG_VIDEO_CAPS_SET(gst, ...)                                            \
    g_object_set(G_OBJECT(TGST_CAPTURE(gst)->video_caps), ##__VA_ARGS__)
#define TG_VIDEO_IN_GET(gst, ...)                                              \
    g_object_get(G_OBJECT(TGST_CAPTURE(gst)->video_in), ##__VA_ARGS__)
#define TG_VIDEO_IN_SET(gst, ...)                                              \
    g_object_set(G_OBJECT(TGST_CAPTURE(gst)->video_in), ##__VA_ARGS__)

#define TG_CAPTURE_ADD(p, ...)                                                 \
    gst_bin_add_many(GST_BIN(TGST_CAPTURE(p)->capture_bin), ##__VA_ARGS__)

//------------------------------------------------------------------------------

#define TG_TYPE_GST_CAPTURE "GstCapture"
#define TGST_CAPTURE(p) ((TGstCapture *)(p))

TG_EXTERN_VTABLE(GstCapture);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_CAPTURE_H
//==============================================================================
