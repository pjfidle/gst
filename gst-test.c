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
#include <gst/video/video.h>
#include <rga/im2d.hpp>

static void g_callback_error(gpointer userdata, GError *err, gchar *debug)
{

}

static void g_callback_warning(gpointer userdata, GError *err, gchar *debug)
{

}

static void g_callback_tags(gpointer userdata, GstTagList *tags)
{

}

static void g_callback_eos(gpointer userdata)
{

}

static void g_callback_state_changed(gpointer userdata, GstState old_state, GstState new_state, GstState pending_state)
{

}

static void g_callback_stream_status(gpointer userdata, GstStreamStatusType type, GstElement *element)
{

}

static void g_callback_duration_changed(gpointer userdata, gint64 duration)
{


}

static void g_callback_position_changed(gpointer userdata, gint64 position)
{


}

static void g_callback_video_sample(gpointer userdata, GstSample *sample)
{
    /* 从样本中获取缓冲区 */
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        printf("无法获取缓冲区\n");
        return;
    }

#if 1
    printf("缓冲区 PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n",
              GST_TIME_ARGS(GST_BUFFER_PTS(buffer)),
              GST_TIME_ARGS(GST_BUFFER_DTS(buffer)));
#endif

    /* 映射视频帧并获取视频帧信息 */
    GstVideoInfo info;
    GstVideoFrame frame;
    auto caps = gst_sample_get_caps(sample);
    gst_video_info_from_caps(&info, caps);
    if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
        printf("视频帧映射失败\n");
        return;
    }

    /* 源视频帧宽度 */
    gint src_w = GST_VIDEO_INFO_WIDTH(&info);
    /* 源视频帧高度 */
    gint src_h = GST_VIDEO_INFO_HEIGHT(&info);
    /* 源视频帧行跨度 */
    gint src_ws = src_w;
    /* 源视频帧列跨度 */
    gint src_hs = src_h;
    /* 源视频帧宽高比 */
    gfloat src_ar = (float)(src_w * GST_VIDEO_INFO_PAR_N(&info)) 
            / (float)(src_h * GST_VIDEO_INFO_PAR_D(&info));

    /* 源视频帧数据 */
    void *src_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);
    /* 源视频帧行跨度 */
    guint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);

    /* 源视频帧像素格式 */
    gint src_fmt = RK_FORMAT_UNKNOWN;

    switch (info.finfo->format) {
        case GST_VIDEO_FORMAT_NV12: {
            /* 已测试 */
            void *uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
            src_ws = stride;
            src_hs = ((gint64)uv_data - (gint64)src_data) / stride;
            src_fmt = RK_FORMAT_YCbCr_420_SP;
            break;
        }
        case GST_VIDEO_FORMAT_NV21: {
            /* 待测试 */
            src_fmt = RK_FORMAT_YCrCb_420_SP;
            break;
        }
        case GST_VIDEO_FORMAT_I420: {
            /* 已测试 */
            void *uv_data = GST_VIDEO_FRAME_PLANE_DATA(&frame, 1);
            src_ws = stride;
            src_hs = ((gint64)uv_data - (gint64)src_data) / stride;
            src_fmt = RK_FORMAT_YCbCr_420_P;
            break;
        }
        case GST_VIDEO_FORMAT_RGB: {
            /* 已测试 */
            src_ws = stride / 3;
            src_hs = src_h;
            src_fmt = RK_FORMAT_RGB_888;
            break;
        }
        case GST_VIDEO_FORMAT_RGBA: {
            /* 已测试 */
            src_ws = stride / 4;
            src_hs = src_h;
            src_fmt = RK_FORMAT_RGBA_8888;
            break;
        }
        case GST_VIDEO_FORMAT_ARGB: {
            /* 已测试 */
            src_ws = stride / 4;
            src_hs = src_h;
            src_fmt = RK_FORMAT_RGBA_8888;
            break;
        }
        case GST_VIDEO_FORMAT_BGRA: {
            /* 已测试 */
            src_ws = stride / 4;
            src_hs = src_h;
            src_fmt = RK_FORMAT_RGBA_8888;
            break;
        }
        default: {
            printf("不支持的 GST 视频帧格式：%s\n", gst_video_format_to_string(info.finfo->format));
            break;
        }
    }

#if 1
    printf("src: %dx%d, ws=%d, hs=%d, data=%p\n", src_w, src_h, src_ws, src_hs, src_data);
#endif

    if (src_fmt == RK_FORMAT_UNKNOWN || src_w <= 0 || src_h <= 0 || src_ws <= 0 || src_hs <= 0) {
        printf("视频帧参数非法: src %dx%d, fmt=%d, stride=%d", src_w, src_h, src_fmt, src_ws);
        goto video_frame_unmap;
    }

    /* 内存边界校验：确保映射后的虚拟地址有效 */
    if (src_data == NULL) {
        printf("视频帧源数据指针为空\n");
        goto video_frame_unmap;
    }

    /**
     * 此处进行视频帧渲染
     */

video_frame_unmap:
    gst_video_frame_unmap(&frame);
}

static GMediaContext ctx = {
    .error            = g_callback_error,
    .warning          = g_callback_warning,
    .tags             = g_callback_tags,
    .eos              = g_callback_eos,
    .state_changed    = g_callback_state_changed,
    .stream_status    = g_callback_stream_status,
    .duration_changed = g_callback_duration_changed,
    .position_changed = g_callback_position_changed,

#if 0
    .use_kmssink      = TRUE,
    .plane_id         = 57,
#else
    .use_kmssink      = FALSE,
    .video_sample     = g_callback_video_sample,
#endif

    .userdata = NULL,
};

int main (int argc, char *argv[])
{
    int ret = 0;
    const char *filename = "/userdata/video01.mp4";
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);;

    printf("gst_media_init\n");

    memset(&ctx, 0, sizeof(GMediaContext));

    if (gst_media_init(&ctx, GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO, 1.0f, 0, FALSE, FALSE) != RESULT_SUCCESS) {
        printf("Fail\n");
        return -1;
    }

    printf("gst_media_play: %s\n", filename);

    gst_media_play(&ctx, filename);

    printf("g_main_loop_run\n");

    g_main_loop_run (loop);

    printf("Done\n");
    gst_media_deinit(&ctx);
    return ret;
}
