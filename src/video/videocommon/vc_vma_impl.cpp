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

/* Create a VMA-allocated VkImage.  Returns VK_SUCCESS on success.
   *out_alloc receives the VmaAllocation handle (as void*). */
VkResult
vc_vma_create_image(void *allocator, const VkImageCreateInfo *image_ci,
                    VkImage *out_image, void **out_alloc)
{
    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult      result = vmaCreateImage(static_cast<VmaAllocator>(allocator),
                                          image_ci, &alloc_ci, out_image,
                                          &alloc, nullptr);
    if (result == VK_SUCCESS)
        *out_alloc = static_cast<void *>(alloc);
    else
        *out_alloc = nullptr;

    return result;
}

/* Destroy a VMA-allocated VkImage. */
void
vc_vma_destroy_image(void *allocator, VkImage image, void *alloc)
{
    if (allocator && image != VK_NULL_HANDLE)
        vmaDestroyImage(static_cast<VmaAllocator>(allocator),
                        image, static_cast<VmaAllocation>(alloc));
}

/* Create a VMA-allocated VkBuffer with specified flags.
   *out_alloc receives the VmaAllocation handle (as void*).
   If mapped is non-zero, the buffer will be HOST_VISIBLE + HOST_COHERENT
   with persistent mapping, and *out_mapped receives the mapped pointer. */
VkResult
vc_vma_create_buffer(void *allocator, const VkBufferCreateInfo *buffer_ci,
                     int mapped, VkBuffer *out_buffer, void **out_alloc,
                     void **out_mapped)
{
    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

    if (mapped) {
        alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                       | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocation     alloc = VK_NULL_HANDLE;
    VmaAllocationInfo info  = {};
    VkResult          result = vmaCreateBuffer(static_cast<VmaAllocator>(allocator),
                                               buffer_ci, &alloc_ci, out_buffer,
                                               &alloc, &info);
    if (result == VK_SUCCESS) {
        *out_alloc = static_cast<void *>(alloc);
        if (out_mapped)
            *out_mapped = info.pMappedData;
    } else {
        *out_alloc = nullptr;
        if (out_mapped)
            *out_mapped = nullptr;
    }

    return result;
}

/* Destroy a VMA-allocated VkBuffer. */
void
vc_vma_destroy_buffer(void *allocator, VkBuffer buffer, void *alloc)
{
    if (allocator && buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(static_cast<VmaAllocator>(allocator),
                         buffer, static_cast<VmaAllocation>(alloc));
}

/* Create a host-visible buffer optimized for GPU->CPU readback.
   Uses HOST_ACCESS_RANDOM_READ_BIT for efficient CPU reads of GPU-written data. */
VkResult
vc_vma_create_readback_buffer(void *allocator, const VkBufferCreateInfo *buffer_ci,
                              VkBuffer *out_buffer, void **out_alloc,
                              void **out_mapped)
{
    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                   | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocation     alloc  = VK_NULL_HANDLE;
    VmaAllocationInfo info   = {};
    VkResult          result = vmaCreateBuffer(static_cast<VmaAllocator>(allocator),
                                               buffer_ci, &alloc_ci, out_buffer,
                                               &alloc, &info);
    if (result == VK_SUCCESS) {
        *out_alloc = static_cast<void *>(alloc);
        if (out_mapped)
            *out_mapped = info.pMappedData;
    } else {
        *out_alloc = nullptr;
        if (out_mapped)
            *out_mapped = nullptr;
    }

    return result;
}

} /* extern "C" */
