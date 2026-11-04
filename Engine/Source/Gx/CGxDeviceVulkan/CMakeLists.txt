# The Vulkan backend: CGxDeviceVulkan (the CGxDevice subclass the engine's factory creates for GxApi_Vulkan)
# over the engine-agnostic Vulkan tier under Vulkan/ (loader, context, memory, frame, textures, pipelines).
#
# A target of its own, apart from Gx, for one reason: the C++ standard. The decompiled engine is built as
# C++17 and stays that way; the Vulkan tier is C++20. Gx and GxVulkan need each other (Gx.cpp calls
# CreateCGxDeviceVulkan, the device calls into CGxDevice), which two static libraries can do: the executable
# links both and the MSVC linker resolves across them in any order.
file(GLOB_RECURSE GX_VULKAN_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Azeroth Remastered 2026-11-03 update. hd/lumen (Lumenveil logic, AR mode only) is on by default; hd/fsr and hd/dlss
# are staged but need device support our Vulkan tier does not have yet (render-scale jitter, velocity, swapchain
# replacement), so they stay out of the glob until their option is turned on.
# (The options are declared in the root CMakeLists.txt.)
if (NOT WOWDECOMP_LUMEN)
    list(FILTER GX_VULKAN_SOURCES EXCLUDE REGEX "/hd/lumen/|/hd/HdLumenBridge")
endif ()
if (NOT WOWDECOMP_FSR)
    list(FILTER GX_VULKAN_SOURCES EXCLUDE REGEX "/hd/fsr/")
    if (AETHERIA_NATIVE_FSR)
        # The native FSR (hd/HdNativeFsr.cpp) uses only the SDK wrapper of Mado's tree, not his wxl-bound pass.
        list(APPEND GX_VULKAN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/hd/fsr/HdFsrCore.cpp")
    endif ()
endif ()
if (NOT AETHERIA_NATIVE_FSR)
    list(FILTER GX_VULKAN_SOURCES EXCLUDE REGEX "/hd/HdNativeFsr")
endif ()
if (NOT WOWDECOMP_DLSS)
    list(FILTER GX_VULKAN_SOURCES EXCLUDE REGEX "/hd/dlss/")
endif ()

add_library(GxVulkan STATIC
    ${GX_VULKAN_SOURCES}
)

set_target_properties(GxVulkan PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(GxVulkan
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (AETHERIA_NATIVE_FSR)
    # HdFsrCore.cpp is guarded by WOWDECOMP_FSR; the define is private to this target, so the model/world hooks stay off.
    target_compile_definitions(GxVulkan PRIVATE WOWDECOMP_FSR=1)
    target_include_directories(GxVulkan PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/FidelityFX")
endif ()

if (AETHERIA_NATIVE_DLSS)
    if (NOT AETHERIA_NATIVE_FSR)
        message(FATAL_ERROR "AETHERIA_NATIVE_DLSS requires AETHERIA_NATIVE_FSR=ON")
    endif ()
    foreach (DLSS_FILE include/nvsdk_ngx_vk.h lib/Windows_x86_64/x64/nvsdk_ngx_d.lib)
        if (NOT EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/DLSS/${DLSS_FILE}")
            message(FATAL_ERROR "DLSS: ThirdParty/DLSS/${DLSS_FILE} missing (run Tools/Setup-DlssSdk.ps1)")
        endif ()
    endforeach ()
    target_compile_definitions(GxVulkan PRIVATE AETHERIA_NATIVE_DLSS=1)
    target_include_directories(GxVulkan PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/DLSS/include")
    # /MD release static library; NGX loads Vulkan itself.
    target_link_libraries(GxVulkan PUBLIC "${CMAKE_SOURCE_DIR}/ThirdParty/DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
endif ()

target_link_libraries(GxVulkan
    PUBLIC
        VulkanHeaders
)

if (WOWDECOMP_RETAIL_ASSETS)
    target_compile_definitions(GxVulkan PRIVATE WOWDECOMP_RETAIL_ASSETS=1)
endif ()

