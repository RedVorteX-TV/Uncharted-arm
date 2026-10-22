// bbport: the one place that loads a Vulkan driver (see vk_driver_loader.cpp).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*BbVkVoidFunction)(void);
typedef BbVkVoidFunction (*BbVkGetInstanceProcAddr)(void* instance, const char* name);

/// Loads the Vulkan driver once (BB_VULKAN_DRIVER unset: the system loader's libvulkan.so; set to a
/// path: that driver library directly) and returns its vkGetInstanceProcAddr, or NULL (logged).
/// Every other Vulkan function of the process is resolved through it.
BbVkGetInstanceProcAddr bb_vk_get_instance_proc_addr(void);

#ifdef __cplusplus
}
#endif
