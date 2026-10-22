// bbport: Vulkan driver loading. Nothing in the process links against libvulkan: this file is the
// only dlopen of a Vulkan library and everything else comes from the entry point it returns
// (vulkan-hpp's dispatcher, VMA, FFX, Dear ImGui, the smoke test).
//
//  - BB_VULKAN_DRIVER unset or empty: the platform's Vulkan loader, dlopen("libvulkan.so")
//    (libvulkan.so.1 on glibc). On Android that is the system loader and the phone's stock driver;
//    layers and ICD discovery are the loader's business. The entry point is vkGetInstanceProcAddr.
//  - BB_VULKAN_DRIVER=/abs/path/libvulkan_xxx.so: that driver (e.g. Mesa Turnip from an adrenotools
//    style zip) is dlopen'ed directly, RTLD_NOW | RTLD_LOCAL, with no loader in between: no layers,
//    and the driver answers instance-level calls itself. The entry point is the ICD's
//    vk_icdGetInstanceProcAddr (after vk_icdNegotiateLoaderICDInterfaceVersion, asking for 5), or
//    its vkGetInstanceProcAddr when it exports only that.
//  - ... or, when the library exports neither but exports the symbol `HMI` (an adrenotools-style
//    Turnip pack built as an Android Vulkan HAL: hwvulkan_module_t), the HAL path below: open
//    device "vk0" through the module and use hwvulkan_device_t::GetInstanceProcAddr. There is no
//    Android loader in between, so nothing ever writes the dispatch word of the driver's
//    dispatchable handles (hwvulkan_dispatch_t, left at HWVULKAN_DISPATCH_MAGIC); that is fine
//    because every call goes straight to a driver function pointer.
#include "vk_driver_loader.h"

#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace {

// ---------------------------------------------------------------------------------------------
// Android hwvulkan HAL ABI. The NDK ships neither <hardware/hardware.h> nor <hardware/hwvulkan.h>,
// so the structs are declared here. Layouts, constants and field order are copied from AOSP
// hardware/libhardware/include/hardware/hardware.h and hwvulkan.h (Apache-2.0; the identical
// copies are in Mesa's include/android_stub/hardware/, which Turnip builds against):
//   hw_module_t     { u32 tag; u16 module_api_version; u16 hal_api_version; const char* id, *name,
//                     *author; hw_module_methods_t* methods; void* dso; u64 reserved[32 - 7]; }
//   hw_device_t     { u32 tag; u32 version; hw_module_t* module; u64 reserved[12];
//                     int (*close)(hw_device_t*); }
//   hwvulkan_device_t { hw_device_t common; PFN_vkEnumerateInstanceExtensionProperties;
//                     PFN_vkCreateInstance; PFN_vkGetInstanceProcAddr; }
//   HARDWARE_MODULE_TAG 'HWMT', HARDWARE_DEVICE_TAG 'HWDT', HAL_MODULE_INFO_SYM = HMI,
//   HWVULKAN_HARDWARE_MODULE_ID "vulkan", HWVULKAN_DEVICE_0 "vk0",
//   HWVULKAN_DEVICE_API_VERSION_0_1 = HARDWARE_MAKE_API_VERSION_2(0, 1, 0) = 0x00010000,
//   HWVULKAN_DISPATCH_MAGIC 0x01CDC0DE (= ICD_LOADER_MAGIC of the LunarG loader).
// hwvulkan_module_t is just { hw_module_t common; }.
// ---------------------------------------------------------------------------------------------
constexpr uint32_t kHwModuleTag = (uint32_t('H') << 24) | (uint32_t('W') << 16) |
                                  (uint32_t('M') << 8) | uint32_t('T');
constexpr uint32_t kHwDeviceTag = (uint32_t('H') << 24) | (uint32_t('W') << 16) |
                                  (uint32_t('D') << 8) | uint32_t('T');
constexpr uint32_t kHwvulkanDeviceApiVersion01 = 0x00010000;
constexpr uint32_t kHwApiVersion2MajMinMask = 0xffff0000;
constexpr const char* kHwvulkanModuleId = "vulkan";
constexpr const char* kHwvulkanDevice0 = "vk0";

struct HwDevice;
struct HwModule;
struct HwModuleMethods {
    int (*open)(const HwModule* module, const char* id, HwDevice** device);
};
struct HwModule {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char* id;
    const char* name;
    const char* author;
    HwModuleMethods* methods;
    void* dso;
    uint64_t reserved[32 - 7];
};
struct HwDevice {
    uint32_t tag;
    uint32_t version;
    HwModule* module;
    uint64_t reserved[12];
    int (*close)(HwDevice* device);
};
struct HwvulkanDevice {
    HwDevice common;
    BbVkVoidFunction EnumerateInstanceExtensionProperties; // really PFN_vkEnumerateInstanceExtensionProperties
    BbVkVoidFunction CreateInstance;                       // really PFN_vkCreateInstance
    BbVkGetInstanceProcAddr GetInstanceProcAddr;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(HwModule) == 248 && offsetof(HwModule, methods) == 32 &&
              offsetof(HwModule, dso) == 40, "hw_module_t layout");
static_assert(sizeof(HwDevice) == 120 && offsetof(HwDevice, close) == 112, "hw_device_t layout");
static_assert(sizeof(HwvulkanDevice) == 144 && offsetof(HwvulkanDevice, GetInstanceProcAddr) == 136,
              "hwvulkan_device_t layout");
#endif

// The HAL's device stays open for the process lifetime (the Android loader never closes it either
// until unload). The three entry points of the device back-fill what its GetInstanceProcAddr
// might not answer for a NULL instance, and vkEnumerateInstanceLayerProperties, which the Android
// loader implements and a HAL need not, reports no layers.
HwvulkanDevice* g_hal_device = nullptr;

int HalNoLayers(uint32_t* count, void* /*properties*/) {
    if (count) {
        *count = 0;
    }
    return 0; // VK_SUCCESS
}

BbVkVoidFunction HalGetInstanceProcAddr(void* instance, const char* name) {
    BbVkVoidFunction fn = g_hal_device->GetInstanceProcAddr(instance, name);
    if (fn || instance || !name) {
        return fn;
    }
    if (!std::strcmp(name, "vkEnumerateInstanceExtensionProperties")) {
        return g_hal_device->EnumerateInstanceExtensionProperties;
    }
    if (!std::strcmp(name, "vkCreateInstance")) {
        return g_hal_device->CreateInstance;
    }
    if (!std::strcmp(name, "vkEnumerateInstanceLayerProperties")) {
        return reinterpret_cast<BbVkVoidFunction>(&HalNoLayers);
    }
    return nullptr;
}

BbVkGetInstanceProcAddr OpenHal(void* hmi, const char* driver) {
    auto* module = static_cast<HwModule*>(hmi);
    if (module->tag != kHwModuleTag) {
        std::fprintf(stderr, "Vulkan: driver %s: HMI has the wrong module tag 0x%08x\n", driver,
                     module->tag);
        return nullptr;
    }
    if (!module->id || std::strcmp(module->id, kHwvulkanModuleId) != 0) {
        std::fprintf(stderr, "Vulkan: driver %s: HMI is module \"%s\", not \"%s\"\n", driver,
                     module->id ? module->id : "(null)", kHwvulkanModuleId);
        return nullptr;
    }
    if (!module->methods || !module->methods->open) {
        std::fprintf(stderr, "Vulkan: driver %s: HAL module has no open method\n", driver);
        return nullptr;
    }
    HwDevice* device = nullptr;
    const int status = module->methods->open(module, kHwvulkanDevice0, &device);
    if (status != 0 || !device) {
        std::fprintf(stderr, "Vulkan: driver %s: opening HAL device %s failed (%d)\n", driver,
                     kHwvulkanDevice0, status);
        return nullptr;
    }
    if (device->tag != kHwDeviceTag ||
        (device->version & kHwApiVersion2MajMinMask) != kHwvulkanDeviceApiVersion01) {
        std::fprintf(stderr, "Vulkan: driver %s: unexpected HAL device (tag 0x%08x, version 0x%08x)\n",
                     driver, device->tag, device->version);
        if (device->close) {
            device->close(device);
        }
        return nullptr;
    }
    auto* vk_device = reinterpret_cast<HwvulkanDevice*>(device);
    if (!vk_device->GetInstanceProcAddr) {
        std::fprintf(stderr, "Vulkan: driver %s: HAL device has no GetInstanceProcAddr\n", driver);
        if (device->close) {
            device->close(device);
        }
        return nullptr;
    }
    g_hal_device = vk_device;
    std::fprintf(stderr, "Vulkan: direct driver %s via HAL module HMI (%s by %s, module API %u.%u, "
                         "HAL API %u.%u)\n",
                 driver, module->name ? module->name : "?", module->author ? module->author : "?",
                 module->module_api_version >> 8, module->module_api_version & 0xff,
                 module->hal_api_version >> 8, module->hal_api_version & 0xff);
    return &HalGetInstanceProcAddr;
}

BbVkGetInstanceProcAddr Load() {
    const char* driver = std::getenv("BB_VULKAN_DRIVER");
    if (!driver || !*driver) {
#ifdef __ANDROID__
        static const char* const names[] = {"libvulkan.so"};
#else
        static const char* const names[] = {"libvulkan.so.1", "libvulkan.so"};
#endif
        for (const char* name : names) {
            if (void* lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
                if (void* gipa = dlsym(lib, "vkGetInstanceProcAddr")) {
                    std::fprintf(stderr, "Vulkan: system loader %s\n", name);
                    return reinterpret_cast<BbVkGetInstanceProcAddr>(gipa);
                }
            }
        }
        std::fprintf(stderr, "Vulkan: no system loader (%s)\n", dlerror());
        return nullptr;
    }
    void* lib = dlopen(driver, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::fprintf(stderr, "Vulkan: cannot load driver %s: %s\n", driver, dlerror());
        return nullptr;
    }
    using NegotiateFn = int (*)(uint32_t*);
    if (auto negotiate = reinterpret_cast<NegotiateFn>(
            dlsym(lib, "vk_icdNegotiateLoaderICDInterfaceVersion"))) {
        uint32_t version = 5;
        if (negotiate(&version) != 0) {
            std::fprintf(stderr, "Vulkan: driver %s rejected ICD interface version 5\n", driver);
            return nullptr;
        }
    }
    const char* entry = "vk_icdGetInstanceProcAddr";
    void* gipa = dlsym(lib, entry);
    if (!gipa) {
        entry = "vkGetInstanceProcAddr";
        gipa = dlsym(lib, entry);
    }
    if (!gipa) {
        if (void* hmi = dlsym(lib, "HMI")) {
            return OpenHal(hmi, driver);
        }
        std::fprintf(stderr, "Vulkan: driver %s exports none of vk_icdGetInstanceProcAddr, "
                             "vkGetInstanceProcAddr and HMI (Android Vulkan HAL module)\n", driver);
        return nullptr;
    }
    std::fprintf(stderr, "Vulkan: direct driver %s via %s\n", driver, entry);
    return reinterpret_cast<BbVkGetInstanceProcAddr>(gipa);
}

} // namespace

extern "C" __attribute__((visibility("default"))) BbVkGetInstanceProcAddr
bb_vk_get_instance_proc_addr(void) {
    static std::once_flag once;
    static BbVkGetInstanceProcAddr gipa;
    std::call_once(once, [] { gipa = Load(); });
    return gipa;
}
