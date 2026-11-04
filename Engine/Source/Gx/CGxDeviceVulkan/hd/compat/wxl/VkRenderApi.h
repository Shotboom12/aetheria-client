// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: the subset of WarcraftXL's wxl/VkRenderApi.h (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL, as carried by
// Mado's Azeroth Remastered 2026-11-03 source) that hd/lumen's GPU executor is written against. Aetheria's Vulkan device
// is not the wxl-vulkan-api device: gxvk::hdwxl (hd/HdWxlShim.*) implements this table over hdscene::Apply, so the
// Lumenveil sources stay as Mado wrote them. Only the members hd/lumen uses exist; the rest of the wxl table is absent.
#ifndef AETHERIA_WXL_VK_RENDER_API_SUBSET_H
#define AETHERIA_WXL_VK_RENDER_API_SUBSET_H
#include <stdint.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#define WXL_VK_RENDER_API_NAME    "wxl.vk.render"
#define WXL_VK_RENDER_API_VERSION 1
#define WXL_VK_STAGE_NEEDS_DEPTH   0x1u
#define WXL_VK_STAGE_WRITES_COLOR  0x2u
#define WXL_VK_GBUFFER_CLASS_MASK     0x1Fu
#define WXL_VK_GBUFFER_INTERIOR_BIT   0x20u
#define WXL_VK_GBUFFER_CLASS_NONE     0u
#define WXL_VK_GBUFFER_CLASS_TERRAIN  1u
#define WXL_VK_GBUFFER_CLASS_MAPOBJ   2u
#define WXL_VK_GBUFFER_CLASS_INTERIOR 3u
#define WXL_VK_GBUFFER_CLASS_MODEL    4u
#define WXL_VK_GBUFFER_CLASS_UNLIT    5u
#define WXL_VK_GBUFFER_MOVER_BIT      0x40u
#define WXL_VK_GBUFFER_PLAIN_BIT      0x80u
#define WXL_VK_GBUFFER_TAG_MIN        0.505f
#define WXL_VK_GBUFFER_TAG_SPAN       0.49f
#define WXL_VK_GBUFFER_TAG_TOLERANCE  0.03f
#define WXL_VK_GBUFFER_MAX_INTERIORS 128u
#define WXL_VK_INTERIOR_EXTERIOR_LIT 1u
#define WXL_VK_INTERIOR_GROUP_UNKNOWN 0xFFFFFFFFu
#ifdef __cplusplus
extern "C" {
#endif
// Points: only the three hd/lumen registers at (AfterOpaqueWorld, AfterWorldBeforeSpells, AfterWorldBloom).
typedef enum WXL_VkMemoryClass { WXL_VK_MEMORY_DEVICE_LOCAL = 0, WXL_VK_MEMORY_HOST = 1, WXL_VK_MEMORY_STREAM = 2 } WXL_VkMemoryClass;
typedef enum WXL_VkRenderPoint { WXL_VK_POINT_AFTER_OPAQUE_WORLD = 0, WXL_VK_POINT_AFTER_WORLD_BEFORE_SPELLS = 5, WXL_VK_POINT_AFTER_WORLD_BLOOM = 7, WXL_VK_POINT_COUNT = 8 } WXL_VkRenderPoint;
typedef struct WXL_VkDeviceInfo
{
    uint32_t                  structSize;
    VkInstance                instance;
    VkPhysicalDevice          physicalDevice;
    VkDevice                  device;
    uint32_t                  queueFamily;
    uint32_t                  frameSlots;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   getDeviceProcAddr;
    VkPipelineCache           pipelineCache;
} WXL_VkDeviceInfo;
typedef struct WXL_VkBufferDesc
{
    VkDeviceSize       size;
    VkBufferUsageFlags usage;
    uint32_t           memory;
} WXL_VkBufferDesc;
typedef struct WXL_VkBufferInfo
{
    VkBuffer     buffer;
    VkDeviceSize size;
    void*        mapped;
} WXL_VkBufferInfo;
typedef struct WXL_VkImageDesc
{
    VkFormat           format;
    uint32_t           width;
    uint32_t           height;
    uint32_t           mipLevels;
    VkImageUsageFlags  usage;
    VkImageAspectFlags aspect;
} WXL_VkImageDesc;
typedef struct WXL_VkImageInfo
{
    VkImage     image;
    VkImageView view;
} WXL_VkImageInfo;
typedef struct WXL_VkSceneConstants
{
    float view[16];
    float projection[16];
    float viewProj[16];
    float cameraPos[4];
    float fogColor[4];
    float fogRange[4];
    float viewport[4];
    float time[4];
} WXL_VkSceneConstants;
typedef struct WXL_VkStageContext
{
    uint32_t        structSize;
    uint32_t        point;
    uint64_t        serial;
    uint64_t        completedSerial;
    uint32_t        frameSlot;
    uint32_t        frameSlots;
    VkCommandBuffer cmd;
    VkExtent2D      extent;
    VkImage         colorImage;
    VkImageView     colorView;
    VkFormat        colorFormat;
    VkImageLayout   colorLayout;
    uint32_t        swapRB;
    VkImageView     depthView;
    VkFormat        depthFormat;
    VkImageLayout   depthLayout;
    const WXL_VkSceneConstants* scene;
    VkViewport      viewport;
    uint32_t        sceneValid;
    float           jitter[2];
} WXL_VkStageContext;
typedef struct WXL_VkStageDesc
{
    uint32_t structSize;
    void*    user;
    uint32_t point;
    int32_t  order;
    uint32_t flags;
    int  (__cdecl* Wanted)(void* user);
    void (__cdecl* Run)(void* user, const WXL_VkStageContext* ctx);
} WXL_VkStageDesc;
typedef struct WXL_VkPreviousDepth
{
    uint32_t      structSize;
    uint32_t      valid;
    uint64_t      serial;
    VkImage       image;
    VkImageView   imageView;
    VkFormat      format;
    VkImageLayout layout;
    VkExtent2D    extent;
    float         view[16];
    float         projection[16];
    float         viewProj[16];
    float         depthMin;
    float         depthMax;
    VkViewport    viewport;
    float         jitter[2];
} WXL_VkPreviousDepth;
typedef struct WXL_VkInteriorVolume
{
    float localFromView[12];
    float boundsMin[4];
    float boundsMax[4];
    float sphere[4];
} WXL_VkInteriorVolume;
typedef struct WXL_VkInteriorGroup
{
    uint32_t modelKey;
    uint32_t groupIndex;
    uint32_t groupFlags;
    uint32_t reserved;
} WXL_VkInteriorGroup;
typedef struct WXL_VkGBuffer
{
    uint32_t      structSize;
    uint32_t      valid;
    uint64_t      serial;
    VkImageView   albedoView;
    VkImageView   normalView;
    VkImageView   lightView;
    VkFormat      albedoFormat;
    VkFormat      normalFormat;
    VkFormat      lightFormat;
    VkImageLayout layout;
    VkExtent2D    extent;
    uint32_t                    interiorCount;
    const WXL_VkInteriorVolume* interiors;
    const WXL_VkInteriorGroup*  interiorGroups;
    VkImageView shadowLowView, shadowHighView;
    VkFormat shadowEndpointFormat;
    uint32_t shadowEndpointContract;
} WXL_VkGBuffer;
typedef struct WXL_VkRenderApi {
    uint32_t structSize;
    uint32_t apiVersion;
    int (__cdecl* GetDeviceInfo)(WXL_VkDeviceInfo* out);
    void* (__cdecl* CreateBuffer)(const WXL_VkBufferDesc* desc, WXL_VkBufferInfo* out);
    void (__cdecl* RetireBuffer)(void* buffer, uint64_t lastSerial);
    void (__cdecl* FlushBuffer)(void* buffer);
    void* (__cdecl* CreateImage)(const WXL_VkImageDesc* desc, WXL_VkImageInfo* out);
    void (__cdecl* RetireImage)(void* image, uint64_t lastSerial);
    void* (__cdecl* RegisterStage)(const WXL_VkStageDesc* desc, const char* name);
    void (__cdecl* UnregisterStage)(void* stage);
    void (__cdecl* RequestGBuffer)(int wanted);
    int (__cdecl* GetGBuffer)(WXL_VkGBuffer* out);
    void (__cdecl* RequestOpaqueDepth)(int wanted);
    int (__cdecl* GetOpaqueDepth)(WXL_VkPreviousDepth* out);
} WXL_VkRenderApi;
#ifdef __cplusplus
}
#endif
#endif
