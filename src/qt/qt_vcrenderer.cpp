/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VCRenderer -- Qt5 widget that creates a VkSurfaceKHR for
 *          the VideoCommon GPU thread.
 *
 *          This widget is a thin surface container.  It does NOT do
 *          any Vulkan drawing -- the GPU thread owns the swapchain
 *          and all rendering.  VCRenderer's only jobs:
 *            1. Create a platform VkSurfaceKHR from its native window
 *            2. Pass the surface handle to VideoCommon via atomic
 *            3. Handle resize events (signal GPU thread)
 *            4. Handle teardown (handshake with GPU thread)
 *
 *          IMPORTANT: This file does NOT include any Vulkan headers.
 *          All Vulkan interaction goes through the opaque API in
 *          <86box/videocommon.h> which uses void* and uintptr_t handles.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */

#include "qt_vcrenderer.hpp"

#include <QGuiApplication>
#include <QResizeEvent>
#include <QWindow>

extern "C" {
#include <86box/videocommon.h>
}

/* -------------------------------------------------------------------------- */
/*  Construction / destruction                                                 */
/* -------------------------------------------------------------------------- */

VCRenderer::VCRenderer(QWidget *parent)
    : QWidget(parent)
{
    /* Ensure a native window handle is created immediately. */
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

VCRenderer::~VCRenderer()
{
    finalize();
}

/* -------------------------------------------------------------------------- */
/*  Initialization                                                             */
/* -------------------------------------------------------------------------- */

void
VCRenderer::initialize()
{
    void *ctx_ptr = vc_display_get_ctx();
    if (!ctx_ptr) {
        /* VideoCommon not active yet.  This is normal during startup --
           the deferred init thread may not have completed.  The caller
           can retry later or fall back to SW rendering. */
        emit errorInitializing();
        return;
    }

    WId wid = winId();
    if (!wid) {
        emit errorInitializing();
        return;
    }

    /* Create a VkSurfaceKHR via the opaque VideoCommon API.
       vc_create_surface() handles all platform-specific logic internally
       (Metal on macOS, Win32 on Windows, XCB on Linux). */
    m_surface = vc_create_surface(ctx_ptr, static_cast<uintptr_t>(wid));
    if (m_surface == 0) {
        emit errorInitializing();
        return;
    }

    /* Pass the surface to VideoCommon.  The GPU thread will pick it up
       on its next vc_display_tick() iteration and create the swapchain. */
    vc_display_set_surface_handle(ctx_ptr, m_surface);

    emit initialized();
}

/* -------------------------------------------------------------------------- */
/*  Teardown                                                                   */
/* -------------------------------------------------------------------------- */

void
VCRenderer::finalize()
{
    if (m_finalized)
        return;
    m_finalized = true;

    void *ctx_ptr = vc_display_get_ctx();
    if (ctx_ptr) {
        /* Signal GPU thread to destroy swapchain resources. */
        vc_display_request_teardown_handle(ctx_ptr);

        /* Wait for GPU thread to finish. */
        vc_display_wait_teardown_handle(ctx_ptr);
    }

    /* Now safe to destroy the VkSurfaceKHR. */
    if (m_surface != 0) {
        void *ctx_for_destroy = vc_display_get_ctx();
        if (ctx_for_destroy)
            vc_destroy_surface(ctx_for_destroy, m_surface);
        m_surface = 0;
    }
}

/* -------------------------------------------------------------------------- */
/*  Resize                                                                     */
/* -------------------------------------------------------------------------- */

void
VCRenderer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    void *ctx_ptr = vc_display_get_ctx();
    if (ctx_ptr) {
        int w = static_cast<int>(event->size().width() * devicePixelRatio());
        int h = static_cast<int>(event->size().height() * devicePixelRatio());
        vc_display_signal_resize_handle(ctx_ptr,
                                         static_cast<uint32_t>(w),
                                         static_cast<uint32_t>(h));
    }
}

/* -------------------------------------------------------------------------- */
/*  RendererCommon overrides                                                   */
/* -------------------------------------------------------------------------- */

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
VCRenderer::getBuffers()
{
    /* VCRenderer does not use the blit path.  Return empty. */
    return {};
}
