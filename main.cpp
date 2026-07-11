#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <tchar.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "gui.h"
#include "wmi_helper.h"
#include <shlwapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shlwapi.lib")

// Direct3D data
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global Gui Controller
static GuiController* g_GuiController = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Create window class
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC | CS_HREDRAW | CS_VREDRAW, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"DiagnosePEClass", nullptr };
    RegisterClassExW(&wc);

    // Get primary screen resolution for borderless fullscreen window
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create borderless fullscreen window (CPM Diagnose Shell)
    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"CPM Diagnose Shell",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // 2. Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // 3. Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Setup style
    g_GuiController = new GuiController();
    g_GuiController->Initialize();
    g_GuiController->ApplyTheme();

    // Load Segoe UI from Windows folder
    ImFont* fontRegular = nullptr;
    ImFont* fontBold = nullptr;
    char winFonts[MAX_PATH];
    GetWindowsDirectoryA(winFonts, MAX_PATH);
    std::string fontPath = std::string(winFonts) + "\\Fonts\\segoeui.ttf";
    std::string fontBoldPath = std::string(winFonts) + "\\Fonts\\segoeuib.ttf";

    if (PathFileExistsA(fontPath.c_str())) {
        fontRegular = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f);
    }
    // Backup default font
    if (!fontRegular) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main loop
    bool isRunning = true;
    bool isMaximized = false;

    while (isRunning) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                isRunning = false;
            }
        }
        if (!isRunning) break;

        // Start new frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        g_GuiController->Update();

        // Control flags triggered by ImGui UI
        bool triggerClose = false;
        bool triggerMinimize = false;
        bool triggerMaximize = false;

        g_GuiController->Draw(hwnd, isMaximized, triggerClose, triggerMinimize, triggerMaximize);

        // Process titlebar commands
        if (triggerClose) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        if (triggerMinimize) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        if (triggerMaximize) {
            WINDOWPLACEMENT wp = { sizeof(wp) };
            GetWindowPlacement(hwnd, &wp);
            if (wp.showCmd == SW_SHOWMAXIMIZED) {
                ShowWindow(hwnd, SW_RESTORE);
                isMaximized = false;
            } else {
                ShowWindow(hwnd, SW_SHOWMAXIMIZED);
                isMaximized = true;
            }
        }

        // Render
        ImGui::Render();
        const float clear_color[4] = { 0.043f, 0.051f, 0.098f, 1.00f }; // Midnight Blue background
        if (g_mainRenderTargetView != nullptr) {
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // Cleanup ImGui
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_GuiController) {
        delete g_GuiController;
        g_GuiController = nullptr;
    }

    // Cleanup WMI if initialized
    WmiHelper::Uninitialize();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    
    // 1. Create Device and Context only
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    
    if (FAILED(hr)) {
        // Fallback to WARP software renderer for any failure (essential under bare WinPE environments)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    
    if (FAILED(hr)) {
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"D3D11CreateDevice fehlgeschlagen!\nEs konnte weder ein Hardware- noch ein WARP-Software-Device erstellt werden.\nHRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errMsg, L"CPM DiagnosePE - D3D11 Fehler", MB_OK | MB_ICONERROR);
        return false;
    }

    // 2. Now create the Swap Chain manually
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = GetSystemMetrics(SM_CXSCREEN);   // Set explicit width to avoid auto-query failures
    sd.BufferDesc.Height = GetSystemMetrics(SM_CYSCREEN); // Set explicit height to avoid auto-query failures
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0; // Disable mode switching which fails on basic virtual display drivers
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // Get DXGI Factory to create swap chain
    IDXGIDevice* pDxgiDevice = nullptr;
    hr = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));
    if (FAILED(hr)) {
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"QueryInterface für IDXGIDevice fehlgeschlagen!\nHRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errMsg, L"CPM DiagnosePE - DXGI Fehler", MB_OK | MB_ICONERROR);
        return false;
    }

    IDXGIAdapter* pDxgiAdapter = nullptr;
    hr = pDxgiDevice->GetParent(IID_PPV_ARGS(&pDxgiAdapter));
    pDxgiDevice->Release();
    if (FAILED(hr)) {
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"GetParent für IDXGIAdapter fehlgeschlagen!\nHRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errMsg, L"CPM DiagnosePE - DXGI Fehler", MB_OK | MB_ICONERROR);
        return false;
    }

    IDXGIFactory* pDxgiFactory = nullptr;
    hr = pDxgiAdapter->GetParent(IID_PPV_ARGS(&pDxgiFactory));
    pDxgiAdapter->Release();
    if (FAILED(hr)) {
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"GetParent für IDXGIFactory fehlgeschlagen!\nHRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errMsg, L"CPM DiagnosePE - DXGI Fehler", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = pDxgiFactory->CreateSwapChain(g_pd3dDevice, &sd, &g_pSwapChain);
    pDxgiFactory->Release();

    if (FAILED(hr)) {
        wchar_t errMsg[256];
        swprintf_s(errMsg, L"CreateSwapChain (Manueller SwapChain) fehlgeschlagen!\nHRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errMsg, L"CPM DiagnosePE - SwapChain Fehler", MB_OK | MB_ICONERROR);
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer != nullptr) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_NCCALCSIZE: {
        if (wParam == TRUE) return 0;
        break;
    }
    case WM_NCHITTEST: {
        return HTCLIENT; // Entire window is client area, prevents moving/dragging
    }
    case WM_GETMINMAXINFO: {
        return 0;
    }
    case WM_SIZE: {
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    }
    case WM_SYSCOMMAND: {
        if ((wParam & 0xFFF0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
