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
 * 2026-09-15 Pu Jianfeng <jianfeng_pu@qq.com> created
 *
 */
//==============================================================================
#ifndef T_GST_HELPER_H
#define T_GST_HELPER_H
//------------------------------------------------------------------------------

#include "TGstTypesDef.h"

//------------------------------------------------------------------------------
BEGIN_C_DECLS
//==============================================================================

const gchar *TG_GetStreamStatusName(GstStreamStatusType type);

const gchar *tgst_get_factory_name(GstElement *element);

void tgst_element_factory_make(GstElement **element,
                               const gchar *plugins,
                               const gchar *name);
void tgst_bin_new(GstElement **bin, const gchar *name);

//------------------------------------------------------------------------------

GstStateChangeReturn tgst_set_state(GstElement *element, GstState state);

gboolean tgst_send_event(GstElement *element, GstEvent *event);

//------------------------------------------------------------------------------

GstPad *tgst_get_static_pad(GstElement *element, const gchar *pad_name);

GstPad *tgst_ghost_pad_new(GstPad *pad, const gchar *ghost_name);

gboolean tgst_element_add_pad(GstElement *element, GstPad *pad);

GstPad *tgst_request_pad_simple(GstElement *element, const gchar *pad_name);

gboolean tgst_is_request_pad(GstPad *pad);

void tgst_release_pad(GstElement *element, GstPad *pad);

gboolean tgst_pad_link(GstPad *first, GstPad *second);

gboolean tgst_pad_unlink(GstPad *first, GstPad *second);

gboolean tgst_request_pad_link(GstElement *src,
                               const gchar *pad_name,
                               GstElement *dst,
                               const gchar *dst_name);

bool_t
tgst_static_pad_link(GstPad *new_pad, GstElement *dst, const gchar *dst_name);

gboolean tgst_add_ghost_pad(GstElement *src,
                            const gchar *ghost_name,
                            GstElement *dst,
                            const gchar *dst_name);

void tgst_element_pad_unlink(GstElement *src,
                             GstElement *dst,
                             const gchar *dst_name);

void tgst_add_buffer_probe(TGst *gst,
                           GstElement *element,
                           const gchar *sink_name,
                           GstPadProbeCallback cb);

void tgst_signal_connect(GstElement *element,
                         const gchar *detailed_signal,
                         GCallback c_handler,
                         gpointer data);

//==============================================================================
END_C_DECLS
//------------------------------------------------------------------------------
#endif // T_GST_HELPER_H
//==============================================================================
