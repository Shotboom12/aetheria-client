# Included by Extensions/CMakeLists.txt for the WxlHost extension (see the "<Name>/<Name>.cmake" hook there).
#
# Extensions/WxlHost/src is an include root so ported WXL code keeps its own spelling:
#   #include "wxl/PluginApi.h"   #include "imgui/imgui.h"
# Dear ImGui (MIT) is compiled in from src/imgui, which the extension's own src/*.cpp glob picks up;
# its feature switches are in src/imgui/imconfig.h.
include_directories("${CMAKE_CURRENT_LIST_DIR}/src")
