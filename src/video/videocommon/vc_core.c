/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon core -- Vulkan instance/device creation,
 *          capability detection, logging.  VMA allocator creation is
 *          in vc_vma_impl.cpp (C++ required by VMA).
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* volk implementation -- compiled once, here. */
#define VOLK_IMPLEMENTATION
#include "vc_internal.h"

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/videocommon.h>

#include "vc_core.h"
#include "vc_thread.h"
#include "vc_display.h"

/* Global pointer to the active vc_ctx_t, set during voodoo init.
   Used by the Qt VCRenderer to find the context for display integration. */
static _Atomic(vc_ctx_t *) vc_global_ctx = NULL;

/* C-callable VMA wrappers (implemented in vc_vma_impl.cpp). */
extern void *vc_vma_create(VkInstance instance, VkPhysicalDevice phys_dev,
                           VkDevice device);
extern void  vc_vma_destroy(void *allocator);

/* -------------------------------------------------------------------------- */
/*  Logging                                                                    */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_VIDEOCOMMON_LOG
int vc_do_log = ENABLE_VIDEOCOMMON_LOG;

void
vc_log_func(const char *fmt, ...)
{
    va_list ap;

    if (vc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#endif

/* -------------------------------------------------------------------------- */
/*  Validation layer support                                                   */
/* -------------------------------------------------------------------------- */

static int
vc_validation_requested(void)
{
    const char *env = getenv("VC_VALIDATE");
    return (env && env[0] == '1');
}

static int
vc_has_validation_layer(void)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    if (count == 0)
        return 0;

    VkLayerProperties *layers = (VkLayerProperties *) malloc(count * sizeof(VkLayerProperties));
    if (!layers)
        return 0;

    vkEnumerateInstanceLayerProperties(&count, layers);

    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            found = 1;
            break;
        }
    }

    free(layers);
    return found;
}

/* -------------------------------------------------------------------------- */
/*  Physical device selection                                                  */
/* -------------------------------------------------------------------------- */

static int
vc_score_device(VkPhysicalDevice dev)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    if (props.apiVersion < VK_API_VERSION_1_2)
        return -1;

    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 500;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 250;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return 100;
        default:
            return 50;
    }
}

static int
vc_find_graphics_queue(VkPhysicalDevice dev)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, NULL);
    if (count == 0)
        return -1;

    VkQueueFamilyProperties *families = (VkQueueFamilyProperties *) malloc(count * sizeof(VkQueueFamilyProperties));
    if (!families)
        return -1;

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families);

    int result = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            result = (int) i;
            break;
        }
    }

    free(families);
    return result;
}

/* -------------------------------------------------------------------------- */
/*  Extension / feature probing                                                */
/* -------------------------------------------------------------------------- */

static int
vc_device_has_extension(VkPhysicalDevice dev, const char *ext_name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(dev, NULL, &count, NULL);
    if (count == 0)
        return 0;

    VkExtensionProperties *exts = (VkExtensionProperties *) malloc(count * sizeof(VkExtensionProperties));
    if (!exts)
        return 0;

    vkEnumerateDeviceExtensionProperties(dev, NULL, &count, exts);

    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(exts[i].extensionName, ext_name) == 0) {
            found = 1;
            break;
        }
    }

    free(exts);
    return found;
}

static void
vc_detect_capabilities(vc_ctx_t *ctx)
{
    VkPhysicalDevice dev = ctx->physical_device;

    ctx->caps.has_extended_dynamic_state =
        vc_device_has_extension(dev, "VK_EXT_extended_dynamic_state");
    ctx->caps.has_extended_dynamic_state2 =
        vc_device_has_extension(dev, "VK_EXT_extended_dynamic_state2");
    ctx->caps.has_extended_dynamic_state3 =
        vc_device_has_extension(dev, "VK_EXT_extended_dynamic_state3");
    ctx->caps.has_push_descriptor =
        vc_device_has_extension(dev, "VK_KHR_push_descriptor");

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(dev, &features);
    ctx->caps.has_dual_src_blend = features.dualSrcBlend ? 1 : 0;

    VC_LOG("VideoCommon: capabilities: eds=%d eds2=%d eds3=%d push_desc=%d dual_src=%d\n",
           ctx->caps.has_extended_dynamic_state,
           ctx->caps.has_extended_dynamic_state2,
           ctx->caps.has_extended_dynamic_state3,
           ctx->caps.has_push_descriptor,
           ctx->caps.has_dual_src_blend);
}

/* -------------------------------------------------------------------------- */
/*  vc_init / vc_destroy                                                       */
/* -------------------------------------------------------------------------- */

vc_ctx_t *
vc_init(void)
{
    VkResult result;

    result = volkInitialize();
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: volkInitialize failed (%d) -- no Vulkan available\n", result);
        return NULL;
    }

    vc_ctx_t *ctx = (vc_ctx_t *) malloc(sizeof(vc_ctx_t));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(vc_ctx_t));

    /* -------------------------------------------------------------------- */
    /*  VkInstance                                                           */
    /* -------------------------------------------------------------------- */

    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = "86Box VideoCommon";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName        = "VideoCommon";
    app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion         = VK_API_VERSION_1_2;

    const char *inst_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __APPLE__
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
        "VK_KHR_portability_enumeration",
        "VK_KHR_get_physical_device_properties2",
#endif
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if defined(__linux__) && !defined(__ANDROID__)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
    };
    uint32_t inst_ext_count = sizeof(inst_extensions) / sizeof(inst_extensions[0]);

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    int         use_validation   = vc_validation_requested() && vc_has_validation_layer();

    VkInstanceCreateInfo instance_ci;
    memset(&instance_ci, 0, sizeof(instance_ci));
    instance_ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_ci.pApplicationInfo        = &app_info;
    instance_ci.enabledExtensionCount   = inst_ext_count;
    instance_ci.ppEnabledExtensionNames = inst_extensions;
#ifdef __APPLE__
    instance_ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    if (use_validation) {
        instance_ci.enabledLayerCount   = 1;
        instance_ci.ppEnabledLayerNames = &validation_layer;
        VC_LOG("VideoCommon: validation layers enabled\n");
    }

    result = vkCreateInstance(&instance_ci, NULL, &ctx->instance);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateInstance failed (%d)\n", result);
        free(ctx);
        return NULL;
    }

    volkLoadInstance(ctx->instance);

    /* -------------------------------------------------------------------- */
    /*  Physical device selection                                           */
    /* -------------------------------------------------------------------- */

    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, NULL);
    if (dev_count == 0) {
        VC_LOG("VideoCommon: no Vulkan physical devices found\n");
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    VkPhysicalDevice *devices = (VkPhysicalDevice *) malloc(dev_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, devices);

    int best_score = -1;
    int best_idx   = -1;
    for (uint32_t i = 0; i < dev_count; i++) {
        int score = vc_score_device(devices[i]);
        if (score > best_score) {
            if (vc_find_graphics_queue(devices[i]) >= 0) {
                best_score = score;
                best_idx   = (int) i;
            }
        }
    }

    if (best_idx < 0) {
        VC_LOG("VideoCommon: no suitable Vulkan device (need VK 1.2 + graphics queue)\n");
        free(devices);
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    ctx->physical_device = devices[best_idx];
    free(devices);

    VkPhysicalDeviceProperties dev_props;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &dev_props);
    strncpy(ctx->device_name, dev_props.deviceName, sizeof(ctx->device_name) - 1);
    ctx->device_name[sizeof(ctx->device_name) - 1] = '\0';
    ctx->api_version = dev_props.apiVersion;

    ctx->queue_family = (uint32_t) vc_find_graphics_queue(ctx->physical_device);

    vc_detect_capabilities(ctx);

    /* -------------------------------------------------------------------- */
    /*  Logical device                                                      */
    /* -------------------------------------------------------------------- */

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_ci;
    memset(&queue_ci, 0, sizeof(queue_ci));
    queue_ci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_ci.queueFamilyIndex = ctx->queue_family;
    queue_ci.queueCount       = 1;
    queue_ci.pQueuePriorities = &queue_priority;

    const char *dev_extensions[16];
    uint32_t    dev_ext_count = 0;

    dev_extensions[dev_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

#ifdef __APPLE__
    dev_extensions[dev_ext_count++] = "VK_KHR_portability_subset";
#endif

    if (ctx->caps.has_extended_dynamic_state)
        dev_extensions[dev_ext_count++] = "VK_EXT_extended_dynamic_state";
    if (ctx->caps.has_extended_dynamic_state2)
        dev_extensions[dev_ext_count++] = "VK_EXT_extended_dynamic_state2";
    if (ctx->caps.has_extended_dynamic_state3)
        dev_extensions[dev_ext_count++] = "VK_EXT_extended_dynamic_state3";
    if (ctx->caps.has_push_descriptor)
        dev_extensions[dev_ext_count++] = "VK_KHR_push_descriptor";

    /*
     * Use VkPhysicalDeviceFeatures2 with a pNext chain to enable both
     * core features and extension features (EDS 1/2/3).  When using the
     * pNext path, pEnabledFeatures must be NULL on VkDeviceCreateInfo.
     *
     * Build the pNext chain from the tail up, so each struct's pNext
     * points to the next struct in the chain.
     */
    void *features_pnext = NULL;

    /* EDS 3 -- query and enable if extension is present. */
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3_features;
    if (ctx->caps.has_extended_dynamic_state3) {
        memset(&eds3_features, 0, sizeof(eds3_features));
        eds3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        eds3_features.pNext = features_pnext;

        /* Query what the device actually supports for EDS3. */
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3_query;
        memset(&eds3_query, 0, sizeof(eds3_query));
        eds3_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 query2;
        memset(&query2, 0, sizeof(query2));
        query2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query2.pNext = &eds3_query;
        vkGetPhysicalDeviceFeatures2(ctx->physical_device, &query2);

        /* Enable all supported EDS3 features. */
        eds3_features.extendedDynamicState3PolygonMode         = eds3_query.extendedDynamicState3PolygonMode;
        eds3_features.extendedDynamicState3RasterizationSamples = eds3_query.extendedDynamicState3RasterizationSamples;
        eds3_features.extendedDynamicState3ColorBlendEnable    = eds3_query.extendedDynamicState3ColorBlendEnable;
        eds3_features.extendedDynamicState3ColorBlendEquation  = eds3_query.extendedDynamicState3ColorBlendEquation;
        eds3_features.extendedDynamicState3ColorWriteMask      = eds3_query.extendedDynamicState3ColorWriteMask;
        eds3_features.extendedDynamicState3DepthClampEnable    = eds3_query.extendedDynamicState3DepthClampEnable;
        eds3_features.extendedDynamicState3AlphaToOneEnable    = eds3_query.extendedDynamicState3AlphaToOneEnable;

        features_pnext = &eds3_features;
    }

    /* EDS 2 -- query and enable if extension is present. */
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2_features;
    if (ctx->caps.has_extended_dynamic_state2) {
        memset(&eds2_features, 0, sizeof(eds2_features));
        eds2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        eds2_features.pNext = features_pnext;

        /* Query what the device actually supports for EDS2. */
        VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2_query;
        memset(&eds2_query, 0, sizeof(eds2_query));
        eds2_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 query2;
        memset(&query2, 0, sizeof(query2));
        query2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query2.pNext = &eds2_query;
        vkGetPhysicalDeviceFeatures2(ctx->physical_device, &query2);

        /* Enable all supported EDS2 features. */
        eds2_features.extendedDynamicState2              = eds2_query.extendedDynamicState2;
        eds2_features.extendedDynamicState2LogicOp       = eds2_query.extendedDynamicState2LogicOp;
        eds2_features.extendedDynamicState2PatchControlPoints = eds2_query.extendedDynamicState2PatchControlPoints;

        features_pnext = &eds2_features;
    }

    /* EDS 1 -- query and enable if extension is present. */
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds1_features;
    if (ctx->caps.has_extended_dynamic_state) {
        memset(&eds1_features, 0, sizeof(eds1_features));
        eds1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        eds1_features.pNext = features_pnext;

        /* Query what the device actually supports for EDS1. */
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds1_query;
        memset(&eds1_query, 0, sizeof(eds1_query));
        eds1_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 query2;
        memset(&query2, 0, sizeof(query2));
        query2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query2.pNext = &eds1_query;
        vkGetPhysicalDeviceFeatures2(ctx->physical_device, &query2);

        eds1_features.extendedDynamicState = eds1_query.extendedDynamicState;

        features_pnext = &eds1_features;
    }

    /* Core features via VkPhysicalDeviceFeatures2 (required when using pNext). */
    VkPhysicalDeviceFeatures2 features2;
    memset(&features2, 0, sizeof(features2));
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = features_pnext;
    if (ctx->caps.has_dual_src_blend)
        features2.features.dualSrcBlend = VK_TRUE;

    VkDeviceCreateInfo device_ci;
    memset(&device_ci, 0, sizeof(device_ci));
    device_ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_ci.pNext                   = &features2;
    device_ci.queueCreateInfoCount    = 1;
    device_ci.pQueueCreateInfos       = &queue_ci;
    device_ci.enabledExtensionCount   = dev_ext_count;
    device_ci.ppEnabledExtensionNames = dev_extensions;
    device_ci.pEnabledFeatures        = NULL; /* Must be NULL when using VkPhysicalDeviceFeatures2 in pNext. */

    result = vkCreateDevice(ctx->physical_device, &device_ci, NULL, &ctx->device);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateDevice failed (%d)\n", result);
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    volkLoadDevice(ctx->device);

    vkGetDeviceQueue(ctx->device, ctx->queue_family, 0, &ctx->queue);

    /* -------------------------------------------------------------------- */
    /*  VMA allocator (C++ implementation in vc_vma_impl.cpp)               */
    /* -------------------------------------------------------------------- */

    ctx->allocator = vc_vma_create(ctx->instance, ctx->physical_device, ctx->device);
    if (!ctx->allocator) {
        VC_LOG("VideoCommon: VMA allocator creation failed\n");
        vkDestroyDevice(ctx->device, NULL);
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    VC_LOG("VideoCommon: Vulkan 1.2 initialized on '%s' (API %u.%u.%u)\n",
           ctx->device_name,
           VK_API_VERSION_MAJOR(ctx->api_version),
           VK_API_VERSION_MINOR(ctx->api_version),
           VK_API_VERSION_PATCH(ctx->api_version));

    return ctx;
}

void
vc_destroy(vc_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* Wait for all GPU work to complete before destroying anything.
       This is essential once Phase 2+ submits actual GPU commands. */
    if (ctx->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx->device);

    if (ctx->allocator) {
        vc_vma_destroy(ctx->allocator);
        ctx->allocator = NULL;
    }

    if (ctx->device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
    }

    if (ctx->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = VK_NULL_HANDLE;
    }

    VC_LOG("VideoCommon: destroyed\n");
    free(ctx);
}

/* -------------------------------------------------------------------------- */
/*  Voodoo integration hooks                                                   */
/* -------------------------------------------------------------------------- */

/* Platform yield for spin-wait in init thread. */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <sched.h>
#endif

#include "vc_gpu_state.h"

/* Include vid_voodoo_common.h for voodoo_t access in the integration hooks.
   This requires the same header chain as vid_voodoo.c. */
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>

/* Background thread function for deferred Vulkan init.
 * Runs off the emulation path so that Glide's PCI + register probing
 * isn't disrupted by a long-running Vulkan setup. */
static void
vc_voodoo_init_thread(void *voodoo_ptr)
{
    voodoo_t *voodoo = (voodoo_t *) voodoo_ptr;

    VC_LOG("VideoCommon: vc_voodoo_init -- GPU renderer requested\n");

    vc_ctx_t *ctx = vc_init();
    if (!ctx) {
        VC_LOG("VideoCommon: Vulkan init failed, falling back to SW renderer\n");
        voodoo->use_gpu_renderer = 0;
        return;
    }

    /* Store back-pointer and divert_to_gpu_ptr BEFORE starting the GPU
       thread.  Thread creation provides a happens-before edge, so the
       GPU thread is guaranteed to see these values during its init. */
    ctx->voodoo_ptr = voodoo;
    ctx->divert_to_gpu_ptr = &voodoo->vc_divert_to_gpu;

    if (vc_start_gpu_thread(ctx) != 0) {
        VC_LOG("VideoCommon: GPU thread start failed, falling back to SW renderer\n");
        vc_destroy(ctx);
        voodoo->use_gpu_renderer = 0;
        return;
    }

    voodoo->vc_ctx = ctx;
    atomic_store_explicit(&vc_global_ctx, ctx, memory_order_release);

    /* Wait for the GPU thread to finish init and publish render_data.
       The GPU thread sets ctx->running = 1 after vc_gpu_thread_init(). */
    while (!atomic_load_explicit(&ctx->running, memory_order_acquire)) {
        /* Yield to avoid spinning hard. */
#if defined(_WIN32)
        SwitchToThread();
#else
        sched_yield();
#endif
    }

    VC_LOG("VideoCommon: vc_voodoo_init complete -- GPU renderer active\n");

    /* Tell the Qt UI to switch to VCRenderer now that we are ready.
       This is a no-op if vc_notify_renderer_ready() has not been
       compiled in (non-Qt builds). */
    /* Renderer switch is deferred to first swap command.
       Switching here (during init) causes VCRenderer's VGA passthrough
       to interfere with Glide hardware detection -- the driver probes
       registers and reads framebuffer before rendering starts.
       See vc_thread.c VC_CMD_SWAP handler for the deferred call. */
}

void
vc_voodoo_init(void *voodoo_ptr)
{
    voodoo_t *voodoo = (voodoo_t *) voodoo_ptr;
    if (!voodoo || !voodoo->use_gpu_renderer)
        return;

    /* Spawn Vulkan init on a background thread so that device init
     * returns immediately and guest-side hardware detection isn't
     * blocked.  The triangle path checks vc_ctx and falls through
     * to the SW rasterizer until init completes. */
    voodoo->vc_init_thread = thread_create(vc_voodoo_init_thread, voodoo);
}

void
vc_voodoo_close(void *voodoo_ptr)
{
    voodoo_t *voodoo = (voodoo_t *) voodoo_ptr;
    if (!voodoo)
        return;

    /* Wait for the deferred init thread to finish before tearing down. */
    if (voodoo->vc_init_thread) {
        thread_wait(voodoo->vc_init_thread);
        voodoo->vc_init_thread = NULL;
    }

    if (!voodoo->vc_ctx)
        return;

    VC_LOG("VideoCommon: vc_voodoo_close\n");

    atomic_store_explicit(&vc_global_ctx, NULL, memory_order_release);

    vc_ctx_t *ctx = (vc_ctx_t *) voodoo->vc_ctx;
    vc_stop_gpu_thread(ctx);
    vc_destroy(ctx);
    voodoo->vc_ctx = NULL;
    voodoo->vc_divert_to_gpu = 0;
}

/* -------------------------------------------------------------------------- */
/*  Platform surface creation -- called from Qt GUI thread                     */
/* -------------------------------------------------------------------------- */

#ifdef __APPLE__
extern void *vc_get_metal_layer(void *ns_view_ptr);
#endif

uintptr_t
vc_create_surface(void *ctx_ptr, uintptr_t native_handle)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    if (!ctx || ctx->instance == VK_NULL_HANDLE)
        return 0;

#if defined(__APPLE__)
    /* macOS: native_handle is an NSView*. */
    void *ns_view = (void *) native_handle;
    void *metal_layer = vc_get_metal_layer(ns_view);
    if (!metal_layer) {
        VC_LOG("VideoCommon: vc_get_metal_layer returned NULL\n");
        return 0;
    }

    VkMetalSurfaceCreateInfoEXT metal_ci;
    memset(&metal_ci, 0, sizeof(metal_ci));
    metal_ci.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    metal_ci.pLayer = metal_layer;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = vkCreateMetalSurfaceEXT(ctx->instance, &metal_ci,
                                               NULL, &surface);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateMetalSurfaceEXT failed (%d)\n", result);
        return 0;
    }
    return (uintptr_t) surface;

#elif defined(_WIN32)
    /* Windows: native_handle is an HWND. */
    VkWin32SurfaceCreateInfoKHR win32_ci;
    memset(&win32_ci, 0, sizeof(win32_ci));
    win32_ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    win32_ci.hwnd      = (HWND) native_handle;
    win32_ci.hinstance = GetModuleHandle(NULL);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = vkCreateWin32SurfaceKHR(ctx->instance, &win32_ci,
                                               NULL, &surface);
    if (result != VK_SUCCESS) {
        VC_LOG("VideoCommon: vkCreateWin32SurfaceKHR failed (%d)\n", result);
        return 0;
    }
    return (uintptr_t) surface;

#elif defined(__linux__) && !defined(__ANDROID__)
    /* Linux: native_handle packs xcb_window_t.
       For XCB, we also need the xcb_connection_t which must be passed
       separately.  For now, return 0 -- Linux surface creation requires
       the connection handle which will be set up in a later phase. */
    (void) native_handle;
    VC_LOG("VideoCommon: Linux surface creation not yet implemented\n");
    return 0;

#else
    (void) native_handle;
    return 0;
#endif
}

void
vc_destroy_surface(void *ctx_ptr, uintptr_t surface)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    if (!ctx || ctx->instance == VK_NULL_HANDLE || surface == 0)
        return;

    vkDestroySurfaceKHR(ctx->instance, (VkSurfaceKHR) surface, NULL);
}

/* -------------------------------------------------------------------------- */
/*  Display integration -- opaque wrappers for Qt VCRenderer                   */
/* -------------------------------------------------------------------------- */

void *
vc_display_get_ctx(void)
{
    return (void *) atomic_load_explicit(&vc_global_ctx, memory_order_acquire);
}

uintptr_t
vc_display_get_instance(void)
{
    vc_ctx_t *ctx = atomic_load_explicit(&vc_global_ctx, memory_order_acquire);
    if (!ctx)
        return 0;
    return (uintptr_t) ctx->instance;
}

void
vc_display_set_surface_handle(void *ctx_ptr, uintptr_t surface)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    vc_display_set_surface(ctx, (VkSurfaceKHR) surface);
}

void
vc_display_signal_resize_handle(void *ctx_ptr, uint32_t width, uint32_t height)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    vc_display_signal_resize(ctx, width, height);
}

void
vc_display_request_teardown_handle(void *ctx_ptr)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    vc_display_request_teardown(ctx);
}

void
vc_display_wait_teardown_handle(void *ctx_ptr)
{
    vc_ctx_t *ctx = (vc_ctx_t *) ctx_ptr;
    vc_display_wait_teardown(ctx);
}
