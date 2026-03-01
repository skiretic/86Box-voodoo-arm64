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
 *          This is a thin surface container.  All Vulkan drawing is
 *          done by the GPU thread; VCRenderer does NOT submit any
 *          Vulkan commands.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef QT_VCRENDERER_HPP
#define QT_VCRENDERER_HPP

#include "qt_renderercommon.hpp"

#include <QWidget>
#include <atomic>
#include <cstdint>
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

protected:
    void resizeEvent(QResizeEvent *event) override;

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    uint32_t getBytesPerRow() override { return 0; }

private:
    uintptr_t m_surface   = 0; /* VkSurfaceKHR cast to uintptr_t. */
    bool      m_finalized = false;
};

#endif /* QT_VCRENDERER_HPP */
