/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VideoCommon -- VMA (Vulkan Memory Allocator) implementation.
 *          VMA requires C++ compilation; this file provides the single
 *          translation unit for VMA_IMPLEMENTATION and C-callable
 *          wrapper functions for allocator create/destroy.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */

/* VK_NO_PROTOTYPES and platform defines are set by CMakeLists.txt. */
#include "volk.h"

/* VMA implementation -- all VMA functions are compiled here. */
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include "vk_mem_alloc.h"

#include <cstring>

extern "C" {

/* Create a VMA allocator using volk-loaded function pointers.
   Returns the allocator as void* (VmaAllocator), or NULL on failure. */
void *
vc_vma_create(VkInstance instance, VkPhysicalDevice phys_dev, VkDevice device)
{
    VmaVulkanFunctions volk_funcs;
    memset(&volk_funcs, 0, sizeof(volk_funcs));

    volk_funcs.vkGetInstanceProcAddr                   = vkGetInstanceProcAddr;
    volk_funcs.vkGetDeviceProcAddr                     = vkGetDeviceProcAddr;
    volk_funcs.vkGetPhysicalDeviceProperties           = vkGetPhysicalDeviceProperties;
    volk_funcs.vkGetPhysicalDeviceMemoryProperties     = vkGetPhysicalDeviceMemoryProperties;
    volk_funcs.vkAllocateMemory                        = vkAllocateMemory;
    volk_funcs.vkFreeMemory                            = vkFreeMemory;
    volk_funcs.vkMapMemory                             = vkMapMemory;
    volk_funcs.vkUnmapMemory                           = vkUnmapMemory;
    volk_funcs.vkFlushMappedMemoryRanges               = vkFlushMappedMemoryRanges;
    volk_funcs.vkInvalidateMappedMemoryRanges          = vkInvalidateMappedMemoryRanges;
    volk_funcs.vkBindBufferMemory                      = vkBindBufferMemory;
    volk_funcs.vkBindImageMemory                       = vkBindImageMemory;
    volk_funcs.vkGetBufferMemoryRequirements           = vkGetBufferMemoryRequirements;
    volk_funcs.vkGetImageMemoryRequirements            = vkGetImageMemoryRequirements;
    volk_funcs.vkCreateBuffer                          = vkCreateBuffer;
    volk_funcs.vkDestroyBuffer                         = vkDestroyBuffer;
    volk_funcs.vkCreateImage                           = vkCreateImage;
    volk_funcs.vkDestroyImage                          = vkDestroyImage;
    volk_funcs.vkCmdCopyBuffer                         = vkCmdCopyBuffer;
    volk_funcs.vkGetBufferMemoryRequirements2KHR       = vkGetBufferMemoryRequirements2;
    volk_funcs.vkGetImageMemoryRequirements2KHR        = vkGetImageMemoryRequirements2;
    volk_funcs.vkBindBufferMemory2KHR                  = vkBindBufferMemory2;
    volk_funcs.vkBindImageMemory2KHR                   = vkBindImageMemory2;
    volk_funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;

    VmaAllocatorCreateInfo ci = {};
    ci.vulkanApiVersion = VK_API_VERSION_1_2;
    ci.instance         = instance;
    ci.physicalDevice   = phys_dev;
    ci.device           = device;
    ci.pVulkanFunctions = &volk_funcs;

    VmaAllocator allocator = VK_NULL_HANDLE;
    VkResult     result    = vmaCreateAllocator(&ci, &allocator);
    if (result != VK_SUCCESS)
        return nullptr;

    return static_cast<void *>(allocator);
}

/* Destroy a VMA allocator. */
void
vc_vma_destroy(void *allocator)
{
    if (allocator)
        vmaDestroyAllocator(static_cast<VmaAllocator>(allocator));
}

} /* extern "C" */
