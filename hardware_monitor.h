#pragma once
#include <string>
#include <vector>

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

    std::wstring gpuName = L"Keine dedizierte Grafikkarte / Integriert";
    float gpuLoad = 0.0f;
    float gpuTemp = 0.0f;
    float gpuVramUsage = 0.0f; // VRAM load percent (or memory usage ratio)

    float ramUsedGb = 0.0f;
    float ramTotalGb = 0.0f;
    float ramPercent = 0.0f;

    std::wstring sysManufacturer = L"Unbekannt";
    std::wstring sysModel = L"Unbekannt";
    std::wstring sysSerialNumber = L"Unbekannt";
    std::wstring mbManufacturer = L"Unbekannt";
    std::wstring mbModel = L"Unbekannt";
    std::wstring mbSerialNumber = L"Unbekannt";

    bool isLhmDriverBlocked = false; // True if standard temp sensors are not accessible
    bool isHWiNFOFallbackActive = false; // True if we successfully got values from HWiNFO
};

class HardwareMonitor {
private:
    unsigned __int64 prevIdleTime = 0;
    unsigned __int64 prevKernelTime = 0;
    unsigned __int64 prevUserTime = 0;
    
    HardwareDiagnostics cachedStaticInfo;
    bool hasStaticInfo = false;

    void FetchStaticInfo();
    float CalculateCpuLoad();
    void ReadHWiNFORegistry(HardwareDiagnostics& diag);
    bool CheckHWiNFORunning();

public:
    HardwareMonitor();
    HardwareDiagnostics GetDiagnostics();
    bool StartHWiNFO();
    bool IsHWiNFOPortableFound();
    std::wstring GetHWiNFOPath();
};
