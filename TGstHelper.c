/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     GST 助手函数
 */

/**
 * History:
 * ================================================================
 * 2026-09-01 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TGstHelper"
#include <log.h>
//==============================================================================

#include "TGstHelper.h"

//==============================================================================

#define TG_GET_ENUM_NAME(short, enum_type)                                     \
    const gchar *TG_Get##short##Name(enum_type type) {                         \
        gint i = 0;                                                            \
        while (i < (ARRAY_SIZE(s##short##NameMap))) {                          \
            TEnumTypeNamePair pair = s##short##NameMap[i];                     \
            if (pair.type == type)                                             \
                return pair.name;                                              \
            i++;                                                               \
        }                                                                      \
        return s##short##Unknown;                                              \
    }

typedef struct {
    const gint type;
    const char *name;
} TEnumTypeNamePair;

static const TEnumTypeNamePair sStreamStatusNameMap[] = {
    // 需要创建一个新线程。
    {GST_STREAM_STATUS_TYPE_CREATE, "GST_STREAM_STATUS_TYPE_CREATE"},
    // 线程进入其循环函数
    {GST_STREAM_STATUS_TYPE_ENTER, "GST_STREAM_STATUS_TYPE_ENTER"},
    // 线程离开其循环函数
    {GST_STREAM_STATUS_TYPE_LEAVE, "GST_STREAM_STATUS_TYPE_LEAVE"},
    // 线程被销毁
    {GST_STREAM_STATUS_TYPE_DESTROY, "GST_STREAM_STATUS_TYPE_DESTROY"},
    // 线程启动
    {GST_STREAM_STATUS_TYPE_START, "GST_STREAM_STATUS_TYPE_START"},
    // 线程暂停
    {GST_STREAM_STATUS_TYPE_PAUSE, "GST_STREAM_STATUS_TYPE_PAUSE"},
    // 线程停止
    {GST_STREAM_STATUS_TYPE_STOP, "GST_STREAM_STATUS_TYPE_STOP"},
};
static const char *sStreamStatusUnknown = "GST_STREAM_STATUS_TYPE_UNKNOWN";
TG_GET_ENUM_NAME(StreamStatus, GstStreamStatusType)

//------------------------------------------------------------------------------

typedef struct {
    gchar *element_name;
} destroyed_data_t;

static void on_element_destroyed(gpointer user_data,
                                 GObject *where_the_object_was) {
    destroyed_data_t *data = (destroyed_data_t *)user_data;
    if (data != NULL) {
        LOG_V("对象 %s 已经彻底释放。",
              data->element_name ? data->element_name : "unknown");
        g_free(data->element_name);
        g_free(data);
    }
}

const gchar *tgst_get_factory_name(GstElement *element) {
    static gchar *factory_unknown = "unknown";
    return_value_if_fail(element != NULL, factory_unknown);

    GstElementFactory *factory = gst_element_get_factory(element);
    return factory != NULL
               ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))
               : factory_unknown;
}

void tgst_element_factory_make(GstElement **element,
                               const gchar *plugins,
                               const gchar *name) {
    THROW_ERROR_IF_FAIL(element != NULL, "element 不能为空！")

    if (*element != NULL) {
        LOG_W("GST 元素 '%s:%s' 重复创建！", plugins, name);
    } else {
        GstElement *e = gst_element_factory_make(plugins, name);
        if (e == NULL) {
            THROW_RUNTIME_ERROR("GST 元素 '%s:%s' 创建失败！", plugins, name);
        }
        LOG_V("GST 元素 '%s:%s' 创建成功。", plugins, name);
        *element = e;

        destroyed_data_t *data = g_new0(destroyed_data_t, 1);
        data->element_name = gst_element_get_name(e);
        g_object_weak_ref(G_OBJECT(e), on_element_destroyed, data);
    }
}

void tgst_bin_new(GstElement **bin, const gchar *name) {
    if (*bin != NULL) {
        LOG_W("GST 容器 '%s' 重复创建！", name);
    } else {
        GstElement *b = gst_bin_new(name);
        if (b == NULL) {
            THROW_RUNTIME_ERROR("GST 容器 '%s' 创建失败！", name);
        } else {
            LOG_V("GST 容器 '%s' 创建成功。", name);
        }
        *bin = b;
        destroyed_data_t *data = g_new0(destroyed_data_t, 1);
        data->element_name = gst_element_get_name(b);
        g_object_weak_ref(G_OBJECT(b), on_element_destroyed, data);
    }
}

//------------------------------------------------------------------------------

GstStateChangeReturn tgst_set_state(GstElement *element, GstState state) {
    return_value_if_fail(element != NULL, GST_STATE_CHANGE_FAILURE);

    gchar *name = gst_element_get_name(element);

    LOG_D("设置对象 %s 状态为 %s", name, gst_element_state_get_name(state));

    GstStateChangeReturn ret = gst_element_set_state(element, state);
    switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
        LOG_D("对象 %s 状态已成功设置为 %s",
              name,
              gst_element_state_get_name(state));
        break;
    case GST_STATE_CHANGE_FAILURE:
        LOG_D("无法设置对象 %s 状态为 %s",
              name,
              gst_element_state_get_name(state));
        break;
    case GST_STATE_CHANGE_NO_PREROLL:
        LOG_D("对象 %s 状态已设置为 %s (无预渲染)",
              name,
              gst_element_state_get_name(state));
        break;
    case GST_STATE_CHANGE_ASYNC:
        LOG_D("对象 %s 状态正在异步转换到 %s",
              name,
              gst_element_state_get_name(state));
        break;
    }

    g_free(name);
    return ret;
}

gboolean tgst_send_event(GstElement *element, GstEvent *event) {
    GstEventType type = GST_EVENT_TYPE(event);

    gchar *name = gst_element_get_name(element);

    const gchar *event_name = gst_event_type_get_name(type);
    gboolean ret = gst_element_send_event(element, event);

    if (ret == FALSE) {
        LOG_W("对象 %s 发送事件 %s 失败", name, event_name);
    } else {
        LOG_V("对象 %s 发送事件 %s 成功", name, event_name);
    }
    return ret == TRUE;
}

//------------------------------------------------------------------------------

GstPad *tgst_get_static_pad(GstElement *element, const gchar *pad_name) {
    return_value_if_fail(element != NULL && pad_name != NULL, NULL);

    gchar *element_name = gst_element_get_name(element);

    GstPad *pad = gst_element_get_static_pad(element, pad_name);
    if (pad == NULL) {
        LOG_E("获取元素 '%s:%s' 的静态衬垫 '%s' 失败！",
              tgst_get_factory_name(element),
              element_name,
              pad_name);
    } else {
        LOG_V("获取元素 '%s:%s' 的静态衬垫 '%s' 成功。",
              tgst_get_factory_name(element),
              element_name,
              pad_name);
    }

    g_free(element_name);
    return pad;
}

GstPad *tgst_ghost_pad_new(GstPad *pad, const gchar *ghost_name) {
    gchar *pad_name = gst_pad_get_name(pad);
    GstPad *ghost_pad = gst_ghost_pad_new(ghost_name, pad);
    if (ghost_pad == NULL) {
        LOG_E("创建衬垫 '%s' 的幽灵衬垫 %s 失败！", pad_name, ghost_name);
    } else {
        LOG_D("创建衬垫 '%s' 的幽灵衬垫 %s 成功。", pad_name, ghost_name);
    }
    g_free(pad_name);
    return ghost_pad;
}

GstPad *tgst_request_pad_simple(GstElement *element, const gchar *pad_name) {
    return_value_if_fail(element != NULL && pad_name != NULL, NULL);

    const gchar *factory_name = tgst_get_factory_name(element);
    gchar *element_name = gst_element_get_name(element);

    GstPad *pad = gst_element_request_pad_simple(element, pad_name);
    if (pad == NULL) {
        LOG_E("请求元素 '%s:%s' 的衬垫 '%s' 失败！",
              factory_name,
              element_name,
              pad_name);

    } else {
        gchar *new_name = gst_pad_get_name(pad);
        LOG_V("请求元素 '%s:%s' 的衬垫 '%s' 成功。",
              factory_name,
              element_name,
              new_name);
        g_free(new_name);
    }

    g_free(element_name);
    return pad;
}

gboolean tgst_element_add_pad(GstElement *element, GstPad *pad) {
    const gchar *factory_name = tgst_get_factory_name(element);
    gchar *element_name = gst_element_get_name(element);
    gchar *pad_name = gst_pad_get_name(pad);
    gboolean ret = gst_element_add_pad(element, pad);
    if (ret) {
        LOG_D("元素 '%s:%s' 增加衬垫 '%s' 成功。",
              factory_name,
              element_name,
              pad_name);
    } else {
        LOG_E("元素 '%s:%s' 增加衬垫 '%s' 失败！",
              factory_name,
              element_name,
              pad_name);
    }
    g_free(element_name);
    g_free(pad_name);
    return ret;
}

gboolean tgst_is_request_pad(GstPad *pad) {
    return_value_if_fail(pad != NULL, FALSE);

    GstPadTemplate *templ = gst_pad_get_pad_template(pad);
    return templ && GST_PAD_TEMPLATE_PRESENCE(templ) == GST_PAD_REQUEST;
}

void tgst_release_pad(GstElement *element, GstPad *pad) {
    return_if_fail(pad != NULL);

    if (tgst_is_request_pad(pad)) {
        gchar *element_name = gst_element_get_name(element);
        gchar *pad_name = gst_pad_get_name(pad);
        gst_element_release_request_pad(element, pad);
        LOG_V("释放对象 %s 的请求衬垫 %s", element_name, pad_name);
        g_free(element_name);
        g_free(pad_name);
    }
    gst_object_unref(pad);
}

gboolean tgst_pad_link(GstPad *first, GstPad *second) {
    gboolean result = TRUE;

    gchar *first_name = gst_pad_get_name(first);
    gchar *second_name = gst_pad_get_name(second);

    GstPadDirection dir = GST_PAD_DIRECTION(first);

    if (dir == GST_PAD_SRC) {
        // 输出衬垫 (Source Pad)
        GstPadLinkReturn ret = gst_pad_link(first, second);
        if (GST_PAD_LINK_FAILED(ret)) {
            LOG_W("衬垫 '%s' -> 衬垫 '%s' 链接失败！", first_name, second_name);
            result = FALSE;

        } else {
            LOG_V("衬垫 '%s' -> 衬垫 '%s' 链接成功。", first_name, second_name);
        }

    } else if (dir == GST_PAD_SINK) {
        // 输入衬垫 (Sink Pad)
        auto ret = gst_pad_link(second, first);
        if (GST_PAD_LINK_FAILED(ret)) {
            LOG_W("衬垫 '%s' -> 衬垫 '%s' 链接失败！", second_name, first_name);
            result = FALSE;

        } else {
            LOG_V("衬垫 '%s' -> 衬垫 '%s' 链接成功。", second_name, first_name);
        }

    } else {
        LOG_E("衬垫 '%s' 方向未知", first_name);
        result = FALSE;
    }

    g_free(first_name);
    g_free(second_name);

    return result;
}

gboolean tgst_pad_unlink(GstPad *first, GstPad *second) {
    gchar *first_name = gst_pad_get_name(first);
    gchar *second_name = gst_pad_get_name(second);

    gboolean ret = gst_pad_unlink(first, second);

    if (ret == TRUE) {
        LOG_V("衬垫 %s <-> 衬垫 %s 断开链接成功。", first_name, second_name);

    } else {
        LOG_W("衬垫 %s <-> 衬垫 %s 断开链接失败！", first_name, second_name);
    }

    g_free(first_name);
    g_free(second_name);

    return ret;
}

gboolean tgst_request_pad_link(GstElement *src,
                               const gchar *pad_name,
                               GstElement *dst,
                               const gchar *dst_name) {
    GstPad *dst_pad = NULL;
    GstPad *pad = NULL;
    gboolean ret = FALSE;

    // 1. 获取目标静态衬垫（使用完必须 unref）
    dst_pad = tgst_get_static_pad(dst, dst_name);
    goto_error_if_fail(dst_pad != NULL);

    // 2. 请求源动态衬垫（所有权属于 src 元素）
    pad = tgst_request_pad_simple(src, pad_name);
    goto_error_if_fail(pad != NULL);

    // 3. 执行衬垫链接
    ret = tgst_pad_link(pad, dst_pad);

error:
    // 💡 修复 1：无论成功与否，必须平衡掉获取静态衬垫的计数
    if (dst_pad != NULL) {
        gst_object_unref(dst_pad);
    }

    // 💡 修复 2：核心修改！
    // 如果链接成功（ret 为 TRUE），绝对不能 unref 它！它归 src 元素管。
    // 只有在【链接失败】时，才需要将其归还/释放给 src 元素。
    if (!ret && pad != NULL) {
        // GStreamer 官方推荐对于 Request Pad 的失败/清理使用此标准接口
        gst_element_release_request_pad(src, pad);
        gst_object_unref(pad); // 平衡请求时增加的引用计数
    }

    return ret;
}

bool_t
tgst_static_pad_link(GstPad *new_pad, GstElement *dst, const gchar *pad_name) {
    THROW_ERROR_IF_FAIL(dst != NULL, "dst 未初始化");
    gchar *new_pad_name = gst_pad_get_name(new_pad);
    gchar *dst_name = gst_element_get_name(dst);

    bool_t ret = FALSE;
    GstPad *sink_pad = gst_element_get_static_pad(dst, pad_name);
    if (gst_pad_is_linked(sink_pad)) {
        LOG_W("'%s' 的 sink pad 已被连接", dst_name);
        goto cleanup;
    }

    if (gst_pad_link(new_pad, sink_pad) == GST_PAD_LINK_OK) {
        LOG_D("'%s' 到 '%s' 连接成功。", new_pad_name, pad_name);
        ret = TRUE;
    } else {
        LOG_E("'%s' 到 '%s' 连接失败！", new_pad_name, pad_name);
    }

cleanup:
    if (sink_pad) {
        gst_object_unref(sink_pad);
    }
    if (dst_name) {
        g_free(dst_name);
    }
    if (new_pad_name) {
        g_free(new_pad_name);
    }
    return ret;
}

gboolean tgst_add_ghost_pad(GstElement *src,
                            const gchar *ghost_name,
                            GstElement *dst,
                            const gchar *dst_name) {

    GstPad *pad = NULL;
    GstPad *ghost_pad = NULL;
    gboolean ret = FALSE;

    // 1. 获取目标静态衬垫（此时 pad 引用计数 +1）
    pad = tgst_get_static_pad(dst, dst_name);
    goto_error_if_fail(pad != NULL);

    // 2. 创建幽灵衬垫（此时 ghost_pad 引用计数为 1）
    ghost_pad = tgst_ghost_pad_new(pad, ghost_name);
    goto_error_if_fail(ghost_pad != NULL);

    // 3. 将幽灵衬垫加入源元素中
    ret = tgst_element_add_pad(src, ghost_pad);

error:
    // 💡 修复 1：无论成功与否，必须释放获取到的静态衬垫，平衡掉 tgst_get_static_pad 的 +1
    if (pad != NULL) {
        gst_object_unref(pad);
    }

    // 💡 修复 2：只有在【失败】的情况下，才需要手动释放 ghost_pad
    // 如果 ret 为 TRUE（成功），src 元素已经接管了它，绝对不能在这里调用 unref！
    if (!ret && ghost_pad != NULL) {
        gst_object_unref(ghost_pad);
    }

    return ret;
}

void tgst_element_pad_unlink(GstElement *src,
                             GstElement *dst,
                             const gchar *dst_name) {
    GstPad *dst_pad = tgst_get_static_pad(dst, dst_name);
    return_if_fail(dst_pad != NULL);

    GstPad *peer = gst_pad_get_peer(dst_pad);
    if (peer == NULL) {
        gchar *name = gst_element_get_name(dst);
        LOG_W("对象 %s 的衬垫 %s 没有链接其它衬垫", name, dst_name);
        g_free(name);

    } else {
        tgst_pad_unlink(dst_pad, peer);
        tgst_release_pad(src, peer);
    }

    tgst_release_pad(src, dst_pad);
}

void tgst_add_buffer_probe(TGst *gst,
                           GstElement *element,
                           const gchar *sink_name,
                           GstPadProbeCallback cb) {
    GstPad *pad = tgst_get_static_pad(element, sink_name);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, cb, gst, NULL);
    gst_object_unref(pad);
}

void tgst_signal_connect(GstElement *element,
                         const gchar *detailed_signal,
                         GCallback c_handler,
                         gpointer data) {
    if (g_signal_connect((gpointer)element,
                         detailed_signal,
                         G_CALLBACK(c_handler),
                         data) == 0) {
        gchar *name = gst_element_get_name(element);
        THROW_RUNTIME_ERROR("'%s' '%s' 信号连接失败", name, detailed_signal);
    }
}

//==============================================================================
