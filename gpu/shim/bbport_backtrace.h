// bbport: backtrace() for diagnostics. Bionic has none (execinfo.h arrives with API 33 and is
// unusable at API 30): there the stack is walked with the unwinder.
#pragma once

#ifdef __ANDROID__
#include <dlfcn.h>
#include <unistd.h>
#include <unwind.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace BbBacktrace {
struct State {
    void** frames;
    int count, max;
};
inline _Unwind_Reason_Code Step(struct _Unwind_Context* context, void* argument) {
    auto* state = static_cast<State*>(argument);
    const std::uintptr_t pc = _Unwind_GetIP(context);
    if (!pc || state->count >= state->max) {
        return _URC_END_OF_STACK;
    }
    state->frames[state->count++] = reinterpret_cast<void*>(pc);
    return _URC_NO_REASON;
}
} // namespace BbBacktrace

inline int bb_backtrace(void** frames, int max) {
    BbBacktrace::State state{frames, 0, max};
    _Unwind_Backtrace(BbBacktrace::Step, &state);
    return state.count;
}

inline void bb_backtrace_symbols_fd(void* const* frames, int count, int fd) {
    for (int i = 0; i < count; ++i) {
        Dl_info info{};
        char line[256];
        if (dladdr(frames[i], &info) && info.dli_fname) {
            std::snprintf(line, sizeof(line), "%s(%s+0x%lx) [%p]\n", info.dli_fname,
                          info.dli_sname ? info.dli_sname : "?",
                          static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(frames[i]) -
                              reinterpret_cast<std::uintptr_t>(info.dli_saddr ? info.dli_saddr : info.dli_fbase)),
                          frames[i]);
        } else {
            std::snprintf(line, sizeof(line), "[%p]\n", frames[i]);
        }
        const ssize_t written = write(fd, line, std::strlen(line));
        (void)written;
    }
}
#else
#include <execinfo.h>
inline int bb_backtrace(void** frames, int max) {
    return backtrace(frames, max);
}
inline void bb_backtrace_symbols_fd(void* const* frames, int count, int fd) {
    backtrace_symbols_fd(frames, count, fd);
}
#endif
