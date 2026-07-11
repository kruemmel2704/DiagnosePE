#include "gui.h"
#include "system_repair.h"
#include "imgui.h"
#include <windows.h>
#include <commdlg.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

static std::wstring NarrowToWide(const std::string& str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    // Remove null terminator
    if (!wstr.empty() && wstr.back() == 0) wstr.pop_back();
    return wstr;
}

static std::string WideToNarrow(const std::wstring& wstr) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
    if (!str.empty() && str.back() == 0) str.pop_back();
    return str;
}

GuiController::GuiController() {
    cpuLoadHistory.resize(60, 0.0f);
    cpuTempHistory.resize(60, 0.0f);
    gpuLoadHistory.resize(60, 0.0f);
    gpuTempHistory.resize(60, 0.0f);
    ramHistory.resize(60, 0.0f);

    lastSecondTick = std::chrono::steady_clock::now();
    statusClearTime = std::chrono::steady_clock::now();
}

GuiController::~GuiController() {
    stressTest.Stop();
}

void GuiController::Initialize() {
    // Query admin status
    isAdmin = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD size;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }

    currentDiag = monitor.GetDiagnostics();
    currentBattery = BatteryService::GetBatteryInfo();
    UpdateClock();
}

void GuiController::SetStatus(const std::wstring& msg, bool isError) {
    statusMessage = msg;
    statusIsError = isError;
    statusClearTime = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

void GuiController::UpdateClock() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);

    wchar_t buf[128];
    // HH:mm:ss - dd.MM.yyyy
    wcsftime(buf, sizeof(buf) / sizeof(wchar_t), L"%H:%M:%S - %d.%m.%Y", &timeinfo);
    currentTimeStr = buf;
}

void GuiController::PushHistory(std::vector<float>& vec, float val, int maxSize) {
    if (vec.size() >= (size_t)maxSize) {
        vec.erase(vec.begin());
    }
    vec.push_back(val);
}

void GuiController::Update() {
    // 1. Tick telemetry once a second
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSecondTick).count() >= 1000) {
        lastSecondTick = now;
        TickOneSecond();
    }

    // 2. Clear status message if expired
    if (!statusIsError && now > statusClearTime && statusMessage != L"Bereit") {
        statusMessage = L"Bereit";
    }
}

void GuiController::TickOneSecond() {
    // Refresh core metrics
    currentDiag = monitor.GetDiagnostics();
    currentBattery = BatteryService::GetBatteryInfo();
    UpdateClock();

    // Push into history graphs
    PushHistory(cpuLoadHistory, currentDiag.cpuAverageLoad);
    PushHistory(cpuTempHistory, currentDiag.cpuAverageTemp > 0 ? currentDiag.cpuAverageTemp : 0.0f);
    PushHistory(gpuLoadHistory, currentDiag.gpuLoad);
    PushHistory(gpuTempHistory, currentDiag.gpuTemp > 0 ? currentDiag.gpuTemp : 0.0f);
    PushHistory(ramHistory, currentDiag.ramPercent);

    // Dynamic thread count setup
    if (cpuStressSelectedThreads > (int)std::thread::hardware_concurrency()) {
        cpuStressSelectedThreads = std::thread::hardware_concurrency();
    }

    // Handle Values Recording
    if (isRecording) {
        auto dur = std::chrono::system_clock::now() - recordingStartTime;
        int secs = (int)std::chrono::duration_cast<std::chrono::seconds>(dur).count();
        int hrs = secs / 3600;
        int mins = (secs % 3600) / 60;
        int s = secs % 60;
        
        wchar_t buf[32];
        swprintf_s(buf, L"%02d:%02d:%02d", hrs, mins, s);
        recordingTimeDisplay = buf;

        // Record a point
        PdfDataPoint pt;
        pt.timeStr = currentTimeStr.substr(0, 8); // Just HH:mm:ss
        pt.cpuLoad = currentDiag.cpuAverageLoad;
        pt.cpuTemp = currentDiag.cpuAverageTemp;
        pt.gpuLoad = currentDiag.gpuLoad;
        pt.gpuTemp = currentDiag.gpuTemp;
        pt.ramPercent = currentDiag.ramPercent;
        recordedData.push_back(pt);
    }

    // Handle Battery Test Countdown
    if (isBatteryTestRunning) {
        batteryTestRemainingSeconds--;
        batteryTestCurrentPercent = currentBattery.currentChargePercent;

        // Log entry every 60 seconds
        if (batteryTestRemainingSeconds % 60 == 0) {
            double drop = batteryTestStartPercent - batteryTestCurrentPercent;
            if (drop < 0) drop = 0;

            wchar_t logBuf[128];
            int remMins = batteryTestRemainingSeconds / 60;
            int remSecs = batteryTestRemainingSeconds % 60;
            
            auto nowTime = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(nowTime);
            struct tm ti; localtime_s(&ti, &t);
            
            swprintf_s(logBuf, L"[%02d:%02d:%02d] - Verbleibende Zeit: %02d:%02d | Ladestand: %.0f%% (-%.0f%%)", 
                ti.tm_hour, ti.tm_min, ti.tm_sec, remMins, remSecs, batteryTestCurrentPercent, drop);
            batteryTestLog.push_back(logBuf);

            if (currentBattery.isConnectedToAc) {
                swprintf_s(logBuf, L"[%02d:%02d:%02d] - Warnung: Netzbetrieb aktiv! Bitte das Ladekabel trennen.",
                    ti.tm_hour, ti.tm_min, ti.tm_sec);
                batteryTestLog.push_back(logBuf);
            }
        }

        if (batteryTestRemainingSeconds <= 0) {
            // Finished successfully
            isBatteryTestRunning = false;
            batteryTestCompleted = true;

            double drop = batteryTestStartPercent - batteryTestCurrentPercent;
            double dischargeRate = drop * 2.0; // extrapolated over 1 hour (since test is 30 mins)
            double estLife = (dischargeRate > 0) ? (batteryTestCurrentPercent / dischargeRate) : 0.0;

            wchar_t res[256];
            swprintf_s(res, L"Testergebnis:\n\u2022 Batterie-Entladung: %.0f%% in 30 Minuten\n\u2022 Hochgerechnete Entladungsrate: %.1f%% pro Stunde\n\u2022 Gesch\u00E4tzte Restlaufzeit unter Last: %.1f Stunden",
                drop, dischargeRate, estLife);
            batteryTestResultsStr = res;
            
            batteryTestLog.push_back(L"Batterietest erfolgreich abgeschlossen.");
            SetStatus(L"Batterietest abgeschlossen!", false);
        }
    }

    // Handle CPU Stress Countdown
    if (isCpuStressRunning) {
        if (cpuStressDurationIndex != 4) { // Not infinite
            cpuStressRemainingSeconds--;
            if (cpuStressRemainingSeconds <= 0) {
                isCpuStressRunning = false;
                stressTest.Stop();
                cpuStressCompleted = true;
                cpuStressResultsStr = L"Test abgeschlossen! " + std::to_wstring(cpuStressSelectedThreads) + L" Threads liefen erfolgreich.";
                cpuStressLog.push_back(L"Stresstest erfolgreich beendet.");
            }
        }

        // Periodic logging in CPU stress (every 5 seconds)
        auto nowTicks = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(nowTicks - lastCpuStressLogTime).count() >= 5) {
            lastCpuStressLogTime = nowTicks;

            auto nowTime = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(nowTime);
            struct tm ti; localtime_s(&ti, &t);

            wchar_t logBuf[128];
            swprintf_s(logBuf, L"[%02d:%02d:%02d] - CPU-Temp: %.1f \u00B0C | CPU-Auslastung: %.1f %%",
                ti.tm_hour, ti.tm_min, ti.tm_sec, currentDiag.cpuAverageTemp, currentDiag.cpuAverageLoad);
            cpuStressLog.push_back(logBuf);
        }
    }
}

void GuiController::Draw(void* hwnd, bool& isMaximized, bool& triggerClose, bool& triggerMinimize, bool& triggerMaximize) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    // 1. Draw Fullscreen Desktop Workspace Window
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    // Style adjustments for transparent fullscreen background
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGui::Begin("DesktopBackground", nullptr, bgFlags);
    
    // Execute sub-draws
    DrawDesktop(hwnd, viewport->Size.x, viewport->Size.y, triggerClose, triggerMinimize);
    
    ImGui::End();
    ImGui::PopStyleVar(2);

    // 2. Draw Floating Windows
    // Dashboard
    if (showDashboard && !minDashboard) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusDashboard ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("Dashboard - Systemübersicht", &showDashboard, ImGuiWindowFlags_NoCollapse)) {
            focusDashboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawDashboard(hwnd);
        } else {
            focusDashboard = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Tools
    if (showTools && !minTools) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusTools ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("Hardware-Tools", &showTools, ImGuiWindowFlags_NoCollapse)) {
            focusTools = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawTools(hwnd);
        } else {
            focusTools = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Repair
    if (showRepair && !minRepair) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusRepair ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("Systemreparatur & Wartung", &showRepair, ImGuiWindowFlags_NoCollapse)) {
            focusRepair = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawRepair(hwnd);
        } else {
            focusRepair = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Battery
    if (showBattery && !minBattery) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusBattery ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("Batterie-Test & Zustand", &showBattery, ImGuiWindowFlags_NoCollapse)) {
            focusBattery = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawBattery(hwnd);
        } else {
            focusBattery = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Stress
    if (showStress && !minStress) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusStress ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("CPU Stresstest", &showStress, ImGuiWindowFlags_NoCollapse)) {
            focusStress = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawCpuStress(hwnd);
        } else {
            focusStress = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Guide
    if (showGuide && !minGuide) {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Border, focusGuide ? ImVec4(0.00f, 0.76f, 1.00f, 0.80f) : ImVec4(0.12f, 0.16f, 0.28f, 1.00f));
        if (ImGui::Begin("System-Handbuch", &showGuide, ImGuiWindowFlags_NoCollapse)) {
            focusGuide = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            DrawGuide();
        } else {
            focusGuide = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // 3. Draw Translucent Bottom Taskbar
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 48.0f));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 48.0f));
    ImGuiWindowFlags tbFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059f, 0.071f, 0.137f, 0.92f)); // 92% translucent slate
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGui::Begin("Taskbar", nullptr, tbFlags);
    DrawTaskbar(hwnd, viewport->Size.x, viewport->Size.y, triggerClose, triggerMinimize);
    ImGui::End();
    
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // 4. Draw Start Menu Panel
    if (showStartMenu) {
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 10.0f, viewport->Pos.y + viewport->Size.y - 48.0f - 460.0f));
        ImGui::SetNextWindowSize(ImVec2(300, 450));
        ImGuiWindowFlags smFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059f, 0.071f, 0.137f, 0.98f)); // Dark slate
        if (ImGui::Begin("StartMenu", &showStartMenu, smFlags)) {
            DrawStartMenu(viewport->Size.x, viewport->Size.y, triggerClose);
        }
        ImGui::End();
        ImGui::PopStyleColor();

        // Close start menu if clicked outside Start Menu or Taskbar bounds
        if (ImGui::IsMouseClicked(0)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            bool insideSM = (mousePos.x >= viewport->Pos.x + 10.0f && mousePos.x <= viewport->Pos.x + 310.0f &&
                             mousePos.y >= viewport->Pos.y + viewport->Size.y - 48.0f - 460.0f && mousePos.y <= viewport->Pos.y + viewport->Size.y - 48.0f - 10.0f);
            bool insideTB = (mousePos.x >= viewport->Pos.x && mousePos.x <= viewport->Pos.x + viewport->Size.x &&
                             mousePos.y >= viewport->Pos.y + viewport->Size.y - 48.0f && mousePos.y <= viewport->Pos.y + viewport->Size.y);
            if (!insideSM && !insideTB) {
                showStartMenu = false;
            }
        }
    }
}

void GuiController::DrawDesktop(void* hwnd, float screenWidth, float screenHeight, bool& triggerClose, bool& triggerMinimize) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background gradient: Deep Midnight Blue to Slate
    dl->AddRectFilledMultiColor(
        viewport->Pos, 
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), 
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.02f, 0.03f, 0.08f, 1.00f)), // top-left
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.04f, 0.05f, 0.10f, 1.00f)), // top-right
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.08f, 0.17f, 1.00f)), // bottom-right
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f, 0.12f, 0.22f, 1.00f))  // bottom-left
    );

    // Tech grid patterns
    ImU32 gridColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 0.76f, 1.00f, 0.02f));
    for (float x = 0; x < screenWidth; x += 80.0f) {
        dl->AddLine(ImVec2(viewport->Pos.x + x, viewport->Pos.y), ImVec2(viewport->Pos.x + x, viewport->Pos.y + screenHeight), gridColor, 1.0f);
    }
    for (float y = 0; y < screenHeight; y += 80.0f) {
        dl->AddLine(ImVec2(viewport->Pos.x, viewport->Pos.y + y), ImVec2(viewport->Pos.x + screenWidth, viewport->Pos.y + y), gridColor, 1.0f);
    }

    // Centered branding: Glowing CPM vector graphic logo
    ImVec2 center(viewport->Pos.x + screenWidth * 0.5f, viewport->Pos.y + screenHeight * 0.5f);
    dl->AddCircleFilled(center, 120.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 0.76f, 1.00f, 0.015f)));

    float bW = 280.0f;
    float bH = 110.0f;
    ImU32 bracketColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 0.76f, 1.00f, 0.12f));
    // Left bracket
    dl->AddLine(ImVec2(center.x - bW*0.5f, center.y - bH*0.5f), ImVec2(center.x - bW*0.4f, center.y - bH*0.5f), bracketColor, 2.0f);
    dl->AddLine(ImVec2(center.x - bW*0.5f, center.y - bH*0.5f), ImVec2(center.x - bW*0.5f, center.y + bH*0.5f), bracketColor, 2.0f);
    dl->AddLine(ImVec2(center.x - bW*0.5f, center.y + bH*0.5f), ImVec2(center.x - bW*0.4f, center.y + bH*0.5f), bracketColor, 2.0f);
    // Right bracket
    dl->AddLine(ImVec2(center.x + bW*0.5f, center.y - bH*0.5f), ImVec2(center.x + bW*0.4f, center.y - bH*0.5f), bracketColor, 2.0f);
    dl->AddLine(ImVec2(center.x + bW*0.5f, center.y - bH*0.5f), ImVec2(center.x + bW*0.5f, center.y + bH*0.5f), bracketColor, 2.0f);
    dl->AddLine(ImVec2(center.x + bW*0.5f, center.y + bH*0.5f), ImVec2(center.x + bW*0.4f, center.y + bH*0.5f), bracketColor, 2.0f);

    // Glowing CPM Text
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.76f, 1.00f, 0.22f));
    ImGui::SetWindowFontScale(3.2f);
    ImVec2 txtSize = ImGui::CalcTextSize("[ CPM ]");
    ImGui::SetCursorScreenPos(ImVec2(center.x - txtSize.x*0.5f, center.y - txtSize.y*0.5f - 12.0f));
    ImGui::Text("[ CPM ]");
    ImGui::SetWindowFontScale(1.0f);
    
    // Subtext
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.55f, 0.65f, 0.18f));
    ImGui::SetWindowFontScale(1.1f);
    ImVec2 subSize = ImGui::CalcTextSize("HARDWARE DIAGNOSTICS SHELL");
    ImGui::SetCursorScreenPos(ImVec2(center.x - subSize.x*0.5f, center.y + bH*0.5f + 12.0f));
    ImGui::Text("HARDWARE DIAGNOSTICS SHELL");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor(2);

    // 2. Render Desktop Icon grid (Left side)
    float startX = viewport->Pos.x + 30.0f;
    float startY = viewport->Pos.y + 30.0f;
    float stepY = 110.0f;

    DrawDesktopIcon("System Info", "DASHBOARD", startX, startY, showDashboard, minDashboard);
    DrawDesktopIcon("Hardware Tools", "TOOLS", startX, startY + stepY, showTools, minTools);
    DrawDesktopIcon("Reparatur", "REPAIR", startX, startY + stepY*2, showRepair, minRepair);
    DrawDesktopIcon("Batterie Test", "BATTERY", startX, startY + stepY*3, showBattery, minBattery);
    DrawDesktopIcon("CPU Stresstest", "STRESS", startX, startY + stepY*4, showStress, minStress);
    DrawDesktopIcon("Handbuch", "GUIDE", startX, startY + stepY*5, showGuide, minGuide);

    // Shell links (CMD / Device Manager)
    bool launchCmd = false; bool dummyCmdMin = false;
    DrawDesktopIcon("CMD Shell", "CMD", startX + 110.0f, startY, launchCmd, dummyCmdMin);
    if (launchCmd) {
        ShellExecuteW(NULL, L"open", L"cmd.exe", NULL, NULL, SW_SHOWNORMAL);
    }

    bool launchDevmgmt = false; bool dummyDevmgmtMin = false;
    DrawDesktopIcon("Geräte Manager", "DEVMGMT", startX + 110.0f, startY + stepY, launchDevmgmt, dummyDevmgmtMin);
    if (launchDevmgmt) {
        ShellExecuteW(NULL, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
    }

    // Power Actions (Reboot / Shutdown)
    bool triggerReboot = false; bool dummyRebootMin = false;
    DrawDesktopIcon("PC Neustarten", "REBOOT", startX + 110.0f, startY + stepY*2, triggerReboot, dummyRebootMin);
    if (triggerReboot) {
        system("shutdown /r /t 0");
    }

    bool triggerShutdown = false; bool dummyShutdownMin = false;
    DrawDesktopIcon("Herunterfahren", "POWER", startX + 110.0f, startY + stepY*3, triggerShutdown, dummyShutdownMin);
    if (triggerShutdown) {
        system("shutdown /s /t 0");
    }
}

void GuiController::DrawTaskbar(void* hwnd, float screenWidth, float screenHeight, bool& triggerClose, bool& triggerMinimize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Top border line
    dl->AddLine(
        ImGui::GetWindowPos(), 
        ImVec2(ImGui::GetWindowPos().x + screenWidth, ImGui::GetWindowPos().y), 
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.16f, 0.28f, 0.60f)), 
        1.5f
    );

    // 1. Start Menu Button
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + 8.0f, ImGui::GetWindowPos().y + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, showStartMenu ? ImVec4(0.00f, 0.70f, 0.90f, 1.00f) : ImVec4(0.08f, 0.35f, 0.53f, 1.00f));
    if (ImGui::Button("START", ImVec2(75, 32))) {
        showStartMenu = !showStartMenu;
    }
    ImGui::PopStyleColor();

    // 2. Render buttons for active floating windows
    float startX = 100.0f;
    auto DrawTaskbarButton = [&](const char* label, bool& showFlag, bool& minFlag, bool isAppFocused, const char* winTitle) {
        if (!showFlag) return;
        
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + startX, ImGui::GetWindowPos().y + 8.0f));
        
        ImVec4 btnColor = minFlag ? ImVec4(0.08f, 0.12f, 0.22f, 0.50f) : (isAppFocused ? ImVec4(0.12f, 0.22f, 0.42f, 1.00f) : ImVec4(0.08f, 0.16f, 0.31f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        
        if (ImGui::Button(label, ImVec2(130, 32))) {
            if (minFlag) {
                minFlag = false;
                ImGui::SetNextWindowFocus();
            } else if (isAppFocused) {
                minFlag = true;
            } else {
                ImGui::SetWindowFocus(winTitle);
            }
        }
        ImGui::PopStyleColor();

        // Focused glow line
        if (!minFlag) {
            ImVec2 pMin = ImGui::GetItemRectMin();
            ImVec2 pMax = ImGui::GetItemRectMax();
            dl->AddRectFilled(
                ImVec2(pMin.x + 8.0f, pMax.y - 3.0f), 
                ImVec2(pMax.x - 8.0f, pMax.y), 
                isAppFocused ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 0.76f, 1.00f, 1.00f)) : ImGui::ColorConvertFloat4ToU32(ImVec4(0.50f, 0.55f, 0.65f, 0.40f))
            );
        }

        startX += 138.0f;
    };

    DrawTaskbarButton("System Info", showDashboard, minDashboard, focusDashboard, "Dashboard - Systemübersicht");
    DrawTaskbarButton("Hardware Tools", showTools, minTools, focusTools, "Hardware-Tools");
    DrawTaskbarButton("Reparatur", showRepair, minRepair, focusRepair, "Systemreparatur & Wartung");
    DrawTaskbarButton("Batterie Test", showBattery, minBattery, focusBattery, "Batterie-Test & Zustand");
    DrawTaskbarButton("CPU Stresstest", showStress, minStress, focusStress, "CPU Stresstest");
    DrawTaskbarButton("Handbuch", showGuide, minGuide, focusGuide, "System-Handbuch");

    // 3. Tray Area (Right side)
    float trayWidth = 280.0f;
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + screenWidth - trayWidth, ImGui::GetWindowPos().y + 8.0f));
    ImGui::BeginChild("TrayTray", ImVec2(trayWidth - 8.0f, 32.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::SetCursorPosY(6.0f);

    // Shield status
    if (isAdmin) {
        ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "[ADMIN SHIELD]");
        ImGui::SameLine();
    }

    // Battery percentage
    if (currentBattery.isPresent) {
        std::string pct = std::to_string((int)currentBattery.currentChargePercent) + "%";
        if (currentBattery.isConnectedToAc) {
            ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "⚡ %s", pct.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.98f, 1.00f), "🔋 %s", pct.c_str());
        }
        ImGui::SameLine();
    }

    // Live clock
    std::string clk = WideToNarrow(currentTimeStr);
    ImGui::TextColored(ImVec4(0.88f, 0.92f, 0.98f, 1.00f), "| %s", clk.c_str());

    ImGui::EndChild();
}

void GuiController::DrawStartMenu(float screenWidth, float screenHeight, bool& triggerClose) {
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "CPM DIAGNOSE SHELL");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Technician info
    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.65f, 1.00f), "User:");
    ImGui::SameLine();
    ImGui::Text("Servicetechniker");

    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.65f, 1.00f), "Mode:");
    ImGui::SameLine();
    ImGui::TextColored(isAdmin ? ImVec4(0.00f, 0.76f, 1.00f, 1.00f) : ImVec4(0.88f, 0.92f, 0.98f, 1.00f), 
        isAdmin ? "SYSTEM (Admin)" : "TECHNICIAN");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Diagnostics Quick Shortcuts
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "DIAGNOSE SHORTCUTS");
    ImGui::Spacing();

    auto DrawStartMenuLink = [](const char* label, const wchar_t* target) {
        if (ImGui::Selectable(label)) {
            ShellExecuteW(NULL, L"open", target, NULL, NULL, SW_SHOWNORMAL);
        }
    };

    DrawStartMenuLink("Eingabeaufforderung (CMD)", L"cmd.exe");
    DrawStartMenuLink("Registrierungs-Editor (Regedit)", L"regedit.exe");
    DrawStartMenuLink("Datenträgerverwaltung (Diskmgmt)", L"diskmgmt.msc");
    DrawStartMenuLink("Taskmanager (Taskmgr)", L"taskmgr.exe");
    DrawStartMenuLink("Geräte-Manager (Devmgmt)", L"devmgmt.msc");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // System actions
    ImGui::TextColored(ImVec4(0.85f, 0.15f, 0.15f, 1.00f), "SYSTEM AKTIONEN");
    ImGui::Spacing();

    if (ImGui::Selectable("System Neustarten")) {
        system("shutdown /r /t 0");
    }
    if (ImGui::Selectable("System Herunterfahren")) {
        system("shutdown /s /t 0");
    }
    ImGui::Separator();
    if (ImGui::Selectable("Beenden (Shell Exit)")) {
        triggerClose = true;
    }
}

void GuiController::DrawDesktopIcon(const char* label, const char* symbolType, float x, float y, bool& showTarget, bool& minTarget) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    
    std::string btnId = std::string("##icon_") + label;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.15f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    
    bool clicked = ImGui::Button(btnId.c_str(), ImVec2(80, 85));
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // Draw vector icon shapes
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 iconPos(x + 40.0f, y + 30.0f);
    RenderVectorIcon(symbolType, iconPos, 32.0f);
    
    // Label text wrapping
    ImVec2 textPos(x + 5.0f, y + 62.0f);
    ImGui::SetCursorScreenPos(textPos);
    ImGui::PushTextWrapPos(x + 75.0f);
    
    float textWidth = ImGui::CalcTextSize(label).x;
    if (textWidth < 70.0f) {
        ImGui::SetCursorScreenPos(ImVec2(x + (80.0f - textWidth)*0.5f, y + 62.0f));
    }
    ImGui::Text("%s", label);
    ImGui::PopTextWrapPos();

    // Action execution on double click
    if (clicked && ImGui::IsMouseDoubleClicked(0)) {
        showTarget = true;
        minTarget = false; // Restore from taskbar
        ImGui::SetNextWindowFocus();
    }
}

void GuiController::RenderVectorIcon(const char* iconType, ImVec2 pos, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 cyanColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.00f, 0.76f, 1.00f, 1.00f));
    ImU32 redColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.15f, 0.15f, 1.00f));

    if (strcmp(iconType, "DASHBOARD") == 0) {
        // Speedometer arc
        dl->PathArcTo(pos, size*0.5f, 3.1415f * 0.9f, 3.1415f * 2.1f, 12);
        dl->PathStroke(cyanColor, 0, 2.5f);
        // dial needle
        dl->AddLine(pos, ImVec2(pos.x + size*0.35f*cosf(-0.7f), pos.y + size*0.35f*sinf(-0.7f)), cyanColor, 2.0f);
        dl->AddCircleFilled(pos, 3.5f, cyanColor);
    } 
    else if (strcmp(iconType, "TOOLS") == 0) {
        // Wrench line
        dl->AddLine(ImVec2(pos.x - size*0.35f, pos.y + size*0.35f), ImVec2(pos.x + size*0.15f, pos.y - size*0.15f), cyanColor, 3.0f);
        dl->AddCircle(ImVec2(pos.x + size*0.15f, pos.y - size*0.15f), 5.5f, cyanColor, 8, 2.5f);
        // Screwdriver line
        dl->AddLine(ImVec2(pos.x + size*0.35f, pos.y + size*0.35f), ImVec2(pos.x - size*0.15f, pos.y - size*0.15f), cyanColor, 2.0f);
        dl->AddRectFilled(ImVec2(pos.x + size*0.15f, pos.y + size*0.15f), ImVec2(pos.x + size*0.4f, pos.y + size*0.4f), cyanColor, 1.5f);
    } 
    else if (strcmp(iconType, "REPAIR") == 0) {
        // Medical emergency cross
        float w = size * 0.26f;
        float h = size * 0.72f;
        dl->AddRectFilled(ImVec2(pos.x - w*0.5f, pos.y - h*0.5f), ImVec2(pos.x + w*0.5f, pos.y + h*0.5f), cyanColor, 1.5f);
        dl->AddRectFilled(ImVec2(pos.x - h*0.5f, pos.y - w*0.5f), ImVec2(pos.x + h*0.5f, pos.y + w*0.5f), cyanColor, 1.5f);
    } 
    else if (strcmp(iconType, "BATTERY") == 0) {
        // Battery outline
        float bw = size * 0.72f;
        float bh = size * 0.40f;
        dl->AddRect(ImVec2(pos.x - bw*0.5f, pos.y - bh*0.5f), ImVec2(pos.x + bw*0.5f, pos.y + bh*0.5f), cyanColor, 2.0f, 0, 2.0f);
        // Battery positive cap
        dl->AddRectFilled(ImVec2(pos.x + bw*0.5f, pos.y - bh*0.2f), ImVec2(pos.x + bw*0.5f + 3.0f, pos.y + bh*0.2f), cyanColor, 1.0f);
        // Charging blocks
        dl->AddRectFilled(ImVec2(pos.x - bw*0.38f, pos.y - bh*0.28f), ImVec2(pos.x + bw*0.15f, pos.y + bh*0.28f), cyanColor, 1.0f);
    } 
    else if (strcmp(iconType, "STRESS") == 0) {
        // CPU square
        float cs = size * 0.58f;
        dl->AddRect(ImVec2(pos.x - cs*0.5f, pos.y - cs*0.5f), ImVec2(pos.x + cs*0.5f, pos.y + cs*0.5f), cyanColor, 2.0f, 0, 2.0f);
        dl->AddRectFilled(ImVec2(pos.x - cs*0.26f, pos.y - cs*0.26f), ImVec2(pos.x + cs*0.26f, pos.y + cs*0.26f), cyanColor, 1.5f);
        // Side pin tracks
        for (int i = -1; i <= 1; i++) {
            dl->AddLine(ImVec2(pos.x + i*cs*0.25f, pos.y - cs*0.5f), ImVec2(pos.x + i*cs*0.25f, pos.y - cs*0.5f - 3.5f), cyanColor, 1.5f);
            dl->AddLine(ImVec2(pos.x + i*cs*0.25f, pos.y + cs*0.5f), ImVec2(pos.x + i*cs*0.25f, pos.y + cs*0.5f + 3.5f), cyanColor, 1.5f);
            dl->AddLine(ImVec2(pos.x - cs*0.5f, pos.y + i*cs*0.25f), ImVec2(pos.x - cs*0.5f - 3.5f, pos.y + i*cs*0.25f), cyanColor, 1.5f);
            dl->AddLine(ImVec2(pos.x + cs*0.5f, pos.y + i*cs*0.25f), ImVec2(pos.x + cs*0.5f + 3.5f, pos.y + i*cs*0.25f), cyanColor, 1.5f);
        }
    } 
    else if (strcmp(iconType, "GUIDE") == 0) {
        // Document lines
        float gw = size * 0.58f;
        float gh = size * 0.72f;
        dl->AddRect(ImVec2(pos.x - gw*0.5f, pos.y - gh*0.5f), ImVec2(pos.x + gw*0.5f, pos.y + gh*0.5f), cyanColor, 2.0f, 0, 2.0f);
        dl->AddLine(ImVec2(pos.x - gw*0.3f, pos.y - gh*0.25f), ImVec2(pos.x + gw*0.3f, pos.y - gh*0.25f), cyanColor, 1.5f);
        dl->AddLine(ImVec2(pos.x - gw*0.3f, pos.y - gh*0.02f), ImVec2(pos.x + gw*0.1f, pos.y - gh*0.02f), cyanColor, 1.5f);
        dl->AddLine(ImVec2(pos.x - gw*0.3f, pos.y + gh*0.20f), ImVec2(pos.x + gw*0.3f, pos.y + gh*0.20f), cyanColor, 1.5f);
        dl->AddLine(ImVec2(pos.x - gw*0.3f, pos.y + gh*0.42f), ImVec2(pos.x + gw*0.2f, pos.y + gh*0.42f), cyanColor, 1.5f);
    } 
    else if (strcmp(iconType, "CMD") == 0) {
        // Command prompt
        float cw = size * 0.72f;
        float ch = size * 0.52f;
        dl->AddRect(ImVec2(pos.x - cw*0.5f, pos.y - ch*0.5f), ImVec2(pos.x + cw*0.5f, pos.y + ch*0.5f), cyanColor, 2.0f, 0, 2.0f);
        // prompt '>'
        dl->AddLine(ImVec2(pos.x - cw*0.36f, pos.y - ch*0.22f), ImVec2(pos.x - cw*0.22f, pos.y - ch*0.08f), cyanColor, 1.5f);
        dl->AddLine(ImVec2(pos.x - cw*0.22f, pos.y - ch*0.08f), ImVec2(pos.x - cw*0.36f, pos.y + ch*0.06f), cyanColor, 1.5f);
        // flashing cursor
        dl->AddLine(ImVec2(pos.x - cw*0.16f, pos.y + ch*0.06f), ImVec2(pos.x - cw*0.02f, pos.y + ch*0.06f), cyanColor, 1.5f);
    }
    else if (strcmp(iconType, "DEVMGMT") == 0) {
        // Device Manager tree diagram
        dl->AddRectFilled(ImVec2(pos.x - 3.0f, pos.y - size*0.38f), ImVec2(pos.x + 3.0f, pos.y - size*0.18f), cyanColor, 1.0f);
        dl->AddLine(ImVec2(pos.x, pos.y - size*0.18f), ImVec2(pos.x, pos.y + size*0.32f), cyanColor, 2.0f);
        // Branch lines and nodes
        dl->AddLine(ImVec2(pos.x, pos.y - 1.0f), ImVec2(pos.x - size*0.28f, pos.y - 1.0f), cyanColor, 2.0f);
        dl->AddLine(ImVec2(pos.x - size*0.28f, pos.y - 1.0f), ImVec2(pos.x - size*0.28f, pos.y + size*0.12f), cyanColor, 2.0f);
        dl->AddRectFilled(ImVec2(pos.x - size*0.28f - 3.0f, pos.y + size*0.12f), ImVec2(pos.x - size*0.28f + 3.0f, pos.y + size*0.32f), cyanColor, 1.0f);

        dl->AddLine(ImVec2(pos.x, pos.y + size*0.12f), ImVec2(pos.x + size*0.28f, pos.y + size*0.12f), cyanColor, 2.0f);
        dl->AddLine(ImVec2(pos.x + size*0.28f, pos.y + size*0.12f), ImVec2(pos.x + size*0.28f, pos.y + size*0.32f), cyanColor, 2.0f);
        dl->AddRectFilled(ImVec2(pos.x + size*0.28f - 3.0f, pos.y + size*0.32f), ImVec2(pos.x + size*0.28f + 3.0f, pos.y + size*0.50f), cyanColor, 1.0f);
    }
    else if (strcmp(iconType, "POWER") == 0) {
        // Red Power circle/line
        dl->PathArcTo(pos, size*0.32f, -3.1415f * 0.32f, 3.1415f * 1.32f, 12);
        dl->PathStroke(redColor, 0, 2.5f);
        dl->AddLine(ImVec2(pos.x, pos.y - size*0.38f), ImVec2(pos.x, pos.y - size*0.05f), redColor, 2.5f);
    }
    else if (strcmp(iconType, "REBOOT") == 0) {
        // Cyan Circular reboot arrow
        dl->PathArcTo(pos, size*0.32f, -3.1415f * 0.1f, 3.1415f * 1.7f, 12);
        dl->PathStroke(cyanColor, 0, 2.5f);
        dl->AddLine(ImVec2(pos.x + size*0.32f, pos.y), ImVec2(pos.x + size*0.15f, pos.y - size*0.1f), cyanColor, 2.0f);
        dl->AddLine(ImVec2(pos.x + size*0.32f, pos.y), ImVec2(pos.x + size*0.45f, pos.y - size*0.1f), cyanColor, 2.0f);
    }
}

void GuiController::DrawDashboard(void* hwnd) {
    ImGui::Columns(2, "DashboardLayout", false);
    ImGui::SetColumnWidth(0, 360.0f);
    ImGui::SetColumnWidth(1, ImGui::GetContentRegionAvail().x - 360.0f);

    // Left child: System details
    ImGui::BeginChild("SysDetailsCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "SYSTEM-INFORMATIONEN");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto DrawField = [](const char* label, const std::wstring& val) {
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "%s:", label);
        ImGui::SameLine();
        std::string v = WideToNarrow(val);
        ImGui::TextWrapped("%s", v.c_str());
        ImGui::Spacing();
    };

    DrawField("Hersteller", currentDiag.sysManufacturer);
    DrawField("Modell", currentDiag.sysModel);
    DrawField("Seriennummer", currentDiag.sysSerialNumber);
    DrawField("Prozessor (CPU)", currentDiag.cpuName);
    
    std::wstring clockStr = std::to_wstring((int)currentDiag.cpuClockSpeed) + L" MHz";
    DrawField("CPU-Takt", clockStr);

    std::wstring ramStr = std::to_wstring(currentDiag.ramUsedGb).substr(0, 4) + L" / " + std::to_wstring(currentDiag.ramTotalGb).substr(0, 4) + L" GB";
    DrawField("Arbeitsspeicher", ramStr);

    DrawField("Motherboard", currentDiag.mbModel);
    DrawField("MB-Seriennummer", currentDiag.mbSerialNumber);
    DrawField("Grafikkarte (GPU)", currentDiag.gpuName);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Values Recording card inside
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "PR\u00DCFPROTOKOLL AUFZEICHNUNG");
    ImGui::Spacing();
    if (!isRecording) {
        if (ImGui::Button("Aufzeichnung starten", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            isRecording = true;
            recordedData.clear();
            recordingStartTime = std::chrono::system_clock::now();
            SetStatus(L"Werte-Aufzeichnung gestartet.", false);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.00f));
        if (ImGui::Button("Aufzeichnung beenden & PDF speichern", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            isRecording = false;
            SetStatus(L"Aufzeichnung beendet.", false);

            if (recordedData.empty()) {
                SetStatus(L"Keine Datenpunkte aufgezeichnet.", true);
            } else {
                showPdfSaveModal = true;
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
        std::string recTimeStr = WideToNarrow(recordingTimeDisplay);
        ImGui::Text("Laufende Aufzeichnung: %s", recTimeStr.c_str());
        ImGui::Text("Erfasste Messpunkte: %zu", recordedData.size());
    }

    ImGui::EndChild();

    if (showPdfSaveModal) {
        ImGui::OpenPopup("PDF-Bericht speichern");
        showPdfSaveModal = false;
    }
    if (ImGui::BeginPopupModal("PDF-Bericht speichern", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Pfad für PDF-Bericht eingeben:");
        ImGui::InputText("##pdfpath", pdfSavePath, IM_ARRAYSIZE(pdfSavePath));
        ImGui::Spacing();
        
        if (ImGui::Button("Speichern", ImVec2(120, 0))) {
            std::string pathNarrow(pdfSavePath);
            std::wstring savePath = NarrowToWide(pathNarrow);
            
            auto nowSys = std::chrono::system_clock::now();
            auto start_t = std::chrono::system_clock::to_time_t(recordingStartTime);
            auto end_t = std::chrono::system_clock::to_time_t(nowSys);
            struct tm ti_s, ti_e;
            localtime_s(&ti_s, &start_t);
            localtime_s(&ti_e, &end_t);
            
            wchar_t startStr[64], endStr[64];
            wcsftime(startStr, sizeof(startStr)/sizeof(wchar_t), L"%d.%m.%Y %H:%M:%S", &ti_s);
            wcsftime(endStr, sizeof(endStr)/sizeof(wchar_t), L"%d.%m.%Y %H:%M:%S", &ti_e);

            bool ok = PdfWriter::GenerateSystemReport(
                savePath,
                currentDiag.cpuName,
                currentDiag.gpuName,
                currentDiag.ramTotalGb > 0 ? (std::to_wstring(currentDiag.ramTotalGb).substr(0, 4) + L" GB") : L"-- GB",
                currentDiag.sysModel,
                currentDiag.sysSerialNumber,
                currentDiag.mbModel,
                startStr,
                endStr,
                recordingTimeDisplay,
                recordedData
            );

            if (ok) {
                SetStatus(L"PDF-Prüfbericht erfolgreich gespeichert!", false);
            } else {
                SetStatus(L"Fehler beim Schreiben des PDF-Berichts.", true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Abbrechen", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Right child: Real-time graphs
    ImGui::NextColumn();
    ImGui::BeginChild("DashboardGraphs", ImVec2(0, 0), false);

    auto DrawRealtimeGraph = [](const char* title, const std::vector<float>& history, float val, const char* unit, ImVec4 color, float maxRange = 100.0f) {
        ImGui::BeginChild(title, ImVec2(ImGui::GetContentRegionAvail().x, 105), true);
        ImGui::TextColored(color, "%s", title);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.96f, 0.98f, 1.00f), " | Aktuell: %.1f %s", val, unit);
        
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
        ImGui::PlotLines("##Line", history.data(), (int)history.size(), 0, NULL, 0.0f, maxRange, ImVec2(ImGui::GetContentRegionAvail().x, 50));
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::Spacing();
    };

    DrawRealtimeGraph("CPU Auslastung", cpuLoadHistory, currentDiag.cpuAverageLoad, "%", ImVec4(0.00f, 0.76f, 1.00f, 1.00f));
    DrawRealtimeGraph("CPU Temperatur", cpuTempHistory, currentDiag.cpuAverageTemp, "\u00B0C", ImVec4(0.96f, 0.62f, 0.043f, 1.0f), 105.0f);
    
    bool hasGpu = (currentDiag.gpuName.find(L"Keine") == std::wstring::npos);
    if (hasGpu) {
        DrawRealtimeGraph("GPU Auslastung", gpuLoadHistory, currentDiag.gpuLoad, "%", ImVec4(0.00f, 0.76f, 1.00f, 1.00f));
        DrawRealtimeGraph("GPU Temperatur", gpuTempHistory, currentDiag.gpuTemp, "\u00B0C", ImVec4(0.96f, 0.62f, 0.043f, 1.0f), 105.0f);
    }

    // RAM usage progress bar
    ImGui::BeginChild("RAM Info", ImVec2(ImGui::GetContentRegionAvail().x, 80), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "RAM Belegung");
    ImGui::SameLine();
    std::string ramInfoDisplay = WideToNarrow(std::to_wstring(currentDiag.ramUsedGb).substr(0, 4) + L" / " + std::to_wstring(currentDiag.ramTotalGb).substr(0, 4) + L" GB");
    ImGui::Text(" | %s", ramInfoDisplay.c_str());
    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.00f, 0.76f, 1.00f, 1.00f));
    ImGui::ProgressBar(currentDiag.ramPercent / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x, 22), "");
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::EndChild();
}

void GuiController::DrawTools(void* hwnd) {
    ImGui::BeginChild("TestingToolsList", ImVec2(0, 0), false);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "STRESSTEST & DIAGNOSE UTILITIES");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto& tools = launcher.GetTools();
    for (auto& tool : tools) {
        std::string key = WideToNarrow(tool.key);
        std::string name = WideToNarrow(tool.name);
        std::string desc = WideToNarrow(tool.description);
        std::string status = WideToNarrow(tool.GetDisplayStatus());

        ImGui::BeginChild(key.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 105), true);
        ImGui::Columns(2, key.c_str(), false);
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() - 220.0f);
        ImGui::SetColumnWidth(1, 200.0f);

        // Col 0: Text details
        ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "%s", name.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", desc.c_str());
        ImGui::Spacing();
        
        if (tool.IsFound()) {
            ImGui::TextColored(ImVec4(0.062f, 0.725f, 0.505f, 1.0f), "Status: %s", status.c_str());
            ImGui::SameLine();
            std::string pathNarrow = WideToNarrow(tool.path);
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), " (%s)", pathNarrow.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.937f, 0.266f, 0.266f, 1.0f), "Status: %s", status.c_str());
        }

        // Col 1: Action buttons
        ImGui::NextColumn();
        ImGui::SetCursorPosY(10.0f);
        if (tool.IsFound()) {
            if (ImGui::Button("Tool starten", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                std::wstring err;
                if (launcher.LaunchTool(tool.key, err)) {
                    SetStatus(L"Tool gestartet: " + tool.key, false);
                } else {
                    SetStatus(err, true);
                }
            }
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            ImGui::Button("Nicht verf\u00FCgbar", ImVec2(ImGui::GetContentRegionAvail().x, 26));
            ImGui::PopStyleVar();
        }
        ImGui::Spacing();
        if (ImGui::Button("Pfad auswählen", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
            pathSelectToolKey = WideToNarrow(tool.key);
            std::string currentPathN = WideToNarrow(tool.path);
            strcpy_s(toolManualPath, currentPathN.c_str());
            ImGui::OpenPopup("Pfad eingeben");
        }
        if (launcher.HasManualPath(tool.key)) {
            ImGui::Spacing();
            if (ImGui::Button("Pfad zurücksetzen", ImVec2(ImGui::GetContentRegionAvail().x, 20))) {
                launcher.ClearManualPath(tool.key);
                SetStatus(L"Pfad für " + tool.key + L" zurückgesetzt.", false);
            }
        }

        ImGui::EndChild();
        ImGui::Spacing();
    }

    if (ImGui::BeginPopupModal("Pfad eingeben", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Geben Sie den Pfad zur EXE-Datei manuell ein:");
        ImGui::InputText("##toolpath", toolManualPath, IM_ARRAYSIZE(toolManualPath));
        ImGui::Spacing();
        
        if (ImGui::Button("Übernehmen", ImVec2(120, 0))) {
            std::string pathN(toolManualPath);
            std::wstring filepath = NarrowToWide(pathN);
            std::wstring toolKeyW = NarrowToWide(pathSelectToolKey);
            launcher.SaveManualPath(toolKeyW, filepath);
            SetStatus(L"Manueller Pfad für " + toolKeyW + L" gespeichert.", false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Abbrechen", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void GuiController::DrawRepair(void* hwnd) {
    ImGui::BeginChild("SystemRepairPage", ImVec2(0, 0), false);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "WINDOWS SYSTEM REPARATUR-Shortcuts");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto DrawRepairCard = [&](const char* title, const char* desc, const wchar_t* cmdKey) {
        ImGui::BeginChild(title, ImVec2(ImGui::GetContentRegionAvail().x, 90), true);
        ImGui::Columns(2, title, false);
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() - 200.0f);
        ImGui::SetColumnWidth(1, 180.0f);

        // Col 0
        ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "%s", title);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", desc);

        // Col 1
        ImGui::NextColumn();
        ImGui::SetCursorPosY(20.0f);
        if (ImGui::Button("Befehl ausf\u00FChren", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            std::wstring descOut;
            if (SystemRepair::RunRepair(cmdKey, descOut)) {
                SetStatus(L"Wird ausgeführt: " + descOut, false);
            } else {
                SetStatus(L"Starten des Befehls fehlgeschlagen.", true);
            }
        }

        ImGui::EndChild();
        ImGui::Spacing();
    };

    DrawRepairCard("System File Checker (SFC)", 
                   "Überprüft alle geschützten Systemdateien und ersetzt beschädigte oder falsche Versionen durch Originaldateien (SFC /scannow).", 
                   L"sfc");
    
    DrawRepairCard("DISM Restore Health", 
                   "Scant das Windows-Image auf Beschädigungen und repariert sie automatisch online über Windows Update (DISM /RestoreHealth).", 
                   L"dism_restore");

    DrawRepairCard("DISM Check Health", 
                   "Schnelle Überprüfung, ob das Image als beschädigt markiert ist (DISM /CheckHealth).", 
                   L"dism_check");

    DrawRepairCard("Chkdsk C:", 
                   "Überprüft das Dateisystem und die Dateisystem-Metadaten des Laufwerks C: auf Fehler im Nur-Lese-Modus (Chkdsk C:).", 
                   L"chkdsk");

    ImGui::EndChild();
}

void GuiController::DrawBattery(void* hwnd) {
    ImGui::Columns(2, "BatteryLayout", false);
    ImGui::SetColumnWidth(0, 360.0f);
    ImGui::SetColumnWidth(1, ImGui::GetContentRegionAvail().x - 360.0f);

    // Left child: Battery status metrics
    ImGui::BeginChild("BatteryInfoCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "BATTERIE DIAGNOSTIK");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto DrawField = [](const char* label, const std::wstring& val, ImVec4 color = ImVec4(0.95f, 0.96f, 0.98f, 1.00f)) {
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "%s:", label);
        ImGui::SameLine();
        std::string v = WideToNarrow(val);
        ImGui::TextColored(color, "%s", v.c_str());
        ImGui::Spacing();
    };

    if (currentBattery.isPresent) {
        ImVec4 hCol = (currentBattery.healthPercent > 80.0) ? ImVec4(0.062f, 0.725f, 0.505f, 1.0f) : ((currentBattery.healthPercent > 60.0) ? ImVec4(0.96f, 0.62f, 0.043f, 1.0f) : ImVec4(0.937f, 0.266f, 0.266f, 1.0f));
        DrawField("Gesundheitszustand (Health)", currentBattery.GetHealthDisplay(), hCol);
        DrawField("Hersteller", currentBattery.manufacturer);
        DrawField("Ger\u00E4tename", currentBattery.deviceName);
        DrawField("Design-Kapazit\u00E4t", currentBattery.GetDesignCapacityDisplay());
        DrawField("Volle Ladekapazit\u00E4t", currentBattery.GetFullChargeCapacityDisplay());
        DrawField("Batterie-Chemie", currentBattery.chemistry);
        
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        
        DrawField("Aktueller Ladestand", std::to_wstring((int)currentBattery.currentChargePercent) + L" %");
        DrawField("Stromquellen-Status", currentBattery.GetChargingStatusDisplay());
        DrawField("Verbleibende Laufzeit", currentBattery.GetRemainingTimeDisplay());
    } else {
        ImGui::TextColored(ImVec4(0.937f, 0.266f, 0.266f, 1.0f), "Keine Batterie im System erkannt.");
        ImGui::Text("Der Akku-Stresstest steht nur auf Laptops zur Verfügung.");
    }

    ImGui::EndChild();

    // Right child: Battery Stress test
    ImGui::NextColumn();
    ImGui::BeginChild("BatteryStressTestCard", ImVec2(0, 0), false);

    ImGui::BeginChild("BatteryTestPanel", ImVec2(ImGui::GetContentRegionAvail().x, 155), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "AKKU ENTLADUNGS-STRESSTEST (30 Minuten)");
    ImGui::Spacing();
    ImGui::TextWrapped("Dieser Test entl\u00E4dt den Akku unter konstanter Last durch Abspielen eines YouTube 4K Videos, um die reale Entladungsrate und Kapazit\u00E4t zu ermitteln.");
    ImGui::Spacing();

    if (currentBattery.isPresent) {
        if (!isBatteryTestRunning) {
            if (ImGui::Button("Entladungstest starten", ImVec2(180, 28))) {
                isBatteryTestRunning = true;
                batteryTestCompleted = false;
                batteryTestRemainingSeconds = 1800; // 30 mins
                batteryTestStartPercent = currentBattery.currentChargePercent;
                batteryTestCurrentPercent = currentBattery.currentChargePercent;
                batteryTestResultsStr = L"";
                batteryTestLog.clear();

                wchar_t logBuf[128];
                swprintf_s(logBuf, L"Akkutest gestartet bei %.0f%% Ladestand.", batteryTestStartPercent);
                batteryTestLog.push_back(logBuf);

                if (currentBattery.isConnectedToAc) {
                    batteryTestLog.push_back(L"Warnung: Ladekabel angeschlossen! Bitte abziehen.");
                }

                // Launch browser with video URL
                ShellExecuteW(NULL, L"open", L"https://www.youtube.com/watch?v=yq6c8h0Z3-Q", NULL, NULL, SW_SHOWNORMAL);
                SetStatus(L"YouTube-Stresstest im Browser gestartet.", false);
            }
            ImGui::SameLine();
            if (batteryTestCompleted) {
                if (ImGui::Button("PDF-Bericht speichern", ImVec2(180, 28))) {
                    wchar_t savePath[MAX_PATH] = L"Batteriebericht.pdf";
                    OPENFILENAMEW ofn = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = (HWND)hwnd;
                    ofn.lpstrFilter = L"PDF-Dateien (*.pdf)\0*.pdf\0";
                    ofn.lpstrFile = savePath;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
                    ofn.lpstrDefExt = L"pdf";

                    if (GetSaveFileNameW(&ofn)) {
                        int elapsed = 1800 - batteryTestRemainingSeconds;
                        int remMins = elapsed / 60;
                        int remSecs = elapsed % 60;
                        
                        auto nowSys = std::chrono::system_clock::now();
                        auto now_t = std::chrono::system_clock::to_time_t(nowSys);
                        auto start_t = std::chrono::system_clock::to_time_t(nowSys - std::chrono::seconds(elapsed));
                        struct tm ti_s, ti_e;
                        localtime_s(&ti_s, &start_t);
                        localtime_s(&ti_e, &now_t);

                        wchar_t startStr[64], endStr[64], durStr[64];
                        wcsftime(startStr, sizeof(startStr)/sizeof(wchar_t), L"%d.%m.%Y %H:%M:%S", &ti_s);
                        wcsftime(endStr, sizeof(endStr)/sizeof(wchar_t), L"%d.%m.%Y %H:%M:%S", &ti_e);
                        swprintf_s(durStr, L"%02d:%02d:%02d", elapsed / 3600, remMins, remSecs);

                        bool ok = PdfWriter::GenerateBatteryReport(
                            savePath,
                            currentDiag.cpuName,
                            currentDiag.gpuName,
                            currentDiag.ramTotalGb > 0 ? (std::to_wstring(currentDiag.ramTotalGb).substr(0, 4) + L" GB") : L"-- GB",
                            currentDiag.sysModel,
                            currentDiag.sysSerialNumber,
                            currentDiag.mbModel,
                            currentBattery.GetHealthDisplay(),
                            currentBattery.manufacturer,
                            currentBattery.deviceName,
                            currentBattery.chemistry,
                            currentBattery.GetDesignCapacityDisplay(),
                            currentBattery.GetFullChargeCapacityDisplay(),
                            startStr,
                            endStr,
                            durStr,
                            batteryTestStartPercent,
                            batteryTestCurrentPercent,
                            batteryTestLog
                        );

                        if (ok) {
                            SetStatus(L"PDF-Batteriebericht gespeichert!", false);
                        } else {
                            SetStatus(L"Fehler beim Erstellen des PDF-Berichts.", true);
                        }
                    }
                }
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.00f));
            if (ImGui::Button("Test abbrechen", ImVec2(180, 28))) {
                isBatteryTestRunning = false;
                batteryTestCompleted = true;
                
                int elapsed = 1800 - batteryTestRemainingSeconds;
                wchar_t logBuf[128];
                swprintf_s(logBuf, L"Test manuell abgebrochen nach %d Min. und %d Sek.", elapsed / 60, elapsed % 60);
                batteryTestLog.push_back(logBuf);

                double drop = batteryTestStartPercent - batteryTestCurrentPercent;
                wchar_t res[256];
                swprintf_s(res, L"Testergebnis (Abgebrochen):\n\u2022 Testdauer: %02d Min. %02d Sek.\n\u2022 Akku-Verbrauch: %.0f%%\n\u2022 Hochrechnung ungenau wegen vorzeitigem Abbruch.",
                    elapsed / 60, elapsed % 60, drop);
                batteryTestResultsStr = res;
                
                SetStatus(L"Akkutest abgebrochen.", false);
            }
            ImGui::PopStyleColor(2);
            
            ImGui::SameLine();
            int remMins = batteryTestRemainingSeconds / 60;
            int remSecs = batteryTestRemainingSeconds % 60;
            ImGui::Text("  Laufende Zeit: %02d:%02d", remMins, remSecs);
        }
    } else {
        ImGui::TextColored(ImVec4(0.937f, 0.266f, 0.266f, 1.0f), "Test nicht verf\u00FCgbar (Keine Batterie).");
    }
    
    ImGui::EndChild();
    ImGui::Spacing();

    // Results and logs
    if (batteryTestCompleted) {
        ImGui::BeginChild("BatteryTestResults", ImVec2(ImGui::GetContentRegionAvail().x, 90), true);
        ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "TESTERGEBNISSE");
        ImGui::Spacing();
        std::string resStr = WideToNarrow(batteryTestResultsStr);
        ImGui::TextWrapped("%s", resStr.c_str());
        ImGui::EndChild();
        ImGui::Spacing();
    }

    ImGui::BeginChild("BatteryLogChild", ImVec2(ImGui::GetContentRegionAvail().x, 0), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "CHRONOLOGISCHER TESTVERLAUF");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    for (auto it = batteryTestLog.rbegin(); it != batteryTestLog.rend(); ++it) {
        std::string l = WideToNarrow(*it);
        ImGui::Text("%s", l.c_str());
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void GuiController::DrawCpuStress(void* hwnd) {
    ImGui::Columns(2, "CpuStressLayout", false);
    ImGui::SetColumnWidth(0, 360.0f);
    ImGui::SetColumnWidth(1, ImGui::GetContentRegionAvail().x - 360.0f);

    // Left child: Controls
    ImGui::BeginChild("CpuStressControlsCard", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "CPU STRESSTEST KONTROLLEN");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Core count slider
    int maxThreads = std::thread::hardware_concurrency();
    if (maxThreads <= 0) maxThreads = 4;
    ImGui::Text("Stress Threads:");
    ImGui::SliderInt("##Threads", &cpuStressSelectedThreads, 1, maxThreads);
    ImGui::Spacing(); ImGui::Spacing();

    // Duration selector
    ImGui::Text("Dauer des Tests:");
    auto DrawDurationRadio = [&](const char* label, int index) {
        if (ImGui::RadioButton(label, cpuStressDurationIndex == index)) {
            cpuStressDurationIndex = index;
        }
        ImGui::Spacing();
    };

    DrawDurationRadio("30 Sekunden", 0);
    DrawDurationRadio("1 Minute", 1);
    DrawDurationRadio("5 Minuten", 2);
    DrawDurationRadio("10 Minuten", 3);
    DrawDurationRadio("Unbegrenzt (Manuell stoppen)", 4);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (!isCpuStressRunning) {
        if (ImGui::Button("CPU Stresstest starten", ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
            isCpuStressRunning = true;
            cpuStressCompleted = false;
            cpuStressResultsStr = L"";
            cpuStressLog.clear();

            // Set duration
            int dur = 60;
            switch (cpuStressDurationIndex) {
                case 0: dur = 30; break;
                case 1: dur = 60; break;
                case 2: dur = 300; break;
                case 3: dur = 600; break;
                case 4: dur = 999999; break; // Huge number
            }
            cpuStressRemainingSeconds = dur;
            lastCpuStressLogTime = std::chrono::steady_clock::now();

            auto nowTime = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(nowTime);
            struct tm ti; localtime_s(&ti, &t);

            wchar_t startLog[128];
            swprintf_s(startLog, L"[%02d:%02d:%02d] - Stresstest gestartet (%d Threads)", 
                ti.tm_hour, ti.tm_min, ti.tm_sec, cpuStressSelectedThreads);
            cpuStressLog.push_back(startLog);

            stressTest.Start(cpuStressSelectedThreads);
            SetStatus(L"CPU-Stresstest gestartet.", false);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.25f, 1.00f));
        if (ImGui::Button("Stresstest stoppen", ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
            isCpuStressRunning = false;
            stressTest.Stop();
            cpuStressCompleted = true;
            cpuStressResultsStr = L"Test abgebrochen.";
            cpuStressLog.push_back(L"Stresstest manuell abgebrochen.");
            SetStatus(L"CPU-Stresstest gestoppt.", false);
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing(); ImGui::Spacing();
        if (cpuStressDurationIndex != 4) {
            int remMins = cpuStressRemainingSeconds / 60;
            int remSecs = cpuStressRemainingSeconds % 60;
            ImGui::Text("Verbleibende Zeit: %02d:%02d", remMins, remSecs);
        } else {
            ImGui::Text("Laufzeit: Unbegrenzt");
        }
    }

    ImGui::EndChild();

    // Right child: real-time graph and log
    ImGui::NextColumn();
    ImGui::BeginChild("CpuStressOutputs", ImVec2(0, 0), false);

    ImGui::BeginChild("CpuStressRealtimeGraph", ImVec2(ImGui::GetContentRegionAvail().x, 105), true);
    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.043f, 1.0f), "CPU Temperaturw\u00E4chter");
    ImGui::SameLine();
    ImGui::Text(" | Aktuell: %.1f \u00B0C", currentDiag.cpuAverageTemp);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.96f, 0.62f, 0.043f, 1.0f));
    ImGui::PlotLines("##Line", cpuTempHistory.data(), (int)cpuTempHistory.size(), 0, NULL, 0.0f, 105.0f, ImVec2(ImGui::GetContentRegionAvail().x, 50));
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::Spacing();

    if (cpuStressCompleted) {
        ImGui::BeginChild("CpuStressResultsBox", ImVec2(ImGui::GetContentRegionAvail().x, 50), true);
        std::string resStr = WideToNarrow(cpuStressResultsStr);
        ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "%s", resStr.c_str());
        ImGui::EndChild();
        ImGui::Spacing();
    }

    ImGui::BeginChild("CpuStressLogsBox", ImVec2(ImGui::GetContentRegionAvail().x, 0), true);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "STRESSTEST EVENT-LOG");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    for (auto it = cpuStressLog.rbegin(); it != cpuStressLog.rend(); ++it) {
        std::string l = WideToNarrow(*it);
        ImGui::Text("%s", l.c_str());
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

void GuiController::DrawGuide() {
    ImGui::BeginChild("SystemGuidePage", ImVec2(0, 0), false);
    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "DIAGNOSTISCHER LEITFADEN & THERMISCHE SCHWELLENWERTE");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "1. CPU-Temperatur-Richtwerte:");
    ImGui::BulletText("Unter 55\u00B0C (Leerlauf): Hervorragende Kühlleistung.");
    ImGui::BulletText("55\u00B0C - 75\u00B0C (Normale Last): Normaler Arbeitsbereich unter Last.");
    ImGui::BulletText("75\u00B0C - 85\u00B0C (Hohe Last/Stresstest): Akzeptabel für Laptops; leicht erhöht für Desktop-PCs.");
    ImGui::BulletText("Über 85\u00B0C (Kritisch): Kühlung überprüfen! Risiko von Thermal Throttling oder Systemabschaltung.");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "2. GPU-Temperatur-Richtwerte:");
    ImGui::BulletText("Unter 45\u00B0C (Leerlauf): Lüfter abgeschaltet / Zero-Fan-Modus.");
    ImGui::BulletText("45\u00B0C - 78\u00B0C (Gaming/Last): Optimaler Betriebstemperaturbereich.");
    ImGui::BulletText("78\u00B0C - 84\u00B0C (Grenzwertig): GPU drosselt eventuell leicht die Leistung (Hot-Spot-Sensoren beachten).");
    ImGui::BulletText("Über 85\u00B0C (Kritisch): Grafikkarten-Kühler entstauben, Wärmeleitpaste prüfen.");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "3. Akku-Zustandsdiagnostik:");
    ImGui::BulletText("Akkugesundheit (Health) > 80%: Guter Zustand, keine spürbaren Alterungserscheinungen.");
    ImGui::BulletText("Health 50% - 80%: Akzeptabler Zustand, verringerte Laufzeiten. Austausch in Erwägung ziehen.");
    ImGui::BulletText("Health < 50%: Kritische Zellalterung. Akku kann plötzlich einbrechen oder sich aufblähen.");
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.00f, 0.76f, 1.00f, 1.00f), "4. Systemreparatur-Anleitung:");
    ImGui::BulletText("Bei Bluescreens (BSOD) oder Dateisystemfehlern immer zuerst SFC und DISM Check ausführen.");
    ImGui::BulletText("Das Reparieren kann je nach SSD/HDD-Geschwindigkeit 5 bis 20 Minuten dauern.");
    ImGui::BulletText("CMD-Fenster während des Prozesses nicht schließen!");

    ImGui::EndChild();
}

void GuiController::ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    colors[ImGuiCol_Text]                   = ImVec4(0.88f, 0.92f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.55f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.043f, 0.051f, 0.098f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.059f, 0.071f, 0.137f, 0.95f);
    colors[ImGuiCol_Border]                 = ImVec4(0.12f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    colors[ImGuiCol_FrameBg]                = ImVec4(0.09f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.12f, 0.16f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.16f, 0.22f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.043f, 0.051f, 0.098f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.043f, 0.051f, 0.098f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.043f, 0.051f, 0.098f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.16f, 0.22f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.20f, 0.28f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    
    colors[ImGuiCol_CheckMark]              = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.00f, 0.85f, 1.00f, 1.00f);
    
    colors[ImGuiCol_Button]                 = ImVec4(0.08f, 0.35f, 0.53f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.11f, 0.48f, 0.73f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    
    colors[ImGuiCol_Header]                 = ImVec4(0.09f, 0.16f, 0.31f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.12f, 0.22f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.16f, 0.30f, 0.55f, 1.00f);
    
    colors[ImGuiCol_Separator]              = ImVec4(0.12f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.16f, 0.22f, 0.40f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.16f, 0.22f, 0.40f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.20f, 0.28f, 0.50f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    
    colors[ImGuiCol_Tab]                    = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.09f, 0.16f, 0.31f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.09f, 0.16f, 0.31f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.059f, 0.071f, 0.137f, 1.00f);
    
    colors[ImGuiCol_PlotLines]              = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.00f, 0.90f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.00f, 0.90f, 1.00f, 1.00f);
    
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.09f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.12f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.09f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.16f, 0.30f, 0.55f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.00f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.043f, 0.051f, 0.098f, 0.60f);
    
    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(8.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 12.0f;
    
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
}

