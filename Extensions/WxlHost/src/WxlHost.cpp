// WxlHost: entry point. See WxlHost.hpp for what this extension is and is not.
//
// Load order: the generated loader calls AddWxlHostExtension() at startup. The WXL plugins registered
// here are loaded at the FIRST frame (WxlApi.cpp), when the graphics device and every other extension
// exist, which is the point the WXL contract tells modules to resolve each other's interfaces.

#include "WxlHost.hpp"
#include "WxlOverlay.hpp"

#include "seyris/SeyrisTools.hpp"

#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

class WxlHostRenderScript : public RenderScript {
    public:
    WxlHostRenderScript() : RenderScript("WxlHostRenderScript") {}

    void OnFrameBegin(float elapsedSec) override {
        m_dt = elapsedSec;
        wxlhost::FrameBegin(elapsedSec);
    }

    // Screen-space overlays go here: every layer has painted and the scene is not presented yet.
    void OnFrameEnd() override {
        wxlhost::FrameEnd();
        wxlhost::overlay::OnFrameEnd(m_dt);
    }

    private:
    float m_dt = 0.0f;
};

}

void AddWxlHostExtension() {
    wxlhost::overlay::Install();

    // Plugins ported from WXL DLLs register here instead of exporting WXL_Query/WXL_Load.
    wxlhost::RegisterPlugin("wxl-seyris-tools", &SeyrisToolsQuery, &SeyrisToolsLoad);

    new WxlHostRenderScript();
    sExtensionMgr->Log("WxlHost: registered (WXL API %d, client build %d)", WXL_API_VERSION, WXL_CLIENT_BUILD);
}
