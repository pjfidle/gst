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
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TGstCapture"
#include <log.h>
//==============================================================================

#include "TGstCapture.h"
#include <gst/gstinfo.h>
#include <gst/pbutils/pbutils.h>
#include <inttypes.h>

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_GST_CAPTURE

//------------------------------------------------------------------------------

typedef struct {
    TGstCapture *cap;
    GstElement *tee;
    const gchar *sink_name;
} DynamicLinkData;

//==============================================================================
/**
 * 流空闲探测回调，用于侦测录制结束时的数据流结尾。
 */
static GstPadProbeReturn
on_branch_idle_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    LeaveContext *ctx = (LeaveContext *)user_data;

    LOG_V("将当前 Pad 及其 Peer Pad 断开链接（防止更多数据流入）");
    gchar *pad_name = gst_pad_get_name(pad);
    GstPad *peer = gst_pad_get_peer(pad);
    if (peer) {
        if ((strcmp(pad_name, "audio_sink") == 0) ||
            (strcmp(pad_name, "video_sink") == 0)) {
            gst_pad_unlink(peer, pad);
            g_mutex_lock(&ctx->mutex);
            ctx->idle_count--;
            LOG_D("'%s' 断开链接！ idel_count=%d", pad_name, ctx->idle_count);
            g_cond_signal(&ctx->cond);
            g_mutex_unlock(&ctx->mutex);

        } else {
            LOG_W("衬垫 '%s' 名称异常！", pad_name);
        }
        gst_object_unref(peer);

    } else {
        LOG_W("衬垫 %s 获取 peer 异常！", pad_name);
    }
    g_free(pad_name);
    return GST_PAD_PROBE_REMOVE;
}

/**
 * 文件收尾回调
 */
static GstPadProbeReturn
on_filesink_eos_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    LeaveContext *ctx = (LeaveContext *)user_data;

    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_EOS) {
        g_mutex_lock(&ctx->mutex);
        ctx->eos = TRUE;
        LOG_D("FileSink 收到 EOS，文件封口完成！");
        g_cond_signal(&ctx->cond);
        g_mutex_unlock(&ctx->mutex);
        return GST_PAD_PROBE_REMOVE;
    }
    return GST_PAD_PROBE_OK;
}

/**
 * 缓冲区流转探针回调，用于侦测 PTS 及录制结束。
 */
static GstPadProbeReturn
on_capture_buffer_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    TGst *gst = TGST(user_data);
    TGstCapture *capture = TGST_CAPTURE(user_data);

    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    // 获取当前 Buffer 的 Running Time（纳秒）
    GstClockTime pts = GST_BUFFER_PTS(buffer);

    if (GST_CLOCK_TIME_IS_VALID(pts)) {
        if (capture->capture_position == GST_CLOCK_TIME_NONE) {
            capture->capture_position = pts;
        }

        gst->position = pts;
        gint64 elapsed_pts = gst->position - capture->capture_position;
        gstNotifyPositionChanged(gst, elapsed_pts);
#if 0
        LOG_V("elapsed_pts=%" PRId64 "duration=%" PRId64,
              elapsed_pts,
              gst->duration);
#endif

        // 判断录制时长
        if (gst->duration != GST_CLOCK_TIME_NONE &&
            elapsed_pts >= gst->duration) {
            LOG_V("达到预定时长（duration=%" PRId64 "），正在结束录制......",
                  gst->duration);
            gstNotifyCompleted(gst);
            return GST_PAD_PROBE_REMOVE; // 移除探针
        }
    }

    return GST_PAD_PROBE_OK;
}

/**
 * 音视频缓冲区流转探针回调，用于重置 PTS。
 */
static GstPadProbeReturn audio_pts_rewrite_probe(GstPad *pad,
                                                 GstPadProbeInfo *info,
                                                 gpointer user_data) {
    TGstCapture *cap = TGST_CAPTURE(user_data);

    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;

    // 检查并使 Buffer 变为当前分支独占可写。
    // 这能保证修改 PTS 时，绝对不会污染到预览分支的同一个 Buffer。
    buf = gst_buffer_make_writable(buf);
    if (!buf)
        return GST_PAD_PROBE_OK;

    // 必须把安全的、可能已经改变地址的新指针重新塞回 info 结构体中
    info->data = (gpointer)buf;

    // 获取当前原始时间戳
    GstClockTime raw_pts = GST_BUFFER_PTS(buf);

    // 音视频共享一模一样的物理剪刀，咔嚓剪掉相同的时间基准
    GstClockTime corrected_pts = 0;
    GstClockTime drift = cap->base_anchor_time + cap->total_gap_accumulated;

    if (raw_pts > drift) {
        corrected_pts = raw_pts - drift;
    } else {
        corrected_pts = 0; // 极小边界容错
    }

    // 安全地刷入录制分支独享的时间戳
    GST_BUFFER_PTS(buf) = corrected_pts;
    GST_BUFFER_DTS(buf) = corrected_pts;

    if (cap->audio_only) {
        if (corrected_pts >= cap->last_notification_pts + (100 * GST_MSECOND)) {
            cap->last_notification_pts = corrected_pts;
            TGst *gst = TGST(user_data);
            gstNotifyPositionChanged(gst, cap->last_notification_pts);

            // 判断录制时长
            if (cap->last_notification_pts >= gst->duration) {
                LOG_V("达到预定时长（duration=%" PRId64
                      "），正在结束录制......",
                      gst->duration);
                gstNotifyCompleted(gst);
                return GST_PAD_PROBE_REMOVE; // 移除探针
            }
        }
    }
    return GST_PAD_PROBE_OK;
}

/**
 * 视频缓冲区流转探针回调，用于重置 PTS 及通知位置变化和判断录制结束。
 */
static GstPadProbeReturn video_pts_rewrite_probe(GstPad *pad,
                                                 GstPadProbeInfo *info,
                                                 gpointer user_data) {
    TGstCapture *cap = TGST_CAPTURE(user_data);

    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;

    // 检查并使 Buffer 变为当前分支独占可写。
    // 这能保证修改 PTS 时，绝对不会污染到预览分支的同一个 Buffer。
    buf = gst_buffer_make_writable(buf);
    if (!buf)
        return GST_PAD_PROBE_OK;

    // 必须把安全的、可能已经改变地址的新指针重新塞回 info 结构体中
    info->data = (gpointer)buf;

    // 获取当前原始时间戳
    GstClockTime raw_pts = GST_BUFFER_PTS(buf);

    // 音视频共享一模一样的物理剪刀，咔嚓剪掉相同的时间基准
    GstClockTime corrected_pts = 0;
    GstClockTime drift = cap->base_anchor_time + cap->total_gap_accumulated;

    if (raw_pts > drift) {
        corrected_pts = raw_pts - drift;
    } else {
        corrected_pts = 0; // 极小边界容错
    }

    // 安全地刷入录制分支独享的时间戳
    GST_BUFFER_PTS(buf) = corrected_pts;
    GST_BUFFER_DTS(buf) = corrected_pts;

    if (corrected_pts >= cap->last_notification_pts + (100 * GST_MSECOND)) {
        cap->last_notification_pts = corrected_pts;
        TGst *gst = TGST(user_data);
        gstNotifyPositionChanged(gst, cap->last_notification_pts);

        // 判断录制时长
        if (cap->last_notification_pts >= gst->duration) {
            LOG_V("达到预定时长（duration=%" PRId64 "），正在结束录制......",
                  gst->duration);
            gstNotifyCompleted(gst);
            return GST_PAD_PROBE_REMOVE; // 移除探针
        }
    }
    return GST_PAD_PROBE_OK;
}

/**
 * 创建录制结束上下文
 * 
 * @param gst           GST 对象。
 * @param idle_count    等待的次数。
 */
LeaveContext *create_leave_context(TGst *gst, gint idle_count) {
    LeaveContext *ctx = g_malloc0(sizeof(LeaveContext));
    // 初始化同步上下文
    g_mutex_init(&ctx->mutex);
    g_cond_init(&ctx->cond);
    ctx->idle_count = idle_count;
    ctx->eos = FALSE;
    ctx->self = gst;
    return ctx;
}

/**
 * 等待录制结束
 * 
 * @param gst           GST 对象。
 * @param ctx           录制结束上下文。
 */
void wait_leave_context(TGst *gst, LeaveContext *ctx) {
    g_mutex_lock(&ctx->mutex);
    while ((ctx->idle_count > 0) || !ctx->eos) {
        // 设置 10 秒超时，防止死锁
        if (!g_cond_wait_until(&ctx->cond,
                               &ctx->mutex,
                               g_get_monotonic_time() +
                                   10 * G_TIME_SPAN_SECOND)) {
            LOG_E("停止录制超时！强制进入清理程序。");
            break;
        }
    }
    g_mutex_unlock(&ctx->mutex);

    // 清理锁资源
    g_mutex_clear(&ctx->mutex);
    g_cond_clear(&ctx->cond);

    g_free(ctx);
}

/**
 * 等待录制结束线程
 */
static gpointer leaving_thread_function(gpointer data) {
    TGstCapture *cap = TGST_CAPTURE(data);

    tgst_send_event(cap->capture_bin, gst_event_new_eos());

    LOG_V("等待文件封口...");
    wait_leave_context(cap, cap->leave_ctx);

    cap->audio_valve = NULL;
    cap->audio_queue2 = NULL;
    if (!cap->audio_only) {
        cap->video_valve = NULL;
        cap->video_queue2 = NULL;
    }
    cap->file_sink = NULL;
    return NULL;
}

/**
 * 增加结束探针
 */
static void add_eos_probe(TGst *gst,
                          LeaveContext *ctx,
                          GstElement *element,
                          const gchar *sink_name) {
    LOG_V("对象 'Gst:%s'：增加停止探针 (gst=%p, ctx=%p, element=%p, "
          "sink_name=%s) ......",
          TOBJ_NAME,
          gst,
          ctx,
          element,
          sink_name);

    GstPad *pad = tgst_get_static_pad(element, sink_name);
    if (pad == NULL) {
        LOG_W("对象 'Gst:%s'：增加停止探针失败！", TOBJ_NAME);
        return;
    }
    gulong id = gst_pad_add_probe(pad,
                                  GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                                  (GstPadProbeCallback)on_filesink_eos_cb,
                                  ctx,
                                  NULL);
    gst_object_unref(pad);
    if (id == 0) {
        LOG_W("对象 'Gst:%s'：增加停止探针失败！", TOBJ_NAME);
    } else {
        LOG_V("对象 'Gst:%s'：增加停止探针成功。", TOBJ_NAME);
    }
}

/**
 * 增加空闲探针
 */
static void add_idle_probe(TGst *gst,
                           LeaveContext *ctx,
                           GstElement *element,
                           const gchar *sink_name) {
    LOG_V("对象 'Gst:%s'：增加空闲探针 (gst=%p, ctx=%p, element=%p, "
          "sink_name=%s) ......",
          TOBJ_NAME,
          gst,
          ctx,
          element,
          sink_name);

    GstPad *pad = tgst_get_static_pad(element, sink_name);
    if (pad == NULL) {
        LOG_W("对象 'Gst:%s'：增加空闲探针失败！", TOBJ_NAME);
        return;
    }
    gulong id = gst_pad_add_probe(pad,
                                  GST_PAD_PROBE_TYPE_IDLE,
                                  (GstPadProbeCallback)on_branch_idle_cb,
                                  ctx,
                                  NULL);
    gst_object_unref(pad);
    if (id == 0) {
        LOG_W("对象 'Gst:%s'：增加空闲探针失败！", TOBJ_NAME);
    } else {
        LOG_V("对象 'Gst:%s'：增加空闲探针成功。", TOBJ_NAME);
    }
}

//------------------------------------------------------------------------------

static void capture_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("对象 'Gst:%s'：创建开始 (gst=%p, funcs=%s) ......",
          TOBJ_NAME,
          gst,
          text_buf);

    TGstCapture *cap = TGST_CAPTURE(gst);

    LOG_V("音频管线：初始化开始......");
    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SRC)) {
        tgst_element_factory_make(&cap->audio_caps,
                                  GST_PLUGINS_CAPS_FILTER,
                                  "audio_caps");

        tgst_element_factory_make(&cap->audio_tee,
                                  GST_PLUGINS_TEE,
                                  "audio_tee");

        tgst_element_factory_make(&cap->audio_queue1,
                                  GST_PLUGINS_QUEUE,
                                  "audio_queue1");
    } else {
        cap->audio_caps = NULL;
        cap->audio_tee = NULL;
        cap->audio_queue1 = NULL;
    }
    cap->audio_valve = NULL;
    cap->audio_queue2 = NULL;

    LOG_V("视频管线：初始化开始......");
    if (TG_IS_FUNC(gst, TG_FUNC_VIDEO_SRC)) {
        tgst_element_factory_make(&cap->video_caps,
                                  GST_PLUGINS_CAPS_FILTER,
                                  "video_caps");

        tgst_element_factory_make(&cap->video_in,
                                  GST_PLUGINS_VIDEO_IN,
                                  "video_in");

        tgst_element_factory_make(&cap->video_tee,
                                  GST_PLUGINS_TEE,
                                  "video_tee");

        tgst_element_factory_make(&cap->video_queue1,
                                  GST_PLUGINS_QUEUE,
                                  "video_queue1");
    } else {
        cap->video_caps = NULL;
        cap->video_in = NULL;
        cap->video_tee = NULL;
        cap->video_queue1 = NULL;
    }
    cap->video_valve = NULL;
    cap->video_queue2 = NULL;

    cap->capture_bin = NULL;
    cap->file_sink = NULL;

    cap->is_stopping = FALSE;

    cap->capture_position = GST_CLOCK_TIME_NONE;

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void capture_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("'%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    LOG_D("'%s'：销毁成功。", TOBJ_NAME);
}

/**
 * 捕捉开始回调
 */
static ret_t capture_on_start(TGst *gst) {
    LOG_V("'%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    TGstCapture *cap = TGST_CAPTURE(gst);
    cap->capture_position = GST_CLOCK_TIME_NONE;

    g_mutex_init(&cap->capturing_mutex);

    if (TG_IS_FUNC(gst, TG_FUNC_AUDIO_SRC)) {
        tgst_element_factory_make(&cap->audio_valve,
                                  GST_PLUGINS_VALVE,
                                  "audio_valve");
        tgst_element_factory_make(&cap->audio_queue2,
                                  GST_PLUGINS_QUEUE,
                                  "audio_queue2");
    }

    if (TG_IS_FUNC(gst, TG_FUNC_VIDEO_SRC)) {
        tgst_element_factory_make(&cap->video_valve,
                                  GST_PLUGINS_VALVE,
                                  "video_valve");
        tgst_element_factory_make(&cap->video_queue2,
                                  GST_PLUGINS_QUEUE,
                                  "video_queue2");
    }

    tgst_bin_new(&cap->capture_bin, "capture_bin");
    tgst_element_factory_make(&cap->file_sink,
                              GST_PLUGINS_FILE_SINK,
                              "file_sink");

    LOG_D("'%s'：启动成功。", TOBJ_NAME);
    return RET_OK;
}

/**
 * 捕捉停止回调
 */
static ret_t capture_on_stop(TGst *gst) {
    LOG_V("'%s'：停止开始 (gst=%p) ......", TOBJ_NAME, gst);

    TGstCapture *cap = TGST_CAPTURE(gst);

    if (cap->is_stopping) {
        LOG_W("'%s'：已经在停止/封口流程中，请勿重复调用。", TOBJ_NAME);
        return RET_BUSY;
    }
    cap->is_stopping = TRUE; // 标记开始停止

    g_mutex_clear(&cap->capturing_mutex);

    LOG_V("初始化同步上下文");
    cap->leave_ctx = create_leave_context(gst, cap->audio_only ? 1 : 2);

    LOG_V("增加文件封口探针");
    add_eos_probe(gst, cap->leave_ctx, cap->file_sink, "sink");

    LOG_V("增加音频流空闲探针");
    add_idle_probe(gst, cap->leave_ctx, cap->capture_bin, "audio_sink");

    if (!cap->audio_only) {
        LOG_V("增加视频流空闲探针");
        add_idle_probe(gst, cap->leave_ctx, cap->capture_bin, "video_sink");
    }

    GThread *thread =
        g_thread_new("leaving_thread", leaving_thread_function, gst);

    g_thread_join(thread);

    // 封口完成，重置状态（或者在销毁阶段处理）
    cap->is_stopping = FALSE;

    cap->audio_valve = NULL;
    cap->audio_queue2 = NULL;
    cap->video_valve = NULL;
    cap->video_queue2 = NULL;

    LOG_D("'%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t capture_on_paused(TGst *gst, gboolean paused) {
    LOG_V("'%s'：暂停开始 (gst=%p, paused=%s) ......",
          TOBJ_NAME,
          gst,
          t_bool2str(paused));

    TGstCapture *cap = TGST_CAPTURE(gst);

    g_mutex_lock(&cap->capturing_mutex);

    g_object_set(G_OBJECT(cap->audio_valve), "drop", paused, NULL);
    if (!cap->audio_only) {
        g_object_set(G_OBJECT(cap->video_valve), "drop", paused, NULL);
    }

    if (paused) {
        cap->last_pause_time =
            gst_element_get_current_running_time(GST_ELEMENT(gst->pipeline));
    } else {
        if (cap->last_pause_time > 0) {
            GstClockTime now = gst_element_get_current_running_time(
                GST_ELEMENT(gst->pipeline));
            if (now > cap->last_pause_time) {
                // 物理世界中停顿了多少纳秒，追加到全局共享的 Gap 中
                cap->total_gap_accumulated += (now - cap->last_pause_time);
            }
        }
    }

    g_mutex_unlock(&cap->capturing_mutex);

    LOG_D("'%s'：暂停成功。", TOBJ_NAME);
    return RET_OK;
}

TG_DECL_VTABLE(GstCapture) = {
    .size = sizeof(TGstCapture),
    .type = TG_TYPE_GST_CAPTURE,
    .get_parent_vt = TG_GET_PARENT_VTABLE(Gst),

    TG_CALLBACKS_REQUIRED(capture),
    .on_paused = capture_on_paused,
};

//------------------------------------------------------------------------------

gboolean gstCaptureStart(TGst *gst) {
    TGstCapture *cap = TGST_CAPTURE(gst);

    // clang-format off
    g_object_set(G_OBJECT(cap->file_sink),
                 "location", gst->filename,
                 "buffer-size", 4194304, // 录制缓冲区
                 "async", TRUE, // 允许异步状态转换，不阻塞预览
                 "sync", FALSE, // 录制分支不需要同步显示时钟
                 NULL);
    // clang-format on

    TG_PIPELINE_ADD(gst, cap->capture_bin);

    //tgst_add_buffer_probe(gst, buff_element, "src", on_capture_buffer_cb);

    cap->base_anchor_time = gst_element_get_current_running_time(gst->pipeline);
    cap->last_notification_pts = 0;
    cap->total_gap_accumulated = 0;

    tgst_add_buffer_probe(gst,
                          cap->audio_queue2,
                          "src",
                          audio_pts_rewrite_probe);
    if (!cap->audio_only) {
        tgst_add_buffer_probe(gst,
                              cap->video_queue2,
                              "src",
                              video_pts_rewrite_probe);
    }

    // 在添加到父对象之后，为捕捉容器同步状态
    if (!gst_element_sync_state_with_parent(cap->capture_bin)) {
        LOG_W("'%s' 和父对象同步状态失败", TOBJ_NAME);
        return FALSE;
    }

    if (cap->audio_tee != NULL && !tgst_request_pad_link(cap->audio_tee,
                                                         "src_%u",
                                                         cap->capture_bin,
                                                         "audio_sink")) {
        LOG_W("'%s' 增加 'audio_sink' 衬垫失败！", TOBJ_NAME);
        return FALSE;
    }

    if (cap->video_tee != NULL && !tgst_request_pad_link(cap->video_tee,
                                                         "src_%u",
                                                         cap->capture_bin,
                                                         "video_sink")) {
        LOG_W("'%s' 增加 'video_sink' 衬垫失败！", TOBJ_NAME);
        return FALSE;
    }

    gstSetState(gst, GST_STATE_PLAYING);
    return TRUE;
}

//==============================================================================
