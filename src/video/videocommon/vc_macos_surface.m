/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon macOS surface helper -- CAMetalLayer extraction
 *          from NSView for Vulkan VK_EXT_metal_surface.
 *
 *          This file is Objective-C (.m) because it accesses Cocoa and
 *          QuartzCore APIs that are not available from C.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void *
vc_get_metal_layer(void *ns_view_ptr)
{
    NSView *view = (__bridge NSView *) ns_view_ptr;

    /* If the view already has a CAMetalLayer, return it. */
    if ([view.layer isKindOfClass:[CAMetalLayer class]])
        return (__bridge void *) view.layer;

    /* Otherwise, make the view layer-backed with a CAMetalLayer.
       Per MoltenVK docs, the delegate should be the containing NSView
       for optimal swapchain and refresh timing across displays. */
    [view setWantsLayer:YES];
    CAMetalLayer *layer = [CAMetalLayer layer];
    [view setLayer:layer];
    layer.delegate = (id<CALayerDelegate>) view;

    return (__bridge void *) layer;
}
