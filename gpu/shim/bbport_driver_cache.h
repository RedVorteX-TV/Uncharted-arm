// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Vulkan driver's own pipeline cache on disk (vk_pipeline_cache.cpp). Header check
// from erickmendes22's uncharted-android (eba4bf6): the blob is handed to the driver only when
// its VkPipelineCacheHeaderVersionOne names this GPU and driver build.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <shared_mutex>
#include <span>

namespace BbDriverCache {

/// BB_DRIVER_CACHE=0: the driver cache is neither loaded nor saved.
inline bool Enabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_DRIVER_CACHE");
        return !(env && env[0] == '0');
    }();
    return enabled;
}

/// VkPipelineCacheHeaderVersionOne: header size, version 1, vendor, device, cache UUID.
inline bool Matches(std::span<const char> data, std::uint32_t vendor, std::uint32_t device,
                    std::span<const std::uint8_t, 16> uuid) {
    constexpr std::size_t size = 32;
    if (data.size() < size) return false;
    std::array<std::uint32_t, 4> h{};
    std::memcpy(h.data(), data.data(), sizeof(h));
    return h[0] >= size && h[0] <= data.size() && h[1] == 1 && h[2] == vendor &&
           h[3] == device && std::memcmp(data.data() + 16, uuid.data(), uuid.size()) == 0;
}

/// Pipeline creation holds it shared, the read of the cache's data exclusively: some drivers do
/// not expect vkGetPipelineCacheData during a creation that uses the same cache.
inline std::shared_mutex& Access() {
    static std::shared_mutex mutex;
    return mutex;
}

} // namespace BbDriverCache
