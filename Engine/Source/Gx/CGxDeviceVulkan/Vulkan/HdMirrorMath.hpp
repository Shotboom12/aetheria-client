#pragma once
namespace gxvk::vk::hdmirror {
// Original D3D clip row-vector -> reflected oblique clip. CPU row-major output.
// Reject camera <= 0.1 yards above plane, nonfinite input and singular projection.
// Failure leaves output untouched. Y conversion belongs to the mirror vertex adapter.
bool ClipTransform(const float view[16], const float projection[16], float height, float eyeZ, float out[16]);
}
