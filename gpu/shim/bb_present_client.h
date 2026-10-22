// bbport: the game's end of the frame path to the Android app (Bionic only). The socket, the
// messages and the buffers' hand-over; the Vulkan side is shadps4/video_core/renderer_vulkan/
// vk_swapchain_ahb.cpp. Wire format: tools/android/present/bb_present_proto.h.
#pragma once
#ifdef __ANDROID__
#include <mutex>
#include "common/types.h"

struct AHardwareBuffer;

namespace BbPresent {

struct Release {
    u32 generation{};
    u32 index{};
    u64 frame{};
    int fence_fd{-1}; ///< release fence, owned by the receiver; -1: free at once
};

class Client {
public:
    Client() = default;
    ~Client();

    /// True when BB_PRESENT_SOCKET names the app's socket: the frames go there. False: the game
    /// renders into the buffers and nobody shows them (benchmarks, a missing app).
    static bool Configured();

    /// True when BB_VSYNC=1 (bbport.ini vsync=1): the app is to show every frame for at least one
    /// display refresh, in order (BB_PRESENT_FLAG_FIFO), instead of the newest only. Lossless Scaling
    /// frame generation (vk_lsfg.h) needs that too while it is on, whatever the VSync setting says:
    /// the caller passes the effective mode (VsyncRequested() || frame generation on) to Connect and
    /// SetFifo.
    static bool VsyncRequested();

    /// Connects to BB_PRESENT_SOCKET (retries for a while: the app may be a moment late), says HELLO
    /// (with the presentation mode: `fifo`) and checks the app's version. Returns false (logged)
    /// when there is no app to talk to.
    bool Connect(u32 width, u32 height, u32 format, u32 count, bool fifo);
    /// Changes the presentation mode of the connection (BB_PRESENT_MODE) when it differs from the
    /// current one; the app follows at once. False: the message could not be sent.
    bool SetFifo(bool fifo);
    bool Fifo() const { return fifo_mode; }
    bool Connected() const { return fd >= 0; }

    /// Starts a new buffer set: RESET for every generation after the first (the HELLO of Connect
    /// is the first), then each buffer. `generation` is the one to use for the set.
    bool BeginGeneration(u32 generation, u32 width, u32 height, u32 format, u32 count);
    bool SendBuffer(u32 generation, u32 index, u32 width, u32 height, u32 format,
                    AHardwareBuffer* buffer);

    /// Hands buffer `index` to the app. The acquire fence is borrowed (the caller closes it).
    bool SendFrame(u32 generation, u32 index, u64 frame, u32 width, u32 height, int acquire_fd);

    /// Reads one release: waits up to timeout_ms (0: look only, -1: forever). 1 = got one,
    /// 0 = nothing, -1 = the app closed the connection or the socket failed (logged).
    int WaitRelease(Release& out, int timeout_ms);

    void Disconnect();

private:
    bool SendRaw(const void* msg, int fd);
    int fd{-1};
    std::mutex send_mutex; ///< a BUFFER message and its buffer go out as one pair
    u32 hello_count{};
    bool fifo_mode{false}; ///< the mode the app was last told (HELLO or MODE)
};

} // namespace BbPresent
#endif
