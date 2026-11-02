#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdCaptureSession.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryOwner.hpp"
namespace gxvk::vk::hdcapture {
namespace {
Batch batch;
bool requested = false, begun = false;
uint64_t epoch = 1, sceneSerial = 0;
}
void BeginScene(bool enabled) {
    batch.Reset(); requested = enabled; begun = false; sceneSerial = 0;
}
void Capture(const pipelines::Draw& draw, uint64_t serial, bool sceneTarget, bool queryActive) {
    if (!requested || !sceneTarget || queryActive || !serial) return;
    // Opaque and alpha-keyed geometry only. Sky, UI and transparent/liquid draws are separate.
    if (!draw.depthTest || !draw.depthWrite || draw.blend > 1) return;
    if (!begun) { batch.Begin(serial,epoch); sceneSerial=serial; begun=true; }
    auto tagged=draw;tagged.geometryInterior=hdgeometry::interiorDraw;
    batch.Append(tagged,serial,epoch);
}
void EndScene(uint64_t serial) {
    requested = false;
    if (begun) batch.Seal(serial,epoch);
}
void BeforeBufferMutation(VkBuffer buffer) { batch.BeforeBufferMutation(buffer); }
void BeforeImageMutation(uint32_t id) { batch.BeforeImageMutation(id); }
void ResetDevice() {
    requested=false; begun=false; sceneSerial=0; batch.Release();
    if (++epoch == 0) epoch=1;
}
const Batch* Ready(uint64_t serial) {
    if (requested || !begun || serial != sceneSerial || !batch.Count()) return nullptr;
    return &batch;
}
bool Read(size_t index, uint64_t serial, pipelines::Draw& out) {
    return Ready(serial) && batch.Read(index,serial,epoch,out);
}
}
