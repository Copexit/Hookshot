// (hookshot) Every Vulkan command that does GPU work, or orders it, counts in work_epoch. full_barrier (vk_texture.cpp) leaves out a
// barrier recorded right after another one on the same command buffer with nothing in between: the second orders nothing the first
// did not. Include this header AFTER the file's other includes: it turns the calls below into a count and the call (a header
// included after it that names one of them with a '(' after it would be changed too).
#pragma once

#include <atomic>

#include "vk_loader.h"

namespace mkwvk {
extern std::atomic<uint64_t> work_epoch;   // (vk_texture.cpp)
extern thread_local VkCommandBuffer barrier_pending_cmd;   // (vk_texture.cpp, RX_VK_BARRIER_DEFER: the game's barrier held back)
void barrier_flush_pending();
// (local, meshsync) RX_VK_MESH_HOIST: a command that writes memory (a dispatch, a copy, a fill, a clear) recorded on the command buffer
// whose mesh fallback compute is hoisted ahead of it: the compute of later mesh draws may read what it writes, so they are not hoisted
// past it (vk_draw.cpp). Draws and passes do not count: a mesh program reads no render target
extern thread_local VkCommandBuffer hoist_watch_cmd;   // (vk_texture.cpp)
extern thread_local bool hoist_blocked;
extern thread_local uint64_t tl_work_n;   // (this thread's work commands: begin_pass's resume test)
inline void note_work() {
    if (barrier_pending_cmd) barrier_flush_pending();   // (recorded before the work, on the command buffer it was for)
    // (an add, not a load and a store: a thread preempted between the two would store a stale count over another thread's, which could
    // make the count equal the one a barrier was recorded at with work recorded since, and the next barrier is left out for it)
    work_epoch.fetch_add(1, std::memory_order_relaxed);
    tl_work_n++;
}
template <class... A>
inline void note_producer(VkCommandBuffer c, const A&...) {
    if (c == hoist_watch_cmd) hoist_blocked = true;
}
inline void note_end(VkCommandBuffer c) {   // (a held-back barrier goes in before its command buffer ends)
    if (barrier_pending_cmd == c && c) barrier_flush_pending();
}
} // namespace mkwvk

#define vkEndCommandBuffer(c) (::mkwvk::note_end(c), ::vkx::vkEndCommandBuffer(c))
#define vkCmdPipelineBarrier2(...) (::mkwvk::note_work(), ::vkx::vkCmdPipelineBarrier2(__VA_ARGS__))
#define vkCmdBeginRendering(...) (::mkwvk::note_work(), ::vkx::vkCmdBeginRendering(__VA_ARGS__))
#define vkCmdEndRendering(...) (::mkwvk::note_work(), ::vkx::vkCmdEndRendering(__VA_ARGS__))
#define vkCmdDraw(...) (::mkwvk::note_work(), ::vkx::vkCmdDraw(__VA_ARGS__))
#define vkCmdDrawIndexed(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawIndexed(__VA_ARGS__))
#define vkCmdDrawIndirect(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawIndirect(__VA_ARGS__))
#define vkCmdDrawIndexedIndirect(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawIndexedIndirect(__VA_ARGS__))
#define vkCmdDrawIndirectCount(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawIndirectCount(__VA_ARGS__))
#define vkCmdDrawIndexedIndirectCount(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawIndexedIndirectCount(__VA_ARGS__))
#define vkCmdDrawMeshTasksEXT(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawMeshTasksEXT(__VA_ARGS__))
#define vkCmdDrawMeshTasksIndirectEXT(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawMeshTasksIndirectEXT(__VA_ARGS__))
#define vkCmdDrawMeshTasksIndirectCountEXT(...) (::mkwvk::note_work(), ::vkx::vkCmdDrawMeshTasksIndirectCountEXT(__VA_ARGS__))
#define vkCmdDispatch(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdDispatch(a_...); }(__VA_ARGS__))
#define vkCmdDispatchIndirect(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdDispatchIndirect(a_...); }(__VA_ARGS__))
#define vkCmdCopyBuffer(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdCopyBuffer(a_...); }(__VA_ARGS__))
#define vkCmdCopyBufferToImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdCopyBufferToImage(a_...); }(__VA_ARGS__))
#define vkCmdCopyImageToBuffer(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdCopyImageToBuffer(a_...); }(__VA_ARGS__))
#define vkCmdCopyImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdCopyImage(a_...); }(__VA_ARGS__))
#define vkCmdBlitImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdBlitImage(a_...); }(__VA_ARGS__))
#define vkCmdResolveImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdResolveImage(a_...); }(__VA_ARGS__))
#define vkCmdClearColorImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdClearColorImage(a_...); }(__VA_ARGS__))
#define vkCmdClearDepthStencilImage(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdClearDepthStencilImage(a_...); }(__VA_ARGS__))
#define vkCmdClearAttachments(...) (::mkwvk::note_work(), ::vkx::vkCmdClearAttachments(__VA_ARGS__))
#define vkCmdFillBuffer(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdFillBuffer(a_...); }(__VA_ARGS__))
#define vkCmdUpdateBuffer(...) ([](const auto&... a_) { ::mkwvk::note_work(), ::mkwvk::note_producer(a_...); return ::vkx::vkCmdUpdateBuffer(a_...); }(__VA_ARGS__))
#define vkCmdBeginQuery(...) (::mkwvk::note_work(), ::vkx::vkCmdBeginQuery(__VA_ARGS__))
#define vkCmdEndQuery(...) (::mkwvk::note_work(), ::vkx::vkCmdEndQuery(__VA_ARGS__))
