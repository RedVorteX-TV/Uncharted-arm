/* Android (Bionic): no libvulkan is linked; the version below from ducvd89's port loads the driver
 * through gpu/shim/vk_driver_loader.cpp. Elsewhere the original, linked against libvulkan. */
#ifdef __ANDROID__
/* Independent backend smoke test: command submission and readback, no game graphics. */
/* The process has no libvulkan to link: the driver comes from gpu/shim/vk_driver_loader.cpp
 * (BB_VULKAN_DRIVER or the system loader) and every function from its vkGetInstanceProcAddr.
 * VK_NO_PROTOTYPES: the vkXxx names below are function pointers of this file. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "gpu/shim/vk_driver_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "Vulkan: %s returned %d\n", #call, r); exit(1); } } while (0)

static PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
static PFN_vkAllocateMemory vkAllocateMemory;
static PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
static PFN_vkBindBufferMemory vkBindBufferMemory;
static PFN_vkCmdFillBuffer vkCmdFillBuffer;
static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
static PFN_vkCreateBuffer vkCreateBuffer;
static PFN_vkCreateCommandPool vkCreateCommandPool;
static PFN_vkCreateDevice vkCreateDevice;
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkDestroyBuffer vkDestroyBuffer;
static PFN_vkDestroyCommandPool vkDestroyCommandPool;
static PFN_vkDestroyDevice vkDestroyDevice;
static PFN_vkDestroyInstance vkDestroyInstance;
static PFN_vkEndCommandBuffer vkEndCommandBuffer;
static PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
static PFN_vkFreeMemory vkFreeMemory;
static PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
static PFN_vkGetDeviceQueue vkGetDeviceQueue;
static PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkMapMemory vkMapMemory;
static PFN_vkQueueSubmit vkQueueSubmit;
static PFN_vkQueueWaitIdle vkQueueWaitIdle;
static PFN_vkUnmapMemory vkUnmapMemory;
static PFN_vkGetInstanceProcAddr get_instance_proc_addr;
#define LOAD(instance, name) do { name = (PFN_##name)get_instance_proc_addr(instance, #name); \
    if (!name) { fprintf(stderr, "Vulkan: driver lacks %s\n", #name); exit(1); } } while (0)

int vulkan_smoke(void) {
    get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)bb_vk_get_instance_proc_addr();
    if (!get_instance_proc_addr) { fprintf(stderr, "Vulkan: no driver\n"); return 1; }
    LOAD(NULL, vkCreateInstance);
    VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="Bloodborne native probe", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance instance; CHECK(vkCreateInstance(&ici, NULL, &instance));
    LOAD(instance, vkAllocateCommandBuffers);
    LOAD(instance, vkAllocateMemory);
    LOAD(instance, vkBeginCommandBuffer);
    LOAD(instance, vkBindBufferMemory);
    LOAD(instance, vkCmdFillBuffer);
    LOAD(instance, vkCmdPipelineBarrier);
    LOAD(instance, vkCreateBuffer);
    LOAD(instance, vkCreateCommandPool);
    LOAD(instance, vkCreateDevice);
    LOAD(instance, vkDestroyBuffer);
    LOAD(instance, vkDestroyCommandPool);
    LOAD(instance, vkDestroyDevice);
    LOAD(instance, vkDestroyInstance);
    LOAD(instance, vkEndCommandBuffer);
    LOAD(instance, vkEnumeratePhysicalDevices);
    LOAD(instance, vkFreeMemory);
    LOAD(instance, vkGetBufferMemoryRequirements);
    LOAD(instance, vkGetDeviceQueue);
    LOAD(instance, vkGetPhysicalDeviceMemoryProperties);
    LOAD(instance, vkGetPhysicalDeviceProperties);
    LOAD(instance, vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(instance, vkMapMemory);
    LOAD(instance, vkQueueSubmit);
    LOAD(instance, vkQueueWaitIdle);
    LOAD(instance, vkUnmapMemory);
    uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "Vulkan: no physical devices\n"); vkDestroyInstance(instance, NULL); return 1; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) exit(1);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = devices[0]; free(devices);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, NULL);
    VkQueueFamilyProperties *families = calloc(nq, sizeof(*families));
    if (!families) exit(1);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, families);
    uint32_t family = 0;
    while (family < nq && !(families[family].queueCount && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))) ++family;
    free(families);
    if (family == nq) { fprintf(stderr, "Vulkan: no graphics queue\n"); vkDestroyInstance(instance, NULL); return 1; }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
    VkDevice device; CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo bci = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=4096, .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer; CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = 0, wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < memory.memoryTypeCount && !((requirements.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & wanted) == wanted)) ++type;
    if (type == memory.memoryTypeCount) { fprintf(stderr, "Vulkan: no coherent host memory\n"); exit(1); }
    VkMemoryAllocateInfo mai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=requirements.size, .memoryTypeIndex=type};
    VkDeviceMemory allocation; CHECK(vkAllocateMemory(device, &mai, NULL, &allocation));
    CHECK(vkBindBufferMemory(device, buffer, allocation, 0));
    VkCommandPoolCreateInfo pci = {.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex=family};
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool=pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1};
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    vkCmdFillBuffer(command, buffer, 0, 4096, 0xb100db0e);
    VkMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    uint32_t *pixels; CHECK(vkMapMemory(device, allocation, 0, 4096, 0, (void **)&pixels));
    int ok = 1; for (int i=0; i<1024; ++i) if (pixels[i] != 0xb100db0e) ok = 0;
    vkUnmapMemory(device, allocation);
    printf("Vulkan: %s; command submission + 4096-byte readback %s\n", properties.deviceName, ok ? "PASS" : "FAIL");
    vkDestroyCommandPool(device, pool, NULL); vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, allocation, NULL); vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL);
    return ok ? 0 : 1;
}
#else
/* Independent backend smoke test: command submission and readback, no game graphics. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "Vulkan: %s returned %d\n", #call, r); exit(1); } } while (0)

int vulkan_smoke(void) {
    VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="Bloodborne native probe", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance instance; CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "Vulkan: no physical devices\n"); vkDestroyInstance(instance, NULL); return 1; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) exit(1);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = devices[0]; free(devices);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, NULL);
    VkQueueFamilyProperties *families = calloc(nq, sizeof(*families));
    if (!families) exit(1);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, families);
    uint32_t family = 0;
    while (family < nq && !(families[family].queueCount && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))) ++family;
    free(families);
    if (family == nq) { fprintf(stderr, "Vulkan: no graphics queue\n"); vkDestroyInstance(instance, NULL); return 1; }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
    VkDevice device; CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo bci = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=4096, .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer; CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = 0, wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < memory.memoryTypeCount && !((requirements.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & wanted) == wanted)) ++type;
    if (type == memory.memoryTypeCount) { fprintf(stderr, "Vulkan: no coherent host memory\n"); exit(1); }
    VkMemoryAllocateInfo mai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=requirements.size, .memoryTypeIndex=type};
    VkDeviceMemory allocation; CHECK(vkAllocateMemory(device, &mai, NULL, &allocation));
    CHECK(vkBindBufferMemory(device, buffer, allocation, 0));
    VkCommandPoolCreateInfo pci = {.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex=family};
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool=pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1};
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    vkCmdFillBuffer(command, buffer, 0, 4096, 0xb100db0e);
    VkMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    uint32_t *pixels; CHECK(vkMapMemory(device, allocation, 0, 4096, 0, (void **)&pixels));
    int ok = 1; for (int i=0; i<1024; ++i) if (pixels[i] != 0xb100db0e) ok = 0;
    vkUnmapMemory(device, allocation);
    printf("Vulkan: %s; command submission + 4096-byte readback %s\n", properties.deviceName, ok ? "PASS" : "FAIL");
    vkDestroyCommandPool(device, pool, NULL); vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, allocation, NULL); vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL);
    return ok ? 0 : 1;
}
#endif
