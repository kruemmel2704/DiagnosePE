#pragma once
#include "platform_compat.h"
#include "hardware_monitor.h"
#include "battery_service.h"
#include "stress_test.h"
#include "ram_test.h"
#include "tool_launcher.h"
#include "pdf_writer.h"
#include <chrono>
#include <vector>

class GuiController {
private:
    HardwareMonitor monitor;
    ToolLauncher launcher;
    StressTest stressTest;
    RamTest ramTest;

    HardwareDiagnostics currentDiag;
    BatteryInfo currentBattery;

    // Shell window management state
    bool showDashboard = true; // Open by default
    bool showTools = false;
    bool showRepair = false;
    bool showBattery = false;
    bool showStress = false;
    bool showRamTest = false;
    bool showGuide = false;
    bool showStartMenu = false;
    bool showPdfSaveModal = false;
    char pdfSavePath[260] = "C:\\Pruefbericht.pdf";
    std::string pathSelectToolKey = "";
    char toolManualPath[260] = "";

    // Minimize states
    bool minDashboard = false;
    bool minTools = false;
    bool minRepair = false;
    bool minBattery = false;
    bool minStress = false;
    bool minRamTest = false;
    bool minGuide = false;

    // Focus states from previous frame
    bool focusDashboard = false;
    bool focusTools = false;
    bool focusRepair = false;
    bool focusBattery = false;
    bool focusStress = false;
    bool focusRamTest = false;
    bool focusGuide = false;

    std::wstring statusMessage = L"Bereit";
    bool statusIsError = false;
    std::chrono::steady_clock::time_point statusClearTime;

    // Realtime plot data buffers (60 samples)
    std::vector<float> cpuLoadHistory;
    std::vector<float> cpuTempHistory;
    std::vector<float> gpuLoadHistory;
    std::vector<float> gpuTempHistory;
    std::vector<float> ramHistory;

    // Clock
    std::wstring currentTimeStr;
    bool isAdmin = false;

    // Values Recording
    bool isRecording = false;
    std::chrono::system_clock::time_point recordingStartTime;
    std::vector<PdfDataPoint> recordedData;
    std::wstring recordingTimeDisplay = L"00:00:00";

    // Battery Test
    bool isBatteryTestRunning = false;
    int batteryTestRemainingSeconds = 1800; // 30 mins
    double batteryTestStartPercent = 0.0;
    double batteryTestCurrentPercent = 0.0;
    bool batteryTestCompleted = false;
    std::wstring batteryTestResultsStr = L"";
    std::vector<std::wstring> batteryTestLog;
    std::chrono::steady_clock::time_point lastBatteryLogTime;

    // CPU Stress duration Index: 0 = 30s, 1 = 1m, 2 = 5m, 3 = 10m, 4 = Infinite
    int cpuStressDurationIndex = 1;
    int cpuStressSelectedThreads = 4;
    bool isCpuStressRunning = false;
    int cpuStressRemainingSeconds = 60;
    bool cpuStressCompleted = false;
    std::wstring cpuStressResultsStr = L"";
    std::vector<std::wstring> cpuStressLog;
    std::chrono::steady_clock::time_point lastCpuStressLogTime;

    // RAM Test settings
    int ramTestSelectedSizeGb = 2;
    std::vector<std::wstring> ramTestLog;

    // Timing helper
    std::chrono::steady_clock::time_point lastSecondTick;

    void SetStatus(const std::wstring& msg, bool isError);
    void UpdateClock();
    void TickOneSecond();
    void PushHistory(std::vector<float>& vec, float val, int maxSize = 60);

    // Tab drawing helpers
    void DrawDashboard(void* hwnd);
    void DrawTools(void* hwnd);
    void DrawRepair(void* hwnd);
    void DrawBattery(void* hwnd);
    void DrawCpuStress(void* hwnd);
    void DrawRamTest(void* hwnd);
    void DrawGuide();

    // Desktop Shell helpers
    void DrawDesktop(void* hwnd, float screenWidth, float screenHeight, bool& triggerClose, bool& triggerMinimize);
    void DrawTaskbar(void* hwnd, float screenWidth, float screenHeight, bool& triggerClose, bool& triggerMinimize);
    void DrawStartMenu(float screenWidth, float screenHeight, bool& triggerClose);
    void DrawDesktopIcon(const char* label, const char* symbolType, float x, float y, bool& showTarget, bool& minTarget);
    void RenderVectorIcon(const char* iconType, struct ImVec2 pos, float size);

public:
    GuiController();
    ~GuiController();

    void Initialize();
    void Update();
    void Draw(void* hwnd, bool& isMaximized, bool& triggerClose, bool& triggerMinimize, bool& triggerMaximize);

    void ApplyTheme();
    bool IsRecordingActive() const { return isRecording; }
    void RenderGui();
};
