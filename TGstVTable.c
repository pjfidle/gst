/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     gstreamer-1.0 core
 */

/**
 * History:
 * ================================================================
 * 2026-09-01 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
#define TAG "TGstVTable"
#include <log.h>
//==============================================================================

#include "TGstVTable.h"

//------------------------------------------------------------------------------

#define tgst_vtable_func(gst, func, arges_list)                                \
    {                                                                          \
        const TGstVTable *iter = gst->vt;                                      \
        do {                                                                   \
            printf("tgst_vtable_func: type=%s\n", iter->type);                 \
            if (iter->func != NULL) {                                          \
                iter->func arges_list;                                         \
                break;                                                         \
            }                                                                  \
            iter = tgst_get_parent_vtable(iter);                               \
        } while (iter != NULL);                                                \
    }

#define tgst_vtable_func_ret(gst, func, ret, arges_list)                       \
    {                                                                          \
        const TGstVTable *iter = gst->vt;                                      \
        do {                                                                   \
            if (iter->func != NULL) {                                          \
                ret = iter->func arges_list;                                   \
                break;                                                         \
            }                                                                  \
            iter = tgst_get_parent_vtable(iter);                               \
        } while (iter != NULL);                                                \
    }

//==============================================================================

inline static const TGstVTable *tgst_get_parent_vtable(const TGstVTable *vt) {
    return (vt != NULL && vt->get_parent_vt != NULL) ? vt->get_parent_vt()
                                                     : NULL;
}

//------------------------------------------------------------------------------

void tgst_vtable_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    tgst_vtable_func(gst, on_create, (gst, funcs));
}

void tgst_vtable_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    tgst_vtable_func(gst, on_destroy, (gst));
}

ret_t tgst_vtable_on_start(TGst *gst) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_start, ret, (gst));
    return ret;
}

ret_t tgst_vtable_on_stop(TGst *gst) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_stop, ret, (gst));
    return ret;
}

ret_t tgst_vtable_on_paused(TGst *gst, gboolean paused) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_paused, ret, (gst, paused));
    return ret;
}

ret_t tgst_vtable_on_async_done(TGst *gst) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_async_done, ret, (gst));
    return ret;
}

ret_t tgst_vtable_on_eos(TGst *gst) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_eos, ret, (gst));
    return ret;
}

ret_t tgst_vtable_on_warning(TGst *gst, GError *err, gchar *debug) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_warning, ret, (gst, err, debug));
    return ret;
}

ret_t tgst_vtable_on_error(TGst *gst, GError *err, gchar *debug) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_error, ret, (gst, err, debug));
    return ret;
}

ret_t tgst_vtable_on_stream_collection(TGst *gst, GstStreamCollection *coll) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_stream_collection, ret, (gst, coll));
    return ret;
}

ret_t tgst_vtable_on_stream_selected(TGst *gst, GstStreamCollection *coll) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_stream_selected, ret, (gst, coll));
    return ret;
}

ret_t tgst_vtable_on_state_changed(TGst *gst,
                                   GstState old_state,
                                   GstState new_state,
                                   GstState pending_state) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst,
                         on_state_changed,
                         ret,
                         (gst, old_state, new_state, pending_state));
    return ret;
}

ret_t tgst_vtable_on_stream_status(TGst *gst,
                                   GstStreamStatusType type,
                                   GstElement *owner) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_stream_status, ret, (gst, type, owner));
    return ret;
}

uint32_t tsgt_vtable_get_capture_bps(TGst *gst) {
    ret_t ret = RET_NOT_IMPL;
    return_value_if_fail(gst != NULL, RET_BAD_PARAMS);
    tgst_vtable_func_ret(gst, on_get_capture_bps, ret, (gst));
    return ret;
}

//------------------------------------------------------------------------------

TG_DECL_VTABLE(Gst) = {
    .size = sizeof(TGst),
    .type = TG_TYPE_NONE,
    .get_parent_vt = NULL,
    .on_create = NULL,
    .on_destroy = NULL,
    .on_start = NULL,
    .on_stop = NULL,
    .on_paused = NULL,
    .on_async_done = NULL,
    .on_eos = NULL,
    .on_warning = NULL,
    .on_error = NULL,
    .on_tags = NULL,
    .on_stream_collection = NULL,
    .on_stream_selected = NULL,
    .on_state_changed = NULL,
    .on_stream_status = NULL,
    .on_get_capture_bps = NULL,
};

//==============================================================================
