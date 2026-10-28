#!/usr/bin/env python3
"""The arrow the Vulkan overlay copies into a swapchain image: the runs it
records, where each one lands, the pixels it puts there, and that the present
waits for the copy (after the keyboard's).

The code is taken from dlls/win32u/vulkan.c as it is and runs against mocks:
the sandbox has no Vulkan, so this is the only way the copy is run outside the
Switch. The OpenGL one (dlls/win32u/winnx_opengl.c) is checked by
tests/check_cursor_overlay.py.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
vk = (root / "dlls/win32u/vulkan.c").read_text()


def between(source, start, end):
    assert start in source and end in source, start
    return source[source.index(start):source.index(end)]


fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pointer_overlay.h"

typedef int32_t VkResult;
typedef uint64_t VkDeviceSize, VkDeviceMemory, VkBuffer, VkImage, VkSemaphore, VkCommandBuffer,
                 VkCommandPool, VkSwapchainKHR, VkQueue, VkDevice, VkPipelineStageFlags;
typedef uint32_t uint32_t_mock;
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define VK_NULL_HANDLE 0
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 1
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 2
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 12
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 13
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 3
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 4
#define VK_ACCESS_MEMORY_WRITE_BIT 5
#define VK_ACCESS_TRANSFER_WRITE_BIT 6
#define VK_QUEUE_FAMILY_IGNORED 7
#define VK_IMAGE_ASPECT_COLOR_BIT 8
#define VK_PIPELINE_STAGE_ALL_COMMANDS_BIT 9
#define VK_PIPELINE_STAGE_TRANSFER_BIT 10
#define VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT 11
#define VK_TRUE 1

typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct { uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef struct { uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct { VkDeviceSize bufferOffset; uint32_t bufferRowLength, bufferImageHeight;
                 VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent; } VkBufferImageCopy;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; VkImage image;
                 VkImageSubresourceRange subresourceRange; int32_t oldLayout, newLayout;
                 uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex;
                 uint32_t srcAccessMask, dstAccessMask; } VkImageMemoryBarrier;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; } VkCommandBufferBeginInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t waitSemaphoreCount;
                 const VkSemaphore *pWaitSemaphores; const VkPipelineStageFlags *pWaitDstStageMask;
                 uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers;
                 uint32_t signalSemaphoreCount; const VkSemaphore *pSignalSemaphores; } VkSubmitInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t waitSemaphoreCount;
                 const VkSemaphore *pWaitSemaphores; uint32_t swapchainCount;
                 const VkSwapchainKHR *pSwapchains; const uint32_t *pImageIndices; } VkPresentInfoKHR;
typedef struct { uint32_t queueFamilyIndex; } VkQueueFamilyProperties_mock;

/* dlls/win32u/vulkan.c */
#define BOOL int
#define TRUE 1
#define FALSE 0

/* The pool the present allocates from, as vulkan.c has it. */
typedef uint64_t UINT64;
#define max(a, b) ((a) > (b) ? (a) : (b))
#define ROUND_SIZE(size, mask) (((size) + (mask)) & ~(size_t)(mask))
struct mempool
{
    struct mempool *next;
    size_t mem_used;
    char mem[2048];
};

struct vulkan_device;
struct vulkan_queue;
struct swapchain
{
    VkExtent2D nx_extent;
    BOOL nx_osk_usable;
    struct nx_cursor_overlay *nx_cursor;
};
struct vulkan_queue
{
    struct vulkan_device *device;
    struct { VkQueue queue; } host;
    struct { uint32_t queueFamilyIndex; } info;
};

struct vulkan_device
{
    struct { VkDevice device; } host;
    VkResult (*p_vkResetCommandBuffer)(VkCommandBuffer, uint32_t);
    VkResult (*p_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo *);
    void (*p_vkCmdPipelineBarrier)(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, uint32_t,
                                   uint32_t, const void *, uint32_t, const void *, uint32_t, const VkImageMemoryBarrier *);
    void (*p_vkCmdCopyBufferToImage)(VkCommandBuffer, VkBuffer, VkImage, int32_t, uint32_t, const VkBufferImageCopy *);
    VkResult (*p_vkEndCommandBuffer)(VkCommandBuffer);
    VkResult (*p_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo *, VkSemaphore);
    void (*p_vkDestroySemaphore)(VkDevice, VkSemaphore, const void *);
    void (*p_vkDestroyBuffer)(VkDevice, VkBuffer, const void *);
    void (*p_vkFreeMemory)(VkDevice, VkDeviceMemory, const void *);
    void (*p_vkDestroyCommandPool)(VkDevice, VkCommandPool, const void *);
};

/* The swapchain's side of it, stubbed below; the arrow's is the real thing. */
static BOOL nx_cursor_prepare( struct vulkan_device *device, struct swapchain *swapchain, uint32_t family,
                              int width, int height );
static struct swapchain *swapchain_from_handle( VkSwapchainKHR handle );

/* Weak in vulkan.c: a runtime without the arrow leaves the overlay doing
 * nothing rather than not linking. */
extern int wine_nx_cursor_panel( int *x, int *y ) __attribute__((weak));
extern void nx_vk_trace( const char *format, ... ) __attribute__((weak));
'''

implementation = (between(vk, "static void mem_free(", "struct instance\n{") +
                  between(vk, "struct nx_cursor_image\n{", "static BOOL nx_cursor_prepare(") +
                  between(vk, "static void nx_cursor_present(", "static VkResult win32u_vkQueuePresentKHR(").rsplit("#endif", 1)[0])

mocks = r'''
/* What the program's frame has in it: the image the copy is recorded against,
 * and the arrow's buffer made of pixels only the copy reads. */
static struct swapchain mock_swapchain;
static struct vulkan_queue mock_queue;
static struct vulkan_device mock_device;

static struct
{
    uint32_t resets, begins, ends, barriers, copies, submits;
    VkImage image;
    VkBuffer buffer;
    int32_t layout;
    uint32_t stage_src, stage_dst;
    int32_t barrier_from[2], barrier_to[2];   /* in, then back out */
    unsigned int regions;
    VkBufferImageCopy region[POINTER_OVERLAY_MAX_RUNS];
    VkSemaphore signalled;
    uint32_t waits;
    VkSemaphore waited;
    uint32_t destroyed_semaphores, destroyed_buffers, freed_memory, destroyed_pools;
} recorded;

static int cursor_panel_x = 100, cursor_panel_y = 50, cursor_visible = 1;
static int prepare_calls, prepare_width, prepare_height;

int wine_nx_cursor_panel( int *x, int *y )
{
    if (!cursor_visible) return 0;
    *x = cursor_panel_x;
    *y = cursor_panel_y;
    return 1;
}

/* The swapchain's own set-up is checked by check_gl_surfaces.py's swapchain
 * work; here only that the arrow asks for the picture's scale. */
static BOOL nx_cursor_prepare( struct vulkan_device *device, struct swapchain *swapchain, uint32_t family,
                              int width, int height )
{
    struct nx_cursor_overlay *cursor;
    uint32_t i;

    /* Made again only when the arrow's own rectangle changes: the arrow moves
     * on every present and the buffers must not follow it. */
    if ((cursor = swapchain->nx_cursor))
    {
        if (cursor->width == width && cursor->height == height) return TRUE;
        for (i = 0; i < cursor->count; i++) free( cursor->per[i].pixels );
        free( cursor->per );
        free( cursor->images );
        free( cursor );
    }
    prepare_calls++;
    prepare_width = width;
    prepare_height = height;
    if (!(cursor = swapchain->nx_cursor = calloc( 1, sizeof(*cursor) ))) return FALSE;
    cursor->count = 2;
    cursor->pool = 4000;
    cursor->width = width;
    cursor->height = height;
    cursor->images = calloc( cursor->count, sizeof(*cursor->images) );
    cursor->per = calloc( cursor->count, sizeof(*cursor->per) );
    cursor->images[0] = 1000;
    cursor->images[1] = 1001;
    for (i = 0; i < cursor->count; i++)
    {
        cursor->per[i].cmd = 2000 + i;
        cursor->per[i].buffer = 3000 + i;
        cursor->per[i].pixels = calloc( (size_t)width * height, 4 );
    }
    return TRUE;
}

static VkResult mock_reset( VkCommandBuffer cmd, uint32_t flags ) { recorded.resets++; return VK_SUCCESS; }
static VkResult mock_begin( VkCommandBuffer cmd, const VkCommandBufferBeginInfo *info ) { recorded.begins++; return VK_SUCCESS; }
static void mock_pipeline_barrier( VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                   uint32_t flags, uint32_t dependencies, const void *deps,
                                   uint32_t buffers, const void *barriers, uint32_t count,
                                   const VkImageMemoryBarrier *image_barriers )
{
    assert(count == 1);
    if (recorded.barriers >= 2) { printf("barriers %u\n", recorded.barriers); abort(); }
    recorded.barrier_from[recorded.barriers] = image_barriers[0].oldLayout;
    recorded.barrier_to[recorded.barriers] = image_barriers[0].newLayout;
    recorded.barriers++;
    recorded.stage_src = src;
    recorded.stage_dst = dst;
}
static void mock_copy( VkCommandBuffer cmd, VkBuffer buffer, VkImage image, int32_t layout,
                       uint32_t count, const VkBufferImageCopy *regions )
{
    uint32_t i;

    assert(count <= POINTER_OVERLAY_MAX_RUNS);
    recorded.copies++;
    recorded.buffer = buffer;
    recorded.image = image;
    recorded.layout = layout;
    recorded.regions = count;
    for (i = 0; i < count; i++) recorded.region[i] = regions[i];
}
static VkResult mock_end( VkCommandBuffer cmd ) { recorded.ends++; return VK_SUCCESS; }
static VkResult mock_submit( VkQueue queue, uint32_t count, const VkSubmitInfo *submits, VkSemaphore fence )
{
    assert(count == 1 && submits[0].commandBufferCount == 1);
    assert(submits[0].pWaitDstStageMask);
    recorded.submits++;
    recorded.waits = submits[0].waitSemaphoreCount;
    recorded.waited = submits[0].waitSemaphoreCount ? submits[0].pWaitSemaphores[0] : 0;
    recorded.signalled = submits[0].pSignalSemaphores[0];
    return VK_SUCCESS;
}

static void mock_destroy_semaphore( VkDevice device, VkSemaphore semaphore, const void *allocator )
{
    recorded.destroyed_semaphores++;
}
static void mock_destroy_buffer( VkDevice device, VkBuffer buffer, const void *allocator )
{
    recorded.destroyed_buffers++;
}
static void mock_free_memory( VkDevice device, VkDeviceMemory memory, const void *allocator )
{
    recorded.freed_memory++;
}
static void mock_destroy_pool( VkDevice device, VkCommandPool pool, const void *allocator )
{
    recorded.destroyed_pools++;
}

static struct swapchain *swapchain_from_handle( VkSwapchainKHR handle ) { return &mock_swapchain; }
/* The pool the present allocates its submit and copy regions from, freed when
 * the present is over (the caller's mem_free). */
static struct mempool mock_pool = {0};
'''

checks = r'''
static void check_recorded( uint32_t index, int px, int py, uint32_t width, uint32_t height )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    struct nx_cursor_overlay *cursor = mock_swapchain.nx_cursor;
    int count = pointer_overlay_runs( px, py, 1280, 720, width, height, runs ), i;
    int origin_x = (int)((long long)px * width / 1280), origin_y = (int)((long long)py * height / 720);

    assert(count > 0 && (uint32_t)count == recorded.regions);
    assert(recorded.image == cursor->images[index]);
    assert(recorded.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];
        const VkBufferImageCopy *region = &recorded.region[i];
        int bx = run->x0 - origin_x, by = run->y0 - origin_y;

        /* The image's pixels the run covers, and where in the arrow's own
         * buffer they come from. */
        assert(region->imageOffset.x == run->x0 && region->imageOffset.y == run->y0);
        assert((int)region->imageExtent.width == run->x1 - run->x0);
        assert((int)region->imageExtent.height == run->y1 - run->y0);
        assert(region->imageExtent.depth == 1);
        /* The arrow's own rectangle in the buffer: its runs read from there. */
        assert(region->bufferOffset == (VkDeviceSize)((size_t)by * cursor->width + bx) * 4);
        assert(region->bufferRowLength == (uint32_t)cursor->width);
        assert(region->bufferImageHeight == (uint32_t)cursor->height);
        assert(region->imageSubresource.layerCount == 1);
    }
}

/* Every pixel a run copies is an opaque one of the arrow: the run's rectangle
 * of the buffer holds a pixel of the sprite, never the clear of a '.'.  Inside
 * a run a boundary may sit a pixel either way, the arrow is 12 pixels wide and
 * the picture is not a whole multiple of it, so the colours are exact only at
 * 1:1. */
static void check_pixels( uint32_t index, int px, int py, uint32_t width, uint32_t height )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    struct nx_cursor_overlay *cursor = mock_swapchain.nx_cursor;
    const uint32_t *pixels = cursor->per[index].pixels;
    int count = pointer_overlay_runs( px, py, 1280, 720, width, height, runs ), i;
    int origin_x = (int)((long long)px * width / 1280), origin_y = (int)((long long)py * height / 720);
    int exact = (width == 1280 && height == 720);
    int opaque = 0;

    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];
        int x, y;

        for (y = 0; y < run->y1 - run->y0; y++)
            for (x = 0; x < run->x1 - run->x0; x++)
            {
                int sx = run->src_x0 + x * (run->src_x1 - run->src_x0) / (run->x1 - run->x0);
                int sy = run->src_y;
                uint32_t pixel;

                if (sx >= run->src_x1) sx = run->src_x1 - 1;
                pixel = pixels[(size_t)(run->y0 - origin_y + y) * cursor->width + (run->x0 - origin_x + x)];
                opaque++;
                if (!exact) continue;
                if (pointer_cursor_shape[sy][sx] != '.')
                    assert(pixel == (pointer_cursor_shape[sy][sx] == 'W' ? 0xffffffffu : 0xff000000u));
            }
    }
    assert(opaque > 0);
}

static void present( uint32_t index )
{
    VkSwapchainKHR swapchain = 55;
    VkSemaphore osk_done = 77;
    uint32_t image = index;
    VkPresentInfoKHR info = { .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &image,
                              .waitSemaphoreCount = 1, .pWaitSemaphores = &osk_done };
    nx_cursor_present( &mock_queue, &info, &swapchain, &mock_pool );
    /* The present waits for the arrow (which waited for the keyboard), and
     * nothing of the pool is let go before it has been submitted. */
    assert(info.waitSemaphoreCount == 1);
    assert(info.pWaitSemaphores == &mock_swapchain.nx_cursor->per[index].done);
    assert(recorded.waits == 1 && recorded.waited == osk_done);
    assert(recorded.signalled == mock_swapchain.nx_cursor->per[index].done);
}

static void reset_recorded( void )
{
    memset( &recorded, 0, sizeof(recorded) );
}

int main(void)
{
    VkSemaphore present_semaphore = 66;

    mock_device.host.device = 1;
    mock_device.p_vkResetCommandBuffer = mock_reset;
    mock_device.p_vkBeginCommandBuffer = mock_begin;
    mock_device.p_vkCmdPipelineBarrier = mock_pipeline_barrier;
    mock_device.p_vkCmdCopyBufferToImage = mock_copy;
    mock_device.p_vkEndCommandBuffer = mock_end;
    mock_device.p_vkQueueSubmit = mock_submit;
    mock_device.p_vkDestroySemaphore = mock_destroy_semaphore;
    mock_device.p_vkDestroyBuffer = mock_destroy_buffer;
    mock_device.p_vkFreeMemory = mock_free_memory;
    mock_device.p_vkDestroyCommandPool = mock_destroy_pool;
    mock_queue.device = &mock_device;
    mock_swapchain.nx_osk_usable = TRUE;
    mock_swapchain.nx_extent.width = 1280;
    mock_swapchain.nx_extent.height = 720;

    /* The program hid the cursor, and a swapchain the arrow cannot go into:
     * the present is left as the program made it. */
    {
        VkSwapchainKHR swapchain = 55;
        uint32_t image = 0;
        VkPresentInfoKHR info = { .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &image,
                                  .waitSemaphoreCount = 1, .pWaitSemaphores = &present_semaphore };

        cursor_visible = 0;
        nx_cursor_present( &mock_queue, &info, &swapchain, &mock_pool );
        assert(info.pWaitSemaphores == &present_semaphore && !prepare_calls && !recorded.submits);
        cursor_visible = 1;
        mock_swapchain.nx_osk_usable = 0;
        nx_cursor_present( &mock_queue, &info, &swapchain, &mock_pool );
        assert(info.pWaitSemaphores == &present_semaphore && !prepare_calls && !recorded.submits);
        mock_swapchain.nx_osk_usable = 1;
    }

    /* The panel's own size: the arrow's own 12x19, at the panel point, copied
     * in after the keyboard's copy. */
    present( 0 );
    assert(prepare_calls == 1 && prepare_width == POINTER_CURSOR_W && prepare_height == POINTER_CURSOR_H);
    assert(recorded.resets == 1 && recorded.begins == 1 && recorded.ends == 1 && recorded.submits == 1);
    /* In to be written, back to be shown. */
    assert(recorded.barriers == 2);
    assert(recorded.barrier_from[0] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    assert(recorded.barrier_to[0] == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    assert(recorded.barrier_from[1] == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    assert(recorded.barrier_to[1] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    check_recorded( 0, cursor_panel_x, cursor_panel_y, 1280, 720 );
    check_pixels( 0, cursor_panel_x, cursor_panel_y, 1280, 720 );

    /* A 1080p picture: the same arrow, its runs scaled into the image. */
    mock_swapchain.nx_extent.width = 1920;
    mock_swapchain.nx_extent.height = 1080;
    reset_recorded();
    present( 0 );
    /* The arrow's own rectangle at the picture's scale, and not the tip's
     * rounding: 12x19 panel pixels are 18x28.5 of 1920x1080. */
    assert(prepare_calls == 2 && prepare_width == 18 && prepare_height == 29);
    check_recorded( 0, cursor_panel_x, cursor_panel_y, 1920, 1080 );
    check_pixels( 0, cursor_panel_x, cursor_panel_y, 1920, 1080 );

    /* One panel pixel to the right: the arrow's own rectangle is the
     * picture's scale of it either way, so the buffers are not made again --
     * and only this image's copy is recorded again. */
    cursor_panel_x++;
    reset_recorded();
    present( 0 );
    assert(prepare_calls == 2 && prepare_width == 18 && prepare_height == 29);
    assert(recorded.resets == 1 && recorded.copies == 1);
    check_recorded( 0, cursor_panel_x, cursor_panel_y, 1920, 1080 );
    cursor_panel_x--;

    /* A 4:3 picture at the panel's corner: what the image has, no more. */
    mock_swapchain.nx_extent.width = 640;
    mock_swapchain.nx_extent.height = 480;
    cursor_panel_x = 1270; cursor_panel_y = 710;
    reset_recorded();
    present( 0 );
    check_recorded( 0, 1270, 710, 640, 480 );
    check_pixels( 0, 1270, 710, 640, 480 );

    /* The picture's other image: its own command buffer, buffer and
     * semaphore. */
    mock_swapchain.nx_extent.width = 1280;
    mock_swapchain.nx_extent.height = 720;
    cursor_panel_x = 100; cursor_panel_y = 50;
    reset_recorded();
    present( 1 );
    assert(recorded.image == mock_swapchain.nx_cursor->images[1]);
    assert(recorded.buffer == mock_swapchain.nx_cursor->per[1].buffer);
    assert(recorded.resets == 1 && recorded.copies == 1 && recorded.submits == 1);
    check_recorded( 1, cursor_panel_x, cursor_panel_y, 1280, 720 );

    /* The arrow moved: the same image's copy is recorded again, and one that
     * has not moved is not. */
    reset_recorded();
    present( 1 );   /* the same place: no copy to record, but the present still waits for it */
    assert(recorded.resets == 0 && recorded.copies == 0 && recorded.submits == 1);
    cursor_panel_x = 101;
    present( 1 );
    assert(recorded.resets == 1 && recorded.copies == 1);
    check_recorded( 1, cursor_panel_x, cursor_panel_y, 1280, 720 );

    /* What the swapchain's teardown lets go of, and the arrow's part of it: a
     * semaphore, a buffer and its memory for each image, then the pool. */
    {
        uint32_t i;

        for (i = 0; i < mock_swapchain.nx_cursor->count; i++) free( mock_swapchain.nx_cursor->per[i].pixels );
        nx_cursor_free( &mock_device, mock_swapchain.nx_cursor );
        mock_swapchain.nx_cursor = NULL;
        assert(recorded.destroyed_semaphores == 2 && recorded.destroyed_buffers == 2);
        assert(recorded.freed_memory == 2 && recorded.destroyed_pools == 1);
        mem_free( &mock_pool );
    }

    printf( "cursor vulkan: the arrow's runs copied into the image, after the keyboard\n" );
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="autorun-cursor-vulkan-") as tmp:
    tmp = Path(tmp)
    (tmp / "pointer_overlay.h").write_text((root / "horizon-wine/source/pointer_overlay.h").read_text())
    (tmp / "pointer_cursor.h").write_text((root / "horizon-wine/source/pointer_cursor.h").read_text())
    (tmp / "cursor_vulkan.c").write_text(fixture + implementation + mocks + checks)
    subprocess.run([os.environ.get("WINE_NX_HOST_CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-address", "-Wno-unused-function", "-fsanitize=address,undefined",
                    str(tmp / "cursor_vulkan.c"), "-o", str(tmp / "cursor_vulkan")], check=True)
    subprocess.run([str(tmp / "cursor_vulkan")], check=True)
print("Cursor vulkan: runs, scale, edges, pixels and the present's wait")
