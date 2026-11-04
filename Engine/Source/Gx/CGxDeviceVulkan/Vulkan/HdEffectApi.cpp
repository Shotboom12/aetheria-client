#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include <new>
#include <memory>
#include <cstdlib>

namespace gxvk::vk::hdeffect
{
    namespace
    {
        int Device(DeviceInfo* out)
        {
            const auto* h=context::Get();
            if (!out || out->structSize<sizeof(DeviceInfo) || !h || !h->iapi || !h->dapi) return 0;
            *out={sizeof(DeviceInfo),h->instance,h->physical,h->device,h->graphicsFamily,frame::kFramesInFlight,
                  loader::Api().GetInstanceProcAddr,h->iapi->GetDeviceProcAddr,VK_NULL_HANDLE};
            return out->getDeviceProcAddr!=nullptr;
        }
        void* Buffer(const BufferDesc* desc,BufferInfo* out)
        {
            if (!desc || !out || !desc->size || desc->memory>Stream) return nullptr;
            auto* b=static_cast<memory::Buffer*>(std::malloc(sizeof(memory::Buffer))); if(!b)return nullptr;
            new(b) memory::Buffer();
            const auto where=desc->memory==Host?memory::Where::HostVisible:
                             desc->memory==Stream?memory::Where::Stream:memory::Where::DeviceLocal;
            if (!memory::CreateBuffer(desc->size,desc->usage,where,*b)) {std::destroy_at(b);std::free(b);return nullptr;}
            *out={b->handle,b->size,b->block.mapped};return b;
        }
        void RetireBuffer(void* handle,uint64_t serial)
        {
            auto* b=static_cast<memory::Buffer*>(handle);if (!b) return;
            frame::RetireBuffer(*b,serial);std::destroy_at(b);std::free(b);
        }
        void Flush(void* handle)
        {
            auto* b=static_cast<memory::Buffer*>(handle);if(b) memory::FlushHost(b->block);
        }
        void* Image(const ImageDesc* desc,ImageInfo* out)
        {
            if(!desc || !out || !desc->mipLevels || desc->mipLevels>16 ||
               (desc->aspect && desc->aspect!=VK_IMAGE_ASPECT_COLOR_BIT))return nullptr;
            const bool target=(desc->usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)!=0;
            const bool storage=(desc->usage&VK_IMAGE_USAGE_STORAGE_BIT)!=0;
            const bool format=desc->format==VK_FORMAT_R8G8B8A8_UNORM || desc->format==VK_FORMAT_R16G16B16A16_SFLOAT ||
                desc->format==VK_FORMAT_R32_SFLOAT || (target && desc->format==VK_FORMAT_R16G16_SFLOAT) ||
                (storage && (desc->format==VK_FORMAT_R32G32B32A32_SFLOAT || desc->format==VK_FORMAT_R32_UINT || desc->format==VK_FORMAT_R8_UNORM || desc->format==VK_FORMAT_R16G16B16A16_SNORM)); // Lumenveil executor targets
            constexpr VkImageUsageFlags supported=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
                VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            // Aetheria 2026-11-03: a mipped storage-flagged image (hd/lumen noise: uploaded per mip, then sampled) is made as a plain mipped texture;
            // a render target or a single-level storage image keep their own paths.
            const bool mippedSampled=storage && !target && desc->mipLevels>1;
            if(!format || (desc->usage&~supported) || (target && storage) || (target && desc->mipLevels!=1) || (storage && !mippedSampled && desc->mipLevels!=1))return nullptr;
            auto* image=static_cast<texture::Image*>(std::malloc(sizeof(texture::Image)));if(!image)return nullptr;
            new(image) texture::Image();
            const bool ok=target?texture::CreateTarget(desc->width,desc->height,1,false,desc->format,*image):
                (storage && !mippedSampled)?texture::CreateStorage(desc->width,desc->height,desc->format,*image):
                texture::CreateMipped(desc->width,desc->height,desc->mipLevels,desc->format,*image);
            if(!ok){std::destroy_at(image);std::free(image);return nullptr;}
            *out={image->handle,image->view};return image;
        }
        void RetireImage(void* handle,uint64_t serial)
        {
            auto* image=static_cast<texture::Image*>(handle);if(!image)return;
            frame::RetireImage(*image,serial);std::destroy_at(image);std::free(image);
        }
    }
    const Api* Get()
    {
        static const Api api{ApiVersion,sizeof(Api),Device,Buffer,RetireBuffer,Flush,Image,RetireImage};
        return context::Live()?&api:nullptr;
    }
    StageContext Stage(const hdscene::Context& c,uint32_t point)
    {
        // CmdBlitImage converts BGRA -> RGBA, unlike upstream's raw-byte alias: swapRB stays zero.
        return {sizeof(StageContext),point,c.serial,c.completed,c.slot,frame::kFramesInFlight,c.cmd,c.extent,
                c.image,c.view,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_GENERAL,0};
    }
}
