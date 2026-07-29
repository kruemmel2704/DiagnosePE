#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct CpuCoreInfo {
    std::wstring name;
    float temperature = -1.0f; // -1 if unknown
    float load = -1.0f;        // -1 if unknown
};

struct HardwareDiagnostics {
    std::wstring cpuName = L"AMD / Intel Processor";
    float cpuAverageLoad = 0.0f;
    float cpuAverageTemp = 0.0f;
    float cpuClockSpeed = 0.0f; // in MHz
    std::vector<CpuCoreInfo> cpuCores;

    std::wstring gpuName = L"Integrated / Discrete GPU";
    float gpuLoad = 0.0f;
    float gpuTemp = 0.0f;
    float gpuVramUsage = 0.0f;

    float ramUsedGb = 0.0f;
    float ramTotalGb = 0.0f;
    float ramPercent = 0.0f;

    std::wstring sysManufacturer = L"Generic PC System";
    std::wstring sysModel = L"Linux Host System";
    std::wstring sysSerialNumber = L"Unknown";
    std::wstring mbManufacturer = L"Motherboard";
    std::wstring mbModel = L"System Board";
    std::wstring mbSerialNumber = L"Unknown";

    bool isLhmDriverBlocked = false;
    bool isHWiNFOFallbackActive = false;
};

class HardwareMonitor {
private:
    uint64_t prevIdleTime = 0;
    uint64_t prevKernelTime = 0;
    uint64_t prevUserTime = 0;
    
    HardwareDiagnostics cachedStaticInfo;
    bool hasStaticInfo = false;

    void FetchStaticInfo();
    float CalculateCpuLoad();

public:
    HardwareMonitor();
    HardwareDiagnostics GetDiagnostics();
};
