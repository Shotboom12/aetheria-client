// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace gxvk::vk::hdgeometry {
inline bool interiorDraw=false;
class InteriorScope {
 bool previous;
public:
 explicit InteriorScope(bool value):previous(interiorDraw){interiorDraw=value;}
 ~InteriorScope(){interiorDraw=previous;}
 InteriorScope(const InteriorScope&)=delete;
 InteriorScope& operator=(const InteriorScope&)=delete;
};
}
