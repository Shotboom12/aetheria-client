// WxlHost overlay (see WxlOverlay.hpp).
//
// Device-agnostic on purpose: the ImGui draw lists are submitted through the engine's own Gx calls (the
// way the software cursor and the FPS graph draw), not through a Vulkan backend, so this works on every
// CGxDevice and never touches the renderer's passes. Everything is drawn in normalised screen space
// (x/width, 1 - y/height) with an identity view and a 0..1 ortho projection, as CGxDevice::ICursorDraw does.
//
// Settings, [WxlHost] in WTF\WXL\WarcraftXL.ini:
//   Enabled=1        0 turns the overlay and its hotkey off
//   Hotkey=120       virtual-key code that toggles the overlay (120 = F9)
//   UseScissor=1     per-draw clip rectangles; 0 draws unclipped (diagnostic fallback)

#include "WxlOverlay.hpp"

#include "WxlHost.hpp"

#include "imgui/imgui.h"

#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Gxu/Gxu.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CRect.h"

#include "WoW/Source/Extension/ExtensionMgr.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace wxlhost
{
    bool OverlayOpen()
    {
        return overlay::IsOpen();
    }
}

namespace wxlhost::overlay
{
    namespace
    {
        constexpr const char* kIni = ".\\WTF\\WXL\\WarcraftXL.ini";

        bool          s_installed  = false;
        bool          s_enabled    = true;
        bool          s_useScissor = true;
        bool          s_open       = false;
        uint32_t      s_hotkey     = VK_F9;
        ImGuiContext* s_context    = nullptr;
        bool          s_mouseSwallowed[5] = {};

        // The font atlas as the engine's texture callback hands it back: BGRA, tightly packed. The engine
        // re-latches it whenever it recreates the texture (device reset), so it has to outlive the texture.
        struct AtlasData
        {
            std::vector<uint8_t> bgra;
            uint32_t             stride = 0;
        };
        AtlasData s_atlas;
        CGxTex*   s_atlasTex = nullptr;

        void AtlasLatch(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void* userArg,
                        uint32_t& texelStrideInBytes, const void*& texels)
        {
            if (command == GxTex_Latch)
            {
                const AtlasData* atlas = static_cast<const AtlasData*>(userArg);
                texels             = atlas->bgra.data();
                texelStrideInBytes = atlas->stride;
            }
        }

        bool EnsureContext()
        {
            if (s_context) return true;

            IMGUI_CHECKVERSION();
            s_context = ImGui::CreateContext();
            if (!s_context) return false;

            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = ".\\WTF\\WXL\\WxlHostImGui.ini";
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.MouseDrawCursor = false; // the engine draws the cursor
            ImGui::StyleColorsDark();

            io.Fonts->AddFontDefault();
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            if (!pixels || width <= 0 || height <= 0) return false;

            // RGBA -> BGRA (GxTex_Argb8888 is D3DFMT_A8R8G8B8: B, G, R, A in memory).
            s_atlas.stride = static_cast<uint32_t>(width) * 4;
            s_atlas.bgra.resize(static_cast<size_t>(s_atlas.stride) * static_cast<size_t>(height));
            for (size_t i = 0; i + 3 < s_atlas.bgra.size(); i += 4)
            {
                s_atlas.bgra[i + 0] = pixels[i + 2];
                s_atlas.bgra[i + 1] = pixels[i + 1];
                s_atlas.bgra[i + 2] = pixels[i + 0];
                s_atlas.bgra[i + 3] = pixels[i + 3];
            }
            return true;
        }

        bool EnsureAtlasTexture()
        {
            if (s_atlasTex) return true;

            ImGuiIO& io = ImGui::GetIO();
            const CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
            if (!GxTexCreate(static_cast<uint32_t>(io.Fonts->TexWidth), static_cast<uint32_t>(io.Fonts->TexHeight),
                             GxTex_Argb8888, flags, &s_atlas, AtlasLatch, s_atlasTex) || !s_atlasTex)
            {
                s_atlasTex = nullptr;
                return false;
            }
            io.Fonts->SetTexID((ImTextureID)(uintptr_t)s_atlasTex);
            return true;
        }

        ImGuiKey MapKey(uintptr_t vk)
        {
            switch (vk)
            {
                case VK_TAB:      return ImGuiKey_Tab;
                case VK_LEFT:     return ImGuiKey_LeftArrow;
                case VK_RIGHT:    return ImGuiKey_RightArrow;
                case VK_UP:       return ImGuiKey_UpArrow;
                case VK_DOWN:     return ImGuiKey_DownArrow;
                case VK_PRIOR:    return ImGuiKey_PageUp;
                case VK_NEXT:     return ImGuiKey_PageDown;
                case VK_HOME:     return ImGuiKey_Home;
                case VK_END:      return ImGuiKey_End;
                case VK_INSERT:   return ImGuiKey_Insert;
                case VK_DELETE:   return ImGuiKey_Delete;
                case VK_BACK:     return ImGuiKey_Backspace;
                case VK_SPACE:    return ImGuiKey_Space;
                case VK_RETURN:   return ImGuiKey_Enter;
                case VK_ESCAPE:   return ImGuiKey_Escape;
                case VK_OEM_MINUS:  return ImGuiKey_Minus;
                case VK_OEM_PLUS:   return ImGuiKey_Equal;
                case VK_OEM_PERIOD: return ImGuiKey_Period;
                case VK_OEM_COMMA:  return ImGuiKey_Comma;
                default: break;
            }
            if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
            if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
            if (vk >= VK_F1 && vk <= VK_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
            return ImGuiKey_None;
        }

        void UpdateModifiers(ImGuiIO& io)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl,  (GetKeyState(VK_CONTROL) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Alt,   (GetKeyState(VK_MENU) & 0x8000) != 0);
        }

        int MouseButton(uint32_t message, uintptr_t wparam)
        {
            switch (message)
            {
                case WM_LBUTTONDOWN: case WM_LBUTTONUP: return 0;
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 1;
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 2;
                case WM_XBUTTONDOWN: case WM_XBUTTONUP: return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
                default: return -1;
            }
        }

        // ---- drawing ----------------------------------------------------------------------------------
        void Submit(const ImDrawData* data, float width, float height)
        {
            if (!data || data->TotalVtxCount <= 0) return;
            if (!g_theGxDevicePtr || !GxHasContext()) return;
            if (!EnsureAtlasTexture()) return;

            // Save what the paint handler left, to put it back.
            C44Matrix view;
            GxXformView(view);
            C44Matrix projection;
            GxXformProjection(projection);
            float minX, maxX, minY, maxY, minZ, maxZ;
            GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);

            C44Matrix ortho;
            GxuXformCreateOrtho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f, ortho);
            GxXformSetProjection(ortho);
            C44Matrix identity;
            identity.Identity();
            GxXformSetView(identity);
            GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

            g_theGxDevicePtr->RsPush();

            // The same fixed-function state the software cursor draws with (CGxDevice::ICursorDraw),
            // with alpha blending in place of its alpha-key.
            GxRsSet(GxRs_PolygonOffset, 0);
            GxRsSet(GxRs_NormalizeNormals, 0);
            GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(GxBlend_Alpha));
            GxUpdateAlphaRef(GxRs_AlphaRef);
            GxRsSet(GxRs_Lighting, 0);
            GxRsSet(GxRs_Fog, 0);
            GxRsSet(GxRs_DepthTest, 0);
            GxRsSet(GxRs_DepthWrite, 0);
            GxRsSet(GxRs_ColorWrite, 15);
            GxRsSet(GxRs_Culling, 0);
            GxRsSet(GxRs_ClipPlaneMask, 0);
            GxRsSet(GxRs_Texture0, static_cast<void*>(s_atlasTex));
            GxRsSet(GxRs_Texture1, nullptr);
            GxRsSet(GxRs_ColorOp0, 0);
            GxRsSet(GxRs_AlphaOp0, 0);
            GxRsSet(GxRs_TexGen0, 0);
            GxRsSet(GxRs_TextureShader0, 0);
            GxRsSet(GxRs_VertexShader, nullptr);
            GxRsSet(GxRs_PixelShader, nullptr);

            C44Matrix world;
            world.Identity();
            GxXformPush(GxXform_World, world);

            const float invW = width > 0.0f ? 1.0f / width : 0.0f;
            const float invH = height > 0.0f ? 1.0f / height : 0.0f;

            for (int n = 0; n < data->CmdListsCount; ++n)
            {
                const ImDrawList* list = data->CmdLists[n];
                const int vertexCount = list->VtxBuffer.Size;
                if (vertexCount <= 0 || vertexCount >= 0x10000) continue; // one pool buffer holds 64K vertices

                CGxBuf* vertexBuf = GxBufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPCT), static_cast<uint32_t>(vertexCount));
                CGxVertexPCT* vertices = reinterpret_cast<CGxVertexPCT*>(GxBufLock(vertexBuf));
                if (!vertices) continue;

                for (int i = 0; i < vertexCount; ++i)
                {
                    const ImDrawVert& src = list->VtxBuffer.Data[i];
                    CGxVertexPCT& dst = vertices[i];
                    dst.p.x  = src.pos.x * invW;
                    dst.p.y  = 1.0f - src.pos.y * invH;
                    dst.p.z  = 0.0f;
                    dst.tc.x = src.uv.x;
                    dst.tc.y = src.uv.y;
                    CImVector colour;
                    colour.r = static_cast<uint8_t>(src.col & 0xFF);
                    colour.g = static_cast<uint8_t>((src.col >> 8) & 0xFF);
                    colour.b = static_cast<uint8_t>((src.col >> 16) & 0xFF);
                    colour.a = static_cast<uint8_t>((src.col >> 24) & 0xFF);
                    GxFormatColor(colour);
                    dst.c = colour;
                }
                GxBufUnlock(vertexBuf, static_cast<uint32_t>(sizeof(CGxVertexPCT)) * static_cast<uint32_t>(vertexCount));

                for (int c = 0; c < list->CmdBuffer.Size; ++c)
                {
                    const ImDrawCmd& cmd = list->CmdBuffer.Data[c];
                    if (cmd.UserCallback || cmd.ElemCount == 0 || cmd.VtxOffset != 0) continue;
                    if (cmd.ElemCount >= 0x20000) continue; // one pool buffer holds 128K indices

                    // The scissor is in the same normalised, y-up space as the viewport set above.
                    // ASSUMPTION (verify at the first in-game run): CGxDevice::SetScissor takes
                    // viewport-space coordinates, as DayNight::RenderSky passes them.
                    if (s_useScissor)
                    {
                        CRect clip;
                        clip.minX = cmd.ClipRect.x * invW;
                        clip.maxX = cmd.ClipRect.z * invW;
                        clip.maxY = 1.0f - cmd.ClipRect.y * invH;
                        clip.minY = 1.0f - cmd.ClipRect.w * invH;
                        if (clip.maxX <= clip.minX || clip.maxY <= clip.minY) continue;
                        GxRsSet(GxRs_ScissorTest, 1);
                        g_theGxDevicePtr->SetScissor(clip);
                    }

                    CGxBuf* indexBuf = GxBufStream(GxPoolTarget_Index, sizeof(uint16_t), cmd.ElemCount);
                    uint16_t* indices = reinterpret_cast<uint16_t*>(GxBufLock(indexBuf));
                    if (!indices) continue;
                    std::memcpy(indices, list->IdxBuffer.Data + cmd.IdxOffset, sizeof(uint16_t) * cmd.ElemCount);
                    GxBufUnlock(indexBuf, static_cast<uint32_t>(sizeof(uint16_t)) * cmd.ElemCount);

                    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
                    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

                    CGxBatch batch;
                    batch.m_primType = GxPrim_Triangles;
                    batch.m_start    = 0;
                    batch.m_count    = cmd.ElemCount;
                    batch.m_minIndex = 0;
                    batch.m_maxIndex = static_cast<uint32_t>(vertexCount) - 1;
                    GxDraw(&batch, 1);
                }
            }

            GxRsSet(GxRs_ScissorTest, 0);
            GxXformPop(GxXform_World);
            g_theGxDevicePtr->RsPop();

            GxXformSetView(view);
            GxXformSetProjection(projection);
            GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
        }
    }

    namespace
    {
        // The shape OsInputSetMessageFilter takes (Engine/Source/Os/Input.h, see the patch).
        int32_t FilterThunk(void* /*window*/, uint32_t message, uintptr_t wparam, intptr_t lparam, intptr_t* result)
        {
            return HandleMessage(message, wparam, lparam, result) ? 1 : 0;
        }
    }

    void Install()
    {
        if (s_installed) return;
        s_installed = true;

        s_enabled    = GetPrivateProfileIntA("WxlHost", "Enabled", 1, kIni) != 0;
        s_useScissor = GetPrivateProfileIntA("WxlHost", "UseScissor", 1, kIni) != 0;
        s_hotkey     = static_cast<uint32_t>(GetPrivateProfileIntA("WxlHost", "Hotkey", VK_F9, kIni));

        if (s_enabled)
            OsInputSetMessageFilter(&FilterThunk);
        sExtensionMgr->Log("WxlHost: overlay %s, hotkey vk=0x%02X, scissor %s", s_enabled ? "enabled" : "disabled",
                           s_hotkey, s_useScissor ? "on" : "off");
    }

    bool IsOpen()
    {
        return s_open;
    }

    void SetOpen(bool open)
    {
        if (open == s_open) return;
        if (open)
        {
            if (!EnsureContext()) return;
            ImGui::GetIO().ClearInputKeys(); // nothing pressed while it was shut is "held"
        }
        s_open = open;
        if (!open) std::memset(s_mouseSwallowed, 0, sizeof s_mouseSwallowed);
    }

    void OnFrameEnd(float dtSec)
    {
        if (!s_open || !s_context || !g_theGxDevicePtr) return;

        const float width  = g_theGxDevicePtr->m_curWindowRect.maxX;
        const float height = g_theGxDevicePtr->m_curWindowRect.maxY;
        if (width < 1.0f || height < 1.0f) return;

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = dtSec > 0.0f ? dtSec : 1.0f / 60.0f;
        io.FontGlobalScale = std::clamp(height / 1080.0f, 1.0f, 2.5f);

        int mouseX = 0, mouseY = 0;
        OsInputGetMousePosition(&mouseX, &mouseY);
        io.AddMousePosEvent(static_cast<float>(mouseX), static_cast<float>(mouseY));

        ImGui::NewFrame();
        DrawPanels();
        ImGui::Render();
        Submit(ImGui::GetDrawData(), width, height);
    }

    bool HandleMessage(uint32_t message, uintptr_t wparam, intptr_t lparam, intptr_t* result)
    {
        if (!s_enabled) return false;

        // The hotkey: toggled on the first key-down, and its repeats and release are swallowed too so the
        // game never sees an F9 it did not press.
        if ((message == WM_KEYDOWN || message == WM_KEYUP) && wparam == s_hotkey)
        {
            if (message == WM_KEYDOWN && !(lparam & (1 << 30)))
                SetOpen(!s_open);
            *result = 0;
            return true;
        }

        if (!s_open || !s_context) return false;
        ImGuiIO& io = ImGui::GetIO();

        switch (message)
        {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            {
                UpdateModifiers(io);
                const ImGuiKey key = MapKey(wparam);
                const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
                if (io.WantCaptureKeyboard || io.WantTextInput)
                {
                    *result = 0;
                    return true;
                }
                return false;
            }

            case WM_CHAR:
            {
                if (!io.WantTextInput) return false;
                if (wparam >= 0x20)
                {
                    if (wparam < 0x80)
                    {
                        io.AddInputCharacter(static_cast<unsigned int>(wparam));
                    }
                    else
                    {
                        // The window is an ANSI window: a high byte is a character of the active code page.
                        const char ansi = static_cast<char>(wparam & 0xFF);
                        wchar_t wide = 0;
                        if (MultiByteToWideChar(CP_ACP, 0, &ansi, 1, &wide, 1) == 1)
                            io.AddInputCharacterUTF16(wide);
                    }
                }
                *result = 0;
                return true;
            }

            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
            {
                const int button = MouseButton(message, wparam);
                if (button < 0) return false;
                io.AddMouseButtonEvent(button, true);
                if (io.WantCaptureMouse)
                {
                    s_mouseSwallowed[button] = true;
                    *result = message == WM_XBUTTONDOWN ? TRUE : 0;
                    return true;
                }
                return false;
            }

            case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
            {
                const int button = MouseButton(message, wparam);
                if (button < 0) return false;
                io.AddMouseButtonEvent(button, false);
                if (s_mouseSwallowed[button])
                {
                    s_mouseSwallowed[button] = false;
                    *result = message == WM_XBUTTONUP ? TRUE : 0;
                    return true;
                }
                return false;
            }

            case WM_MOUSEWHEEL:
            {
                io.AddMouseWheelEvent(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA));
                if (io.WantCaptureMouse)
                {
                    *result = 0;
                    return true;
                }
                return false;
            }

            case WM_KILLFOCUS:
                io.ClearInputKeys();
                return false;

            default:
                return false;
        }
    }
}
