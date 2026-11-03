// wxl-seyris-tools x64 port: the entry points WxlHost registers in place of the WXL_Query/WXL_Load
// exports of the original DLL (see Extension.cpp).
#pragma once

#include "wxl/PluginApi.h"

const WXL_PluginInfo* __cdecl SeyrisToolsQuery(void);
int __cdecl SeyrisToolsLoad(const WXL_Api* api);
