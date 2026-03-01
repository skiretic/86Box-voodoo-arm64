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
 *          and all rendering.  VCRenderer's jobs:
 *            1. Create a platform VkSurfaceKHR from its native window
 *            2. Pass the surface handle to VideoCommon via atomic
 *            3. Handle resize events (signal GPU thread)
 *            4. Handle teardown (handshake with GPU thread)
 *            5. Provide image buffers for VGA passthrough blit
 *            6. Receive blitToRenderer signal and notify GPU thread
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
#include <86box/86box.h>
#include <86box/video.h>
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

    RendererCommon::parentWidget = parent;

    /* Allocate double-buffered image buffers for VGA passthrough.
       Same format as SoftwareRenderer: 2048x2048 Format_RGB32 (BGRA8). */
    m_images[0] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);
    m_images[1] = std::make_unique<QImage>(QSize(2048, 2048), QImage::Format_RGB32);

    buf_usage = std::vector<std::atomic_flag>(2);
    buf_usage[0].clear();
    buf_usage[1].clear();
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

    /* Register VGA blit buffer pointers with VideoCommon so the GPU
       thread can read from them for VGA passthrough display. */
    if (!m_bufsRegistered) {
        vc_display_set_vga_bufs(ctx_ptr,
                                 m_images[0]->bits(),
                                 m_images[1]->bits());
        m_bufsRegistered = true;
    }

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
/*  VGA passthrough blit                                                       */
/* -------------------------------------------------------------------------- */

void
VCRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    /* Release the previous buffer for the blit thread to reuse. */
    buf_usage[buf_idx ^ 1].clear();

    /* Update source rect for RendererCommon coordinate mapping. */
    auto origSource = source;
    source.setRect(x, y, w, h);
    if (source != origSource)
        onResize(this->width(), this->height());

    /* Notify the GPU thread that a VGA frame is ready.
       The GPU thread will read the pixel data from the image buffer
       and present it via the Vulkan swapchain. */
    void *ctx_ptr = vc_display_get_ctx();
    if (ctx_ptr) {
        vc_display_notify_vga_frame(ctx_ptr, buf_idx, x, y, w, h);
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
    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;

    buffers.push_back(std::make_tuple(m_images[0]->bits(), &buf_usage[0]));
    buffers.push_back(std::make_tuple(m_images[1]->bits(), &buf_usage[1]));

    return buffers;
}
