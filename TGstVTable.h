/*
 * Copyright (C) 2026 Shenzhen Chip Blueprint Technology Co., Ltd.
 * All rights reserved.
 *
 * Authors:
 *     Jianfeng Pu <jianfeng_pu@qq.com>
 *
 * Description:
 *     gst vtable
 */

/**
 * History:
 * ================================================================
 * 2026-09-08 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_VTABLE_H
#define T_GST_VTABLE_H
//------------------------------------------------------------------------------

#include "TGst.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

void tgst_vtable_on_create(TGst *ctx, uint32_t funcs);
void tgst_vtable_on_destroy(TGst *ctx);

ret_t tgst_vtable_on_start(TGst *gst);
ret_t tgst_vtable_on_stop(TGst *gst);
ret_t tgst_vtable_on_paused(TGst *gst, gboolean paused);

ret_t tgst_vtable_on_async_done(TGst *gst);

ret_t tgst_vtable_on_eos(TGst *gst);

ret_t tgst_vtable_on_warning(TGst *gst, GError *err, gchar *debug);
ret_t tgst_vtable_on_error(TGst *gst, GError *err, gchar *debug);

ret_t tgst_vtable_on_stream_collection(TGst *gst, GstStreamCollection *coll);
ret_t tgst_vtable_on_stream_selected(TGst *gst, GstStreamCollection *coll);

ret_t tgst_vtable_on_state_changed(TGst *gst,
                                   GstState old_state,
                                   GstState new_state,
                                   GstState pending_state);
ret_t tgst_vtable_on_stream_status(TGst *gst,
                                   GstStreamStatusType type,
                                   GstElement *owner);

uint32_t tsgt_vtable_get_capture_bps(TGst *gst);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_VTABLE_H
//==============================================================================
