#pragma once
namespace gxvk::vk::texture { struct Image; }
namespace HD::NativeWaterService {
bool PrepareMaterialProxies(); // Shared current-frame scene/depth proxy identities.
const gxvk::vk::texture::Image* MirrorImage(); // Same-frame selected water-plane reflection; null when unavailable.
void BeginWorld();
void BeforeLiquids();
void AfterServices();
void BeforeUi();
void Shutdown();
const gxvk::vk::texture::Image* SkyImage(); // Implemented beside native texture records.
}
