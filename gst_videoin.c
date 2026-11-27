/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 视频输入插件
 */

/**
 * History:
 * ================================================================
 * 2026-09-22 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
// #define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "gstvideoin"
#include <log.h>
//==============================================================================

#ifdef LINUX
#pragma GCC target("fpu=neon-vfpv4")
#pragma GCC diagnostic ignored "-Wattributes"

#include "native_window/native_window_fb_gl.h"
#include "tkc/types_def.h"
#include <arm_neon.h>
#include <libusb-1.0/libusb.h>
#include <linux/dma-buf.h>
#include <rga/RgaUtils.h>
#include <rga/im2d.h>
#include <sys/ioctl.h>
#endif

#include "../common/usbtransfer.h"
#include "tkc/types_def.h"
#include <gst/allocators/gstdmabuf.h>
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef WINDOWS
#include <libusb.h>
#endif

//------------------------------------------------------------------------------

#define GST_PLUGIN_BUILD_STATIC

#ifndef PACKAGE
#define PACKAGE "videoin"
#endif

#ifdef WINDOWS

#define MAX_POOL_SIZE 8

#define MAX_VIDEO_WIDTH 1280
#define MAX_VIDEO_HEIGHT 720
#define MAX_VIDEO_BYTES_PER_PIXEL 4

typedef struct {
    void *ptr;
    size_t size;
    int w;
    int h;
} plane_bo_t;
#endif

#ifdef LINUX
#define MAX_POOL_SIZE DRM_CMA_BUFFER_NUMBER
#endif

//------------------------------------------------------------------------------

typedef enum {
    INIT_STATE_SUCCESS,
    INIT_STATE_LIBUSB_INIT_FAILED,
    INIT_STATE_LIBUSB_GET_DEVICE_LIST_FAILED,
    INIT_STATE_LIBUSB_NO_DEVICE_FOUND,
} InitState;

enum {
    PROP_0,
    PROP_SOURCE,
    PROP_SOURCE_CHANGED,
    PROP_STANDARD,
    PROP_STANDARD_CHANGED,
};

typedef void (*GstSourceChangedCallback)(guint source, gpointer user_data);
typedef struct {
    GstSourceChangedCallback func;
    gpointer user_data;
} GstSourceChangedCallbackInfo;

typedef void (*GstStandardChangedCallback)(guint standard, gpointer user_data);
typedef struct {
    GstStandardChangedCallback func;
    gpointer user_data;
} GstStandardChangedCallbackInfo;

typedef struct _GstVideoIn {
    GstVideoFilter parent;

    gint pool_count;
    gint pool_index;
    plane_bo_t *vop_buf[MAX_POOL_SIZE];

#ifdef LINUX
    GstAllocator *dmabuf_alloc;
#endif

    int g_devices_num;
    libusb_device_handle *g_devices_handle[3];
    libusb_device **g_usb_devs;
    uint32_t init_state;

    guint vin_source;

    gboolean is_blue_screen;
    guint vin_standard;
    guint detect_standard;
    GstClockTime vin_detect_time;

    GstSourceChangedCallbackInfo source_changed_info;
    GstStandardChangedCallbackInfo standard_changed_info;
} GstVideoIn;

#define GST_VIDEO_IN(object) ((GstVideoIn *)object)

typedef struct _GstVideoInClass {
    GstVideoFilterClass parent_class;
} GstVideoInClass;

G_DEFINE_TYPE(GstVideoIn, gst_videoin, GST_TYPE_VIDEO_FILTER);
#define GST_TYPE_VIDEOIN (gst_videoin_get_type())

#define VIDEO_IN_DEBOUNCE_TIME_NS (1000 * 1000 * 1000)
#define VIDEO_USB_INTERFACE_NUMBER 4
#define VIDEO_USB_VID 0x534d
#define VIDEO_USB_PID 0x0021
#define VIDEO_IN_UNKNOWN 0x07

//==============================================================================

__attribute__((unused)) static const char *s_video_in_source_name[] = {
    "VIDEO_IN_SOURCE_CVBS",
    "VIDEO_IN_SOURCE_SVIDEO",
};
#define VIDEO_IN_SOURCE_UNKNOWN (0x7)

__attribute__((unused)) static const char *s_video_in_standard_name[] = {
    "VIDEO_IN_STANDARD_NTSC_358",
    "VIDEO_IN_STANDARD_NTSC_443",
    "VIDEO_IN_STANDARD_PAL",
    "VIDEO_IN_STANDARD_PAL_M",
    "VIDEO_IN_STANDARD_PAL_NC",
    "VIDEO_IN_STANDARD_SECAM",
    "VIDEO_IN_STANDARD_PAL_60",
    "VIDEO_IN_STANDARD_UNKNOWN",
};
#define VIDEO_IN_STANDARD_UNKNOWN (0x7)

//------------------------------------------------------------------------------

#ifdef WINDOWS
GstVideoConverter *converter = NULL;
#endif

static int get_product_descriptor_string(libusb_device_handle *handle,
                                         struct libusb_device_descriptor desc,
                                         unsigned char *str) {
    int ret;
    int size;

    ret = libusb_get_string_descriptor_ascii(handle, desc.iProduct, str, 32);
    if (ret < 0) {
        printf("get product descriptor string failed, ret %d\n", ret);
        return -1;
    }

    size = strlen((char *)str);
    for (int i = 0; i < size; i++) {
        if (str[i] == 0x20 || str[i] == ' ') {
            str[i] = 0;
            break;
        }
    }

    return 0;
}

static gboolean get_devs(GstVideoIn *ctx,
                         libusb_device **devs,
                         uint16_t vid,
                         uint16_t pid,
                         int interface_number) {
    int i = 0, ret = 0;
    libusb_device *dev;
    libusb_device_handle *handle;
    unsigned char *str_product;

    str_product = (unsigned char *)malloc(256);
    while ((dev = devs[i++]) != NULL) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(dev, &desc);
        if (ret < 0) {
            fprintf(stderr, "failed to get device descriptor.\n");
            free(str_product);
            return FALSE;
        }

        if (desc.idVendor == VIDEO_USB_VID && desc.idProduct == VIDEO_USB_PID) {
            handle = 0;
            ret = libusb_open(dev, &handle);
            if (ret == -3) {
                printf("Permission denied!\n");
                free(str_product);
                return FALSE;
            }
            ctx->g_devices_handle[ctx->g_devices_num++] = handle;
            ret = get_product_descriptor_string(handle, desc, str_product);
            if (ret < 0) {
                printf("find device handle:%p, devId: %d\n",
                       handle,
                       ctx->g_devices_num - 1);
            } else {
                printf("find device:\n>>> devId: %d, device descriptor: %s\n",
                       ctx->g_devices_num - 1,
                       str_product);
                printf("--- --- ---\n");
            }
        }
    }
    free(str_product);

    return TRUE;
}

static guint get_uvc_video_in_source(GstVideoIn *ctx) {
    int device_id = 0;
    uint16_t addr = 0x48;
    uint8_t data;

    if (xdata_read(ctx->g_devices_handle[device_id],
                   addr,
                   &data,
                   VIDEO_USB_INTERFACE_NUMBER) < 0) {
        return 0;
    }
    LOG_V("video in source: %s", s_video_in_source_name[data & 0x1]);
    return data & 0x1;
}

static guint get_uvc_video_in_standard(GstVideoIn *ctx) {
    int device_id = 0;
    uint16_t addr = 0x40;
    uint8_t data;

    if (xdata_read(ctx->g_devices_handle[device_id],
                   addr,
                   &data,
                   VIDEO_USB_INTERFACE_NUMBER) < 0) {
        return VIDEO_IN_UNKNOWN;
    }
    LOG_V("video in source: %s", s_video_in_standard_name[data & 0x7]);
    return data & 0x7;
}

static void gst_videoin_set_property(GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec) {
    THROW_ERROR_IF_FAIL(object != NULL, "插件 object 为空指针！");
    LOG_V("(object=%p, prop_id=%u, value=%p, pspec=%p)",
          object,
          prop_id,
          value,
          pspec);

    GstVideoIn *self = GST_VIDEO_IN(object);

    switch (prop_id) {
    case PROP_SOURCE_CHANGED: {
        GstSourceChangedCallbackInfo *info =
            (GstSourceChangedCallbackInfo *)g_value_get_pointer(value);
        if (info) {
            self->source_changed_info.func = info->func;
            self->source_changed_info.user_data = info->user_data;
        } else {
            self->source_changed_info.func = NULL;
            self->source_changed_info.user_data = NULL;
        }
        break;
    }
    case PROP_STANDARD_CHANGED: {
        GstStandardChangedCallbackInfo *info =
            (GstStandardChangedCallbackInfo *)g_value_get_pointer(value);
        if (info) {
            self->standard_changed_info.func = info->func;
            self->standard_changed_info.user_data = info->user_data;
        } else {
            self->standard_changed_info.func = NULL;
            self->standard_changed_info.user_data = NULL;
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_videoin_get_property(GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec) {
    THROW_ERROR_IF_FAIL(object != NULL, "插件 object 为空指针！");
    LOG_V("(object=%p, prop_id=%u, value=%p, pspec=%p)",
          object,
          prop_id,
          value,
          pspec);

    GstVideoIn *self = GST_VIDEO_IN(object);

    switch (prop_id) {
    case PROP_SOURCE: {
        self->vin_source = get_uvc_video_in_source(self);
        g_value_set_uint(value, self->vin_source);
        break;
    }
    case PROP_STANDARD: {
        self->vin_standard = get_uvc_video_in_standard(self);
        self->vin_detect_time = GST_CLOCK_TIME_NONE;
        g_value_set_uint(value, self->vin_standard);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

//------------------------------------------------------------------------------

static GstCaps *gst_videoin_transform_caps(GstBaseTransform *trans,
                                           GstPadDirection direction,
                                           GstCaps *caps,
                                           GstCaps *filter) {
    LOG_V("direction=%d", direction);
    GstCaps *next_caps = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); i++) {
        GstStructure *structure = gst_caps_get_structure(caps, i);
        GstStructure *new_struct = gst_structure_copy(structure);
        gst_structure_remove_fields(new_struct,
                                    "format",
                                    "colorimetry",
                                    "chroma-site",
                                    NULL);
#ifdef WINDOWS
        if (direction == GST_PAD_SRC) {
            // 输出反查输入支持：声明我需要 YUY2
            GValue list = G_VALUE_INIT;
            g_value_init(&list, GST_TYPE_LIST);
            GValue v = G_VALUE_INIT;
            g_value_init(&v, G_TYPE_STRING);
            g_value_set_string(&v, "YUY2");
            gst_value_list_append_value(&list, &v);
            gst_structure_take_value(new_struct, "format", &list);
            g_value_unset(&v);

        } else {
            // 输入询问输出支持：强制只能转 ARGB
            gst_structure_set(new_struct,
                              "format",
                              G_TYPE_STRING,
                              "RGBA",
                              NULL);
            gst_structure_set_name(new_struct, "video/x-raw");
        }
#endif

#ifdef LINUX
        if (direction == GST_PAD_SRC) {
            // 输出反查输入支持：声明我需要 YUY2
            GValue list = G_VALUE_INIT;
            g_value_init(&list, GST_TYPE_LIST);
            GValue v = G_VALUE_INIT;
            g_value_init(&v, G_TYPE_STRING);
            g_value_set_string(&v, "YUY2");
            gst_value_list_append_value(&list, &v);
            gst_structure_take_value(new_struct, "format", &list);
            g_value_unset(&v);

        } else {
            // 输入询问输出支持：强制只能转 NV12
            gst_structure_set(new_struct,
                              "format",
                              G_TYPE_STRING,
                              "NV12",
                              NULL);
            gst_structure_set_name(new_struct, "video/x-raw");
        }
#endif
        gst_caps_append_structure(next_caps, new_struct);
    }
    if (filter) {
        GstCaps *intersection =
            gst_caps_intersect_full(filter,
                                    next_caps,
                                    GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(next_caps);
        return intersection;
    }

    LOG_V("next_caps");
    return next_caps;
}

static gboolean gst_videoin_set_info(GstVideoFilter *filter,
                                     GstCaps *incaps,
                                     GstVideoInfo *in_info,
                                     GstCaps *outcaps,
                                     GstVideoInfo *out_info) {
    if (GST_VIDEO_INFO_FORMAT(in_info) != GST_VIDEO_FORMAT_YUY2) {
        return FALSE;
    }

    LOG_V("IN: %dx%d",
          GST_VIDEO_INFO_WIDTH(in_info),
          GST_VIDEO_INFO_HEIGHT(in_info));
    LOG_V("OUT: %dx%d",
          GST_VIDEO_INFO_WIDTH(out_info),
          GST_VIDEO_INFO_HEIGHT(out_info));

    int dst_w = GST_VIDEO_INFO_WIDTH(in_info);
    int dst_h = GST_VIDEO_INFO_HEIGHT(in_info);
    int dst_ws = dst_w;

#ifdef WINDOWS
    // 强行把输出的 info 固化为 RGBA，并严格继承上游的宽高
    gst_video_info_set_format(out_info, GST_VIDEO_FORMAT_RGBA, dst_w, dst_h);
    out_info->fps_n = in_info->fps_n;
    out_info->fps_d = in_info->fps_d;
    out_info->par_n = in_info->par_n;
    out_info->par_d = in_info->par_d;

    out_info->stride[0] = dst_ws;
    out_info->offset[0] = 0;

    out_info->size = dst_ws * dst_h * 4;
#endif

#ifdef LINUX
    // 强行把输出的 info 固化为 NV12，并严格继承上游的宽高
    gst_video_info_set_format(out_info, GST_VIDEO_FORMAT_NV12, dst_w, dst_h);
    out_info->fps_n = in_info->fps_n;
    out_info->fps_d = in_info->fps_d;
    out_info->par_n = in_info->par_n;
    out_info->par_d = in_info->par_d;

    // NV12 格式：Plane 0 是 Y 信号，Plane 1 是 UV 交错信号
    out_info->stride[0] = dst_ws; // Y 平面的每行跨度
    out_info->stride[1] = dst_ws; // UV 平面的每行跨度

    // 重新计算每个平面在物理内存中的起始偏移量（必须与对齐后的步长严格同步）
    out_info->offset[0] = 0; // Y 平面起始地址
    out_info->offset[1] =
        dst_ws * dst_h; // UV 平面起始地址（位于 Y 平面正下方）

    // 3. 强行刷新整帧视频的总内存字节大小，防止后级读取时发生物理内存越界
    // NV12 内存大小 = (对齐后的宽 * 高) + (对齐后的宽 * 高 / 2) = 1.5 *
    // 对齐后的宽 * 高
    out_info->size = dst_ws * dst_h * 3 / 2;
#endif
    return TRUE;
}

static gboolean gst_videoin_start(GstBaseTransform *trans) {
    GstVideoIn *self = GST_VIDEO_IN(trans);
    if (self->init_state != INIT_STATE_SUCCESS) {
        return FALSE;
    }
#ifdef LINUX
    self->dmabuf_alloc = gst_dmabuf_allocator_new();
#endif
    return TRUE;
}

static gboolean gst_videoin_stop(GstBaseTransform *trans) {
#ifdef LINUX
    GstVideoIn *self = GST_VIDEO_IN(trans);
    if (self->dmabuf_alloc)
        gst_object_unref(self->dmabuf_alloc);
#endif
    return TRUE;
}

static GstFlowReturn gst_videoin_transform_frame(GstVideoFilter *filter,
                                                 GstVideoFrame *in_frame,
                                                 GstVideoFrame *out_frame) {
    GstVideoIn *self = GST_VIDEO_IN(filter);

#define DEBUG_TIMER 0
#if DEBUG_TIMER
    GstClockTime start_time = gst_util_get_timestamp();
#endif
#if 0
    guint32 fmt = GST_VIDEO_FRAME_FORMAT(in_frame);
    guint32 fourcc = gst_video_format_to_fourcc(fmt);
    LOG_V("IN: %dx%d %c%c%c%c",
          GST_VIDEO_FRAME_WIDTH(in_frame),
          GST_VIDEO_FRAME_HEIGHT(in_frame),
          (fourcc & 0xff),
          ((fourcc >> 8) & 0xff),
          ((fourcc >> 16) & 0xff),
          ((fourcc >> 24) & 0xff));

    fmt = GST_VIDEO_FRAME_FORMAT(out_frame);
    fourcc = gst_video_format_to_fourcc(fmt);
    LOG_V("OUT: %dx%d %c%c%c%c",
          GST_VIDEO_FRAME_WIDTH(out_frame),
          GST_VIDEO_FRAME_HEIGHT(out_frame),
          (fourcc & 0xff),
          ((fourcc >> 8) & 0xff),
          ((fourcc >> 16) & 0xff),
          ((fourcc >> 24) & 0xff));
#endif

#ifdef LINUX
    plane_bo_t *dst_bo = NULL;
    int dst_fd = -1;
    if (self->pool_count > 0) {
        // 选择输出 FD (优先使用外部 Pool)
        dst_bo = self->vop_buf[self->pool_index];
        dst_fd = dup(dst_bo->buf_fd); // 复制一个句柄，交给下游去消耗/关闭
        self->pool_index = (self->pool_index + 1) % self->pool_count;

        // 包装这个 dup 出来的临时 FD
        GstMemory *new_mem = gst_dmabuf_allocator_alloc(self->dmabuf_alloc,
                                                        dst_fd,
                                                        out_frame->info.size);
        gst_buffer_replace_all_memory(out_frame->buffer, new_mem);

        int dst_ws = GST_VIDEO_FRAME_PLANE_STRIDE(out_frame, 0);
        int dst_h = GST_VIDEO_FRAME_HEIGHT(out_frame);
        GST_VIDEO_FRAME_PLANE_DATA(out_frame, 0) = dst_bo->ptr;
        GST_VIDEO_FRAME_PLANE_DATA(out_frame, 1) =
            (uint8_t *)dst_bo->ptr + dst_ws * dst_h;

    } else {
        LOG_W("回退到下游协商的 FD");
        GstMemory *out_mem = gst_buffer_peek_memory(out_frame->buffer, 0);
        if (gst_is_dmabuf_memory(out_mem)) {
            dst_fd = gst_dmabuf_memory_get_fd(out_mem);
        }
    }
#endif

    // 获取输入和输出的像素映射基地址
    int src_w = GST_VIDEO_FRAME_WIDTH(in_frame);
    int src_h = GST_VIDEO_FRAME_HEIGHT(in_frame);

    guint vin_standard = get_uvc_video_in_standard(self);
    gboolean is_blue_screen = self->vin_standard == VIDEO_IN_STANDARD_UNKNOWN;
    if (self->vin_detect_time == GST_CLOCK_TIME_NONE) {
        if (self->vin_standard != vin_standard) {
            self->detect_standard = vin_standard;
            self->vin_detect_time = gst_util_get_timestamp();
            /**
             * 原输入视频制式不为 VIDEO_IN_STANDARD_UNKNOWN* 时，
             * 输入视频制式发生变化自动切换为蓝屏
             */
            is_blue_screen = self->vin_standard != VIDEO_IN_STANDARD_UNKNOWN;

        } else {
            if (self->vin_standard != VIDEO_IN_STANDARD_UNKNOWN) {
                guint vin_source = get_uvc_video_in_source(self);
                if (self->vin_source != vin_source) {
                    self->vin_source = vin_source;
                    if (self->source_changed_info.func != NULL) {
                        self->source_changed_info.func(
                            self->vin_source,
                            self->source_changed_info.user_data);
                    }
                }
            }
        }
    } else {
        is_blue_screen = self->vin_standard != VIDEO_IN_STANDARD_UNKNOWN;
        /**
         * 输入视频制式保持时间中再次变化，重新开始计时
         */
        if (self->detect_standard != vin_standard) {
            self->detect_standard = vin_standard;
            self->vin_detect_time = gst_util_get_timestamp();

        } else {
            GstClockTime keep_time =
                gst_util_get_timestamp() - self->vin_detect_time;
            if (keep_time > VIDEO_IN_DEBOUNCE_TIME_NS) {
                self->vin_detect_time = GST_CLOCK_TIME_NONE;
                self->vin_standard = self->detect_standard;
                if (self->standard_changed_info.func != NULL) {
                    self->standard_changed_info.func(
                        self->vin_standard,
                        self->standard_changed_info.user_data);
                }
            }
        }
    }

#ifdef WINDOWS
    uint8_t *dst_data = (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(out_frame, 0);
    int dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE(out_frame, 0);

    if (is_blue_screen) {
        for (int y = 0; y < src_h; y++) {
            // 获取当前行的行首指针
            uint8_t *dst_row = dst_data + y * dst_stride;

            for (int x = 0; x < src_w; x++) {
                dst_row[0] = 0;   // R
                dst_row[1] = 0;   // G
                dst_row[2] = 255; // B (纯蓝)
                dst_row[3] = 255; // A (不透明)

                dst_row += 4; // 移动到下一个 RGBA 像素（每个像素 4 字节）
            }
        }
    } else {
        if (converter == NULL) {
            // 获取输入和输出的完整视频信息（包含色彩空间、限幅等元数据）
            GstVideoInfo *in_info = &in_frame->info;
            GstVideoInfo *out_info = &out_frame->info;
            // 创建完美的官方转换器，它会自动识别 CVBS 的 BT.601
            // 限幅并做画质优化
            converter = gst_video_converter_new(in_info, out_info, NULL);
        }

        gst_video_converter_frame(converter, in_frame, out_frame);
    }
#endif

#ifdef LINUX
    // 通知内核：CPU 准备通过映射指针强行向这块物理 FD 写入数据了，刷新 Cache
    // 避开冲突
    struct dma_buf_sync sync = {.flags =
                                    DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
    ioctl(dst_fd, DMA_BUF_IOCTL_SYNC, &sync);

    uint8_t *dst_y = (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(out_frame, 0);
    uint8_t *dst_uv = (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(out_frame, 1);
    int dst_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(out_frame, 0);
    int dst_uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(out_frame, 1);

    int neon_pixels_per_loop = 16;
    int width_aligned_count = src_w / neon_pixels_per_loop;

    if (is_blue_screen) {
        // ==========================================
        // 修改为：直接使用 NEON 汇编高效填充 NV12 纯蓝色
        // ==========================================

        // 1. 初始化纯蓝色的 NEON 寄存器
        // Y 对应 29 (0x1D)
        uint8x16_t b_y_vec = vdupq_n_u8(29);

        // UV 交叉面交替写入 U=255(0xFF), V=107(0x6B)
        uint8x8_t b_u_vec = vdup_n_u8(255);
        uint8x8_t b_v_vec = vdup_n_u8(107);
        uint8x8x2_t b_uv_vec;
        b_uv_vec.val[0] = b_u_vec;
        b_uv_vec.val[1] = b_v_vec;

        // 2. 开始逐行填充
        for (int i = 0; i < src_h; i++) {
            uint8_t *dst_y_row = dst_y + (i * dst_y_stride);

            // 填充 Y 面 (每循环处理 16 个像素的 Y 分量)
            for (int j = 0; j < width_aligned_count; j++) {
                vst1q_u8(dst_y_row, b_y_vec);
                dst_y_row += 16;
            }

            // 填充 UV 面 (NV12 格式中，UV 的行数只有 Y
            // 的一半，且只有偶数行需要写入)
            if (i % 2 == 0) {
                uint8_t *dst_uv_row = dst_uv + ((i / 2) * dst_uv_stride);

                // 每循环写入 8 个 U 和 8 个 V，刚好对应 16 个像素
                for (int j = 0; j < width_aligned_count; j++) {
                    vst2_u8(dst_uv_row, b_uv_vec);
                    dst_uv_row += 16;
                }
            }
        }

    } else {
        uint8_t *src_data = (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA(in_frame, 0);
        int src_ws = GST_VIDEO_FRAME_PLANE_STRIDE(in_frame, 0);

        for (int i = 0; i < src_h; i++) {
            const uint8_t *src_row = src_data + (i * src_ws);
            uint8_t *dst_y_row = dst_y + (i * dst_y_stride);
            uint8_t *dst_uv_row = dst_uv + ((i / 2) * dst_uv_stride);

            if (i < src_h - 1) {
                __builtin_prefetch(src_row + src_ws, 0, 3);
            }

            if (i % 2 == 0) {
                for (int j = 0; j < width_aligned_count; j++) {
                    uint8x8x4_t yuyv = vld4_u8(src_row);
                    uint8x8x2_t y_combined;
                    y_combined.val[0] = yuyv.val[0];
                    y_combined.val[1] = yuyv.val[2];
                    vst2_u8(dst_y_row, y_combined);

                    uint8x8x2_t uv_combined;
                    uv_combined.val[0] = yuyv.val[1];
                    uv_combined.val[1] = yuyv.val[3];
                    vst2_u8(dst_uv_row, uv_combined);

                    src_row += 32;
                    dst_y_row += 16;
                    dst_uv_row += 16;
                }
            } else {
                for (int j = 0; j < width_aligned_count; j++) {
                    uint8x8x4_t yuyv = vld4_u8(src_row);
                    uint8x8x2_t y_combined;
                    y_combined.val[0] = yuyv.val[0];
                    y_combined.val[1] = yuyv.val[2];
                    vst2_u8(dst_y_row, y_combined);

                    src_row += 32;
                    dst_y_row += 16;
                }
            }
        }
    }

    // 通知内核：CPU 写入结束，将 Cache 数据强制刷回 DDR
    // 物理颗粒，通知下游硬件（kmssink）来读
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    ioctl(dst_fd, DMA_BUF_IOCTL_SYNC, &sync);
#endif

    // ====== 【核心精简点】 ======
    // 彻底删掉之前动态创建 allocator、dup 和 remove_all_memory 的代码！
    // 你的数据已经完好地呆在由 Ref 机制托管的 out_frame->buffer 里了，直接返回
    // OK。

#if DEBUG_TIMER
    GstClockTime end_time = gst_util_get_timestamp();
    double duration = (double)(end_time - start_time) / GST_MSECOND;
    static int frame_count = 0;
    if (frame_count++ % 30 == 0) {
        LOG_I("%dx%d Secure Cost: %.2f ms", width, height, duration);
    }
#endif
    return GST_FLOW_OK;
}

//------------------------------------------------------------------------------

#define GST_VIDEO_IN_STATIC_CAPS(name, direction, string)                      \
    static GstStaticCaps gst_videoin_##name##_format_caps =                    \
        GST_STATIC_CAPS(string);                                               \
    static GstCaps *gst_videoin_##name##_get_capslist(void) {                  \
        static GstCaps *caps = NULL;                                           \
        static gsize inited = 0;                                               \
        if (g_once_init_enter(&inited)) {                                      \
            caps = gst_static_caps_get(&gst_videoin_##name##_format_caps);     \
            g_once_init_leave(&inited, 1);                                     \
        }                                                                      \
        return caps;                                                           \
    }                                                                          \
    static GstPadTemplate *gst_videoin_##name##_template_factory(void) {       \
        return gst_pad_template_new(#name,                                     \
                                    direction,                                 \
                                    GST_PAD_ALWAYS,                            \
                                    gst_videoin_##name##_get_capslist());      \
    }

#ifdef WINDOWS
GST_VIDEO_IN_STATIC_CAPS(sink,
                         GST_PAD_SINK,
                         "video/x-raw(memory:DMABuf), format=(string)YUY2; "
                         "video/x-raw, format=(string)YUY2")

GST_VIDEO_IN_STATIC_CAPS(src,
                         GST_PAD_SRC,
                         "video/x-raw(memory:DMABuf), format=(string)RGBA; "
                         "video/x-raw, format=(string)RGBA")
#endif

#ifdef LINUX
GST_VIDEO_IN_STATIC_CAPS(sink,
                         GST_PAD_SINK,
                         "video/x-raw(memory:DMABuf), format=(string)YUY2; "
                         "video/x-raw, format=(string)YUY2")

GST_VIDEO_IN_STATIC_CAPS(src,
                         GST_PAD_SRC,
                         "video/x-raw(memory:DMABuf), format=(string)NV12; "
                         "video/x-raw, format=(string)NV12")
#endif

static void gst_videoin_finalize(GObject *object) {
    THROW_ERROR_IF_FAIL(object != NULL, "插件 object 为空指针！");
    LOG_V("(object=%p)", object);

    GstVideoIn *self = GST_VIDEO_IN(object);

    for (int i = 0; i < self->g_devices_num; i++) {
        if (self->g_devices_handle[i] != NULL) {
            releaseInterface(self->g_devices_handle[i],
                             VIDEO_USB_INTERFACE_NUMBER);
            libusb_close(self->g_devices_handle[i]);
            self->g_devices_handle[i] = NULL;
        }
    }

    if (self->g_usb_devs != NULL) {
        libusb_free_device_list(self->g_usb_devs, 1);
        self->g_usb_devs = NULL;
    }
    libusb_exit(NULL);

#ifdef WINDOWS
    for (int i = 0; i < self->pool_count; i++) {
        plane_bo_t *bo = self->vop_buf[i];
        if (bo != NULL) {
            g_free(bo->ptr);
            g_free(bo);
            self->vop_buf[i] = NULL;
        }
    }
#endif

    G_OBJECT_CLASS(gst_videoin_parent_class)->finalize(object);
}

static void gst_videoin_class_init(GstVideoInClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstVideoFilterClass *filter_class = GST_VIDEO_FILTER_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    /* 插件属性 */
    gobject_class->set_property = gst_videoin_set_property;
    gobject_class->get_property = gst_videoin_get_property;

    g_object_class_install_property(
        gobject_class,
        PROP_SOURCE,
        g_param_spec_uint(
            "source",              /* 属性名称 */
            "Video in source",     /* 昵称 */
            "0: CVBS, 1: S-VIDEO", /* 详细描述 */
            0,                     /* 最小值 */
            1,                     /* 最大值 */
            0,                     /* 默认值 (UNKNOWN) */
            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class,
        PROP_SOURCE_CHANGED,
        g_param_spec_pointer(
            "source-changed",
            "Callback Pointer",
            "A pointer to GstSourceChangedCallbackInfo struct for data "
            "callback",
            (GParamFlags)(G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS) // 仅可写
            ));
    g_object_class_install_property(
        gobject_class,
        PROP_STANDARD,
        g_param_spec_uint(
            "standard",          /* 属性名称 */
            "Video in standard", /* 昵称 */
            "0: VDO_NTSC_358, 1: VDO_NTSC_443, 2: VDO_PAL, "
            "3: VDO_PAL_M, 4: VDO_PAL_NC, 5: VDO_SECAM, "
            "6: VDO_PAL_60, 7: UNKNOWN", /* 详细描述 */
            0,                           /* 最小值 */
            VIDEO_IN_UNKNOWN,            /* 最大值 */
            VIDEO_IN_UNKNOWN,            /* 默认值 (UNKNOWN) */
            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class,
        PROP_STANDARD_CHANGED,
        g_param_spec_pointer(
            "standard-changed",
            "Callback Pointer",
            "A pointer to GstStandardChangedCallbackInfo struct for data "
            "callback",
            (GParamFlags)(G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS) // 仅可写
            ));

    /* 插件输入输出格式 */
    gst_element_class_add_pad_template(element_class,
                                       gst_videoin_sink_template_factory());
    gst_element_class_add_pad_template(element_class,
                                       gst_videoin_src_template_factory());

    /* 插件身份证信息 */
    gst_element_class_set_static_metadata(
        element_class,
        "Video colorspace converter",
        "Filter/Converter/Video/Colorspace",
        "Converts video from one colorspace to another",
        "Pu Jianfeng <jianfeng_pu@qq.com>");

    /* 核心格式协商虚函数 */
    trans_class->transform_caps = GST_DEBUG_FUNCPTR(gst_videoin_transform_caps);

    trans_class->start = gst_videoin_start;
    trans_class->stop = gst_videoin_stop;

    /* 核心图像与内存处理虚函数 */
    filter_class->set_info = GST_DEBUG_FUNCPTR(gst_videoin_set_info);
    filter_class->transform_frame =
        GST_DEBUG_FUNCPTR(gst_videoin_transform_frame);

    gobject_class->finalize = gst_videoin_finalize;
}

static void gst_videoin_init(GstVideoIn *self) {
    THROW_ERROR_IF_FAIL(self != NULL, "插件 self 为空指针！");
    LOG_V("(self=%p)", self);

    self->pool_index = 0;
    self->pool_count = MAX_POOL_SIZE;

#ifdef WINDOWS
    size_t size =
        MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT * MAX_VIDEO_BYTES_PER_PIXEL;
    for (uint32_t i = 0; i < MAX_POOL_SIZE; i++) {
        plane_bo_t *bo = g_malloc0(sizeof(plane_bo_t));
        bo->size = size;
        bo->w = MAX_VIDEO_WIDTH;
        bo->h = MAX_VIDEO_BYTES_PER_PIXEL;
        bo->ptr = g_malloc(size);
        self->vop_buf[i] = bo;
    }
#endif

#ifdef LINUX
    void *ctx = egl_devices_get_context();
    LOG_V("egl_devices_context=%p", ctx);
    for (guint i = 0; i < self->pool_count; i++) {
        self->vop_buf[i] = egl_devices_get_cma_buffer(ctx, i);
        LOG_V("self->vop_buf[%d]=%p", i, self->vop_buf[i]);
    }
#endif

    self->source_changed_info.func = NULL;
    self->source_changed_info.user_data = NULL;
    self->standard_changed_info.func = NULL;
    self->standard_changed_info.user_data = NULL;

    self->init_state = INIT_STATE_SUCCESS;

    self->g_devices_num = 0;
    int r = libusb_init(NULL);
    if (r < 0) {
        LOG_E("libusb init failed %d", r);
        self->init_state = INIT_STATE_LIBUSB_INIT_FAILED;

    } else {
        ssize_t cnt = libusb_get_device_list(NULL, &self->g_usb_devs);
        if (cnt < 0) {
            LOG_E("failed to get usb_device_list, exit.");
            self->init_state = INIT_STATE_LIBUSB_GET_DEVICE_LIST_FAILED;

        } else {
            if (get_devs(self,
                         self->g_usb_devs,
                         VIDEO_USB_VID,
                         VIDEO_USB_PID,
                         VIDEO_USB_INTERFACE_NUMBER) == TRUE) {
                if (self->g_devices_num == 0) {
                    LOG_E("no device found:vid:0x%x, pid:0x%x\n",
                          VIDEO_USB_VID,
                          VIDEO_USB_PID);

                    self->init_state = INIT_STATE_LIBUSB_NO_DEVICE_FOUND;

                } else {
                    for (int i = 0; i < self->g_devices_num; i++) {
                        claimInterface(self->g_devices_handle[i],
                                       VIDEO_USB_INTERFACE_NUMBER);
                    }
                }
            }
        }
    }

    self->vin_source = get_uvc_video_in_source(self);

    self->detect_standard = VIDEO_IN_STANDARD_UNKNOWN;
    self->vin_standard = get_uvc_video_in_standard(self);
    self->vin_detect_time = GST_CLOCK_TIME_NONE;
}

static gboolean plugin_init(GstPlugin *plugin) {
    THROW_ERROR_IF_FAIL(plugin != NULL, "插件 plugin 为空指针！");
    LOG_V("(plugin=%p)", plugin);
    return gst_element_register(plugin,
                                "videoin",
                                GST_RANK_PRIMARY,
                                GST_TYPE_VIDEOIN);
}

//------------------------------------------------------------------------------

gboolean register_gst_videoin_plugin(GstPlugin *plugin) {
    THROW_ERROR_IF_FAIL(plugin != NULL, "插件 plugin 为空指针！");
    LOG_V("(plugin=%p)", plugin);
    return plugin_init(plugin);
}

#ifndef GST_PLUGIN_BUILD_STATIC
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  videoin,
                  "Video In",
                  plugin_init,
                  "1.0",
                  "Proprietary",
                  "GStreamer",
                  "N/A")
#endif

//==============================================================================
