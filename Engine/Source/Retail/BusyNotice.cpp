#include "Engine/Source/Retail/BusyNotice.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <map>
#include <atomic>

namespace Retail {
namespace {

constexpr DWORD kShowDelayMs = 400;   // reads shorter than this never show a window
constexpr DWORD kLingerMs = 1500;     // keep the window across back-to-back reads

std::mutex g_mutex;
struct Notice { std::wstring text; int priority; };
std::map<uint64_t, Notice> g_notices;
uint64_t g_nextId = 0;
DWORD g_firstBegin = 0, g_lastEnd = 0;
std::atomic<bool> g_stopping{false};

// Declared after the notice data so the painter stops before that data is freed.
struct NoticeWorker {
    std::thread thread;
    ~NoticeWorker() {
        g_stopping.store(true);
        if (thread.joinable()) thread.join();
    }
} g_worker;

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT rect;
        GetClientRect(window, &rect);
        HBRUSH background = CreateSolidBrush(RGB(20, 24, 34));
        FillRect(dc, &rect, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 215, 110));
        HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ previous = SelectObject(dc, font);
        std::wstring text;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            int priority = -1;
            for (const auto& entry : g_notices) {
                if (entry.second.priority >= priority) { text = entry.second.text; priority = entry.second.priority; }
            }
        }
        const unsigned dots = (GetTickCount() / 400) % 4;
        text += std::wstring(dots, L'.');
        DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        DeleteObject(font);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Run() {
    HWND window = nullptr;
    while (!g_stopping.load()) {
        Sleep(100);
        if (g_stopping.load()) break;
        bool expired = false;
        bool show = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            const DWORD now = GetTickCount();
            expired = g_notices.empty() && now - g_lastEnd > kLingerMs;
            show = !g_notices.empty() && now - g_firstBegin >= kShowDelayMs;
        }
        if (expired && window) { DestroyWindow(window); window = nullptr; }
        if (show && !window) {
            WNDCLASSW cls = {};
            cls.lpfnWndProc = Proc;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"WowRetailBusyNotice";
            cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514));
            RegisterClassW(&cls);
            const int width = 520, height = 54;
            window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP | WS_BORDER,
                (GetSystemMetrics(SM_CXSCREEN) - width) / 2, 40, width, height, nullptr, nullptr, cls.hInstance, nullptr);
            if (window) ShowWindow(window, SW_SHOWNOACTIVATE);
        }
        if (window) {
            ShowWindow(window, show ? SW_SHOWNOACTIVATE : SW_HIDE);
            InvalidateRect(window, nullptr, FALSE);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    if (window) DestroyWindow(window);
}

}

BusyNotice::BusyNotice(const char* text, int priority) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const DWORD now = GetTickCount();
    if (g_notices.empty()) g_firstBegin = now;
    m_id = ++g_nextId;
    g_notices.emplace(m_id, Notice{std::wstring(text, text + std::char_traits<char>::length(text)), priority});
    m_active = true;
    if (!g_worker.thread.joinable()) {
        g_worker.thread = std::thread(Run);
    }
}

BusyNotice::~BusyNotice() {
    if (!m_active) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notices.erase(m_id);
    g_lastEnd = GetTickCount();
}

}
#else
namespace Retail {
BusyNotice::BusyNotice(const char*, int) {}
BusyNotice::~BusyNotice() {}
}
#endif
