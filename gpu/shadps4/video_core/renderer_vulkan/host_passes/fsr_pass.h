//  SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
//  SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

class FsrPass {
public:
    struct Settings {
        bool enable{true};
        bool use_rcas{true};
        float rcas_attenuation{0.25f};
    };

    void Create(vk::Device device, VmaAllocator allocator, u32 num_images);

    /// input_size is the part read, from the top-left of an input image of input_image_size
    /// (the same when empty). The output (R16G16B16A16Sfloat) is left shader-read-only.
    vk::ImageView Render(vk::CommandBuffer cmdbuf, vk::ImageView input, vk::Extent2D input_size,
                         vk::Extent2D output_size, Settings settings, bool hdr,
                         vk::Extent2D input_image_size = {});

    /// The image behind the view the last Render returned (upscaling presets, after
    /// BilalReloaded's Uncharted PC port).
    vk::Image LastOutput() const {
        return last_output;
    }

private:
    struct Img {
        u8 id{};
        bool dirty{true};

        VideoCore::UniqueImage intermediary_image;
        vk::UniqueImageView intermediary_image_view;

        VideoCore::UniqueImage output_image;
        vk::UniqueImageView output_image_view;
    };

    void ResizeAndInvalidate(u32 width, u32 height);
    void CreateImages(Img& img) const;

    vk::Device device{};
    u32 num_images{};

    vk::UniqueDescriptorSetLayout descriptor_set_layout{};
    vk::UniqueDescriptorSet easu_descriptor_set{};
    vk::UniqueDescriptorSet rcas_descriptor_set{};
    vk::UniqueSampler sampler{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniquePipeline easu_pipeline{};
    vk::UniquePipeline rcas_pipeline{};

    vk::Extent2D cur_size{};
    u32 cur_image{};
    vk::Image last_output{};
    std::vector<Img> available_imgs;
};

} // namespace Vulkan::HostPasses
