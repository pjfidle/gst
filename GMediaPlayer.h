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
 * 
 * 注意：播放框架将保证线程安全，可以创建多个实例
 */

#ifndef __GST_PLAYBACK_H__
#define __GST_PLAYBACK_H__

#include <stdbool.h>
#include <gst/gst.h>
#include <gst/playback/gstplay-enum.h>

// 彻底替换原来的宏定义，直接用 GCC 的强行公开属性
#define GST_PLAYBACK_API __attribute__((visibility("default")))

typedef struct
{
    /**
     * 严重错误，需立即停止播放
     * @param err       错误类型等详细信息
     * @param debug     字符串形式的错误信息，utf_8，多语言
     * @param userdata  用户自定义数据指针
     */
    void (*error)(gpointer userdata, GError *err, gchar *debug);

    /**
     * 警告错误，可以继续播放
     * @param err       错误类型等详细信息
     * @param debug     字符串形式的错误信息，utf_8，多语言
     * @param userdata  用户自定义数据指针
     */
    void (*warning)(gpointer userdata, GError *err, gchar *debug);

    /**
     * 读取到媒体的标签信息
     * @param tags      标签信息
     * @param userdata  用户自定义数据指针
     */
    void (*tags)(gpointer userdata, GstTagList *tags);

    /**
     * 媒体播放结束
     * @param userdata 用户自定义数据指针
     */
    void (*eos)(gpointer userdata);

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
     * @param old_state     旧状态（切换前的状态）     
     * @param new_state     新状态（切换后的最终状态）
     * @param pending_state 待处理状态（正在切换的目标状态）
     * @param userdata      用户自定义数据指针
     */
    void (*state_changed)(gpointer userdata, GstState old_state, GstState new_state, GstState pending_state);

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
     * @param type      流状态
     * @param element   发生状态改变的元素
     * @param userdata  用户自定义数据指针
     */
    void (*stream_status)(gpointer userdata, GstStreamStatusType type, GstElement *element);

    /**
     * 媒体长度发生变化，
     * @param duration  正在播放的媒体长度，单位纳秒
     *                  1 秒 = 1,000 ms (毫秒) = 1,000,000 μs (微秒) = 1,000,000,000 ns (纳秒)
     * @param userdata  用户自定义数据指针
     */
    void (*duration_changed)(gpointer userdata, gint64 duration);

    /**
     * 媒体位置发生变化，
     * @param position  正在播放的媒体位置，单位纳秒
     *                  1 秒 = 1,000 ms (毫秒) = 1,000,000 μs (微秒) = 1,000,000,000 ns (纳秒)
     * @param userdata  用户自定义数据指针
     */
    void (*position_changed)(gpointer userdata, gint64 position);

    /**
     * 视频部分
     * @param use_kmssink   视频是否使用 kmssink
     *                        TRUE  使用 kmssink + plane_id
     *                        FALSE 使用 video_sample 返回视频帧
     */
    gboolean use_kmssink;
    union {
        /** 播放视频使用的DRM plane_id，在 use_kmssink 为 TRUE 时使用 */
        gint plane_id;
        /** 播放视频帧数据回调，在 use_kmssink 为 FALSE 时使用 */
        void (*video_sample)(gpointer userdata, GstSample *sample);
    };

    /**
     * 用户自定义数据真正指针
     */
    gpointer userdata;

    //----------------------------------
    // 以下播放框架内部使用
    gpointer player;

} GMediaContext;

typedef GMediaContext* GMediaContextPtr;

typedef enum
{
  GST_PLAY_TRICK_MODE_NONE = 0,
  GST_PLAY_TRICK_MODE_DEFAULT,
  GST_PLAY_TRICK_MODE_DEFAULT_NO_AUDIO,
  GST_PLAY_TRICK_MODE_KEY_UNITS,
  GST_PLAY_TRICK_MODE_KEY_UNITS_NO_AUDIO,
  GST_PLAY_TRICK_MODE_LAST,

  /* The instant-rate setting is a flag,
   * applied on top of the trick-mode enum value.
   * It needs to have a 2^n value bigger than 
   * any of the enum values so setting it
   * won't affect the trickmode value */
  GST_PLAY_TRICK_MODE_INSTANT_RATE = (1 << 3)
} GstPlayTrickMode;

typedef enum
{
    GST_PLAY_TRACK_TYPE_INVALID = 0,
    GST_PLAY_TRACK_TYPE_AUDIO,
    GST_PLAY_TRACK_TYPE_VIDEO,
    GST_PLAY_TRACK_TYPE_SUBTITLE
} GstPlayTrackType;

typedef enum
{
    RESULT_SUCCESS = 0,

    /* 分配内存失败 */
    ERROR_MEM_ALLOC = -1,

    /* 无法创建播放管线 */
    ERROR_PLAYBIN_NEW = -2,

    /* 无法获得播放管线的消息总线 */
    ERROR_GET_BUS = -3,

    /* 无法创建播放管线的消息总线回调 */
    ERROR_BUS_ADD_WATCH = -4,

    /* 不支持的媒体类型 */
    ERROR_INVALID_MEDIA_TYPE = -5,

    /* 参数错误 */
    ERROR_INVALID_PARAM = -6,

    ERROR_ELEMENT_MAKE = -7,

    ERROR_GST_INITIAL = -8,

} GResult;

/**
 * gint64 播放位置和长度转换为 "HH:MM:SS"
 * @param time      需要转换的时间
 * @param buffer    存储时间输出的字符串缓冲区，长度为10
 */
GST_PLAYBACK_API void gst_time_to_string(gint64 time, gchar *buffer);

/**
 * gstreamer 播放框架初始化，仅需要初始化一次，之后可以连续播放
 * 
 * @param ctx               播放框架上下文
 * @param flags             播放标志枚举，用于配置输出端行为的扩展标志
 *  typedef enum {
 *    GST_PLAY_FLAG_VIDEO,              启用视频流渲染
 *    GST_PLAY_FLAG_AUDIO,              启用音频流渲染
 *    GST_PLAY_FLAG_TEXT,               启用字幕渲染
 *    GST_PLAY_FLAG_VIS,                无视频流时，启用音频可视化效果渲染
 *    GST_PLAY_FLAG_SOFT_VOLUME,        使用软件音量控制
 *
 *    GST_PLAY_FLAG_NATIVE_AUDIO,       仅允许原生音频格式，
 *                                      此标志会省略 audioconvert 和 audioresample 的配置
 *
 *    GST_PLAY_FLAG_NATIVE_VIDEO,       仅允许原生视频格式，
 *                                      此标志会省略 videoconvert 和 videoscale 的配置
 *
 *    GST_PLAY_FLAG_DOWNLOAD,           对选定格式启用渐进式下载缓冲
 *
 *    GST_PLAY_FLAG_BUFFERING,          对解封装或解析后的数据启用缓冲处理
 *
 *    GST_PLAY_FLAG_DEINTERLACE,        对原始视频进行去隔行处理（未强制原生格式时生效）
 *
 *    GST_PLAY_FLAG_SOFT_COLORBALANCE,  使用软件色彩平衡滤镜
 *
 *    GST_PLAY_FLAG_FORCE_FILTERS,      如果设置了滤镜，则强制应用音视频滤镜
 *
 *    GST_PLAY_FLAG_FORCE_SW_DECODERS,  强制仅使用纯软件解码器，
 *                                      忽略所有硬件解码器类
 *  } GstPlayFlags;
 *
 * @param initial_volume        默认音量，0-100
 * @param start_position        播放起始位置，等于 0 从头播放，单位：秒
 * @param accurate_seeks        是否启用精确定位，等于 TRUE 时精确到帧。
 * @param instant_rate_changes  即时速率切换 / 无缝倍速切换
 */
GST_PLAYBACK_API GResult gst_media_init(GMediaContextPtr ctx, 
                                        GstPlayFlags flags, 
                                        gint initial_volume,
                                        gdouble start_position, 
                                        gboolean accurate_seeks,
                                        gboolean instant_rate_changes);

/**
 * gstreamer 播放框架释放
 * 
 * @param ctx   播放框架上下文
 */
GST_PLAYBACK_API void gst_media_deinit(GMediaContextPtr ctx);

/**
 * 播放指定类型的媒体文件
 * @param ctx       播放框架上下文
 * @param type      媒体类型
 * @param filename  媒体路径文件名
 */
GST_PLAYBACK_API void gst_media_play(GMediaContextPtr ctx, const gchar *filename);

/**
 * 停止正在进行的播放
 * @param ctx   播放框架上下文
 */
GST_PLAYBACK_API void gst_media_stop(GMediaContextPtr ctx);

/**
 * 查询当前播放状态
 * @param ctx   播放框架上下文
 * @return      当前的播放状态
 */
GST_PLAYBACK_API GstState gst_media_get_state(GMediaContextPtr ctx);

/**
 * 查询是否在播放状态，包括即将切换至播放状态
 * @param ctx       播放框架上下文
 * @return  TRUE为在播放中
 */
GST_PLAYBACK_API gboolean gst_media_is_playing(GMediaContextPtr ctx);
/**
 * 切换暂停播放状态，如果原来在暂停状态，将切换为播放状态，反之切换为暂停状态
 * @param ctx       播放框架上下文
 */
GST_PLAYBACK_API void gst_media_toggle_paused(GMediaContextPtr ctx);

GST_PLAYBACK_API gboolean gst_media_seek(GMediaContextPtr ctx, gint64 position);

/**
 * 获取当前音量
 * @param ctx   播放框架上下文
 * @return      当前音量，0-100
 */
GST_PLAYBACK_API gint gst_media_get_volume(GMediaContextPtr ctx);
/**
 * 设置当前音量
 * @param ctx       播放框架上下文
 * @param volume    设置的音量值，0-100
 */
GST_PLAYBACK_API void gst_media_set_volume(GMediaContextPtr ctx, gint volume);

GST_PLAYBACK_API gdouble gst_media_get_rate(GMediaContextPtr ctx);
GST_PLAYBACK_API void gst_media_set_playback_rate (GMediaContextPtr ctx, gdouble rate);
GST_PLAYBACK_API void gst_media_set_relative_playback_rate (GMediaContextPtr ctx, gdouble rate_step, gboolean reverse_direction);

#endif /* __GST_PLAYBACK_H__ */
