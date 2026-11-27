/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     图片查看器
 */

/**
 * History:
 * ================================================================
 * 2026-09-09 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
// #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
// #define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#define TAG "TPictureViewer"
#include <log.h>
//==============================================================================

#include "TPictureViewer.h"

//------------------------------------------------------------------------------

#define TOBJ_NAME TG_TYPE_PICTURE_VIEWER

//==============================================================================

static void viewer_on_create(TGst *gst, uint32_t funcs) {
    return_if_fail(gst != NULL);
    gchar text_buf[256];
    tgst_func_to_str(text_buf, sizeof(text_buf), funcs);
    LOG_V("对象 'Gst:%s'：创建开始 (gst=%p, funcs=%s) ......",
          TOBJ_NAME,
          gst,
          text_buf);

    TG_VTABLE_FUNC(GstPlayback, on_create, (gst, funcs));

    LOG_D("对象 'Gst:%s'：创建成功。", TOBJ_NAME);
}

static void viewer_on_destroy(TGst *gst) {
    return_if_fail(gst != NULL);
    LOG_V("对象 'Gst:%s'：销毁开始 (gst=%p) ......", TOBJ_NAME, gst);

    TG_VTABLE_FUNC(GstPlayback, on_destroy, (gst))

    LOG_D("对象 'Gst:%s'：销毁成功。", TOBJ_NAME);
}

static ret_t viewer_on_start(TGst *gst) {
    LOG_V("对象 'Gst:%s'：启动开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstPlayback, on_start, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：启动失败！", TOBJ_NAME);

    LOG_D("对象 'Gst:%s'：启动成功。", TOBJ_NAME);
    return RET_OK;
}

static ret_t viewer_on_stop(TGst *gst) {
    LOG_V("对象 'Gst:%s'：停止开始 (gst=%p) ......", TOBJ_NAME, gst);

    ret_t ret = RET_OK;
    TG_VTABLE_FUNC_RET(GstPlayback, on_stop, ret, (gst));
    THROW_ERROR_IF_FAIL(ret == RET_OK, "对象 'Gst:%s'：停止失败！", TOBJ_NAME);

    LOG_D("对象 'Gst:%s'：停止成功。", TOBJ_NAME);
    return RET_OK;
}

TG_DECL_VTABLE(PictureViewer) = {
    .size = sizeof(TPictureViewer),
    .type = TG_TYPE_PICTURE_VIEWER,
    .get_parent_vt = TG_GET_PARENT_VTABLE(GstPlayback),

    TG_CALLBACKS_REQUIRED(viewer),
};

//------------------------------------------------------------------------------

TGst *pictureViewerCreate(void) {
    LOG_V("对象 'Gst:%s'：创建开始 () ......", TOBJ_NAME);
    TGst *gst = gstCreate(TG_REF_VTABLE(PictureViewer), TG_FUNC_PICTURE_VIEW);
    THROW_ERROR_IF_FAIL(gst != NULL, "对象 'Gst:%s'：创建失败！", TOBJ_NAME);

    LOG_D("对象 'Gst:%s'：创建成功：%p", TOBJ_NAME, gst);
    return gst;
}

//==============================================================================
