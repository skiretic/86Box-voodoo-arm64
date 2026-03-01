/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VCRenderer -- Qt5 widget that creates a VkSurfaceKHR and
 *          passes it to the VideoCommon GPU thread for swapchain
 *          creation and presentation.
 *
 *          Handles both Voodoo 3D (GPU thread renders via Vulkan) and
 *          VGA passthrough (receives blit signal, uploads pixels to
 *          Vulkan staging buffer, GPU thread presents via swapchain).
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef QT_VCRENDERER_HPP
#define QT_VCRENDERER_HPP

#include "qt_renderercommon.hpp"

#include <QImage>
#include <QWidget>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

/* Vulkan handle types -- we use uintptr_t to avoid pulling Vulkan headers
   into Qt compilation units.  The actual VkSurfaceKHR is a pointer-sized
   opaque handle on all platforms we support. */

class VCRenderer : public QWidget, public RendererCommon {
    Q_OBJECT

public:
    explicit VCRenderer(QWidget *parent = nullptr);
    ~VCRenderer() override;

    /* Called once after the widget is shown. Creates the platform
       surface and hands it to VideoCommon. */
    void initialize();

    /* Teardown handshake: signal GPU thread, wait for swapchain
       destruction, then destroy the VkSurfaceKHR. */
    void finalize() override;

signals:
    void initialized();
    void errorInitializing();

public slots:
    /* Receives blitToRenderer signal from RendererStack.
       Notifies the GPU thread that a VGA frame is ready. */
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    void resizeEvent(QResizeEvent *event) override;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    uint32_t getBytesPerRow() override { return 2048 * 4; }

private:
    uintptr_t m_surface   = 0; /* VkSurfaceKHR cast to uintptr_t. */
    bool      m_finalized = false;

    /* Double-buffered image buffers for VGA passthrough blit.
       Same pattern as SoftwareRenderer: 2048x2048 BGRA8 images. */
    std::array<std::unique_ptr<QImage>, 2> m_images;
    bool m_bufsRegistered = false;
};

#endif /* QT_VCRENDERER_HPP */
