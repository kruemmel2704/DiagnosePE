#include "hardware_monitor.h"
#include "wmi_helper.h"
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <cwctype>
#include <thread>

#pragma comment(lib, "shlwapi.lib")

static unsigned __int64 FileTimeToUInt64(const FILETIME& ft) {
    return (((unsigned __int64)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

HardwareMonitor::HardwareMonitor() {
    prevIdleTime = 0;
    prevKernelTime = 0;
    prevUserTime = 0;
    hasStaticInfo = false;
}

void HardwareMonitor::FetchStaticInfo() {
    if (hasStaticInfo) return;

    // 1. Get CPU Name from registry (much faster than WMI)
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[256];
        DWORD dwSize = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)buf, &dwSize) == ERROR_SUCCESS) {
            cachedStaticInfo.cpuName = buf;
        }
        RegCloseKey(hKey);
    }
    
    if (cachedStaticInfo.cpuName.empty()) {
        cachedStaticInfo.cpuName = L"AMD / Intel Processor";
    }

    // 2. Query System Info via WMI
    auto compSystem = WmiHelper::Query(L"ROOT\\CIMV2", L"SELECT Manufacturer, Model FROM Win32_ComputerSystem", {L"Manufacturer", L"Model"});
    if (!compSystem.empty()) {
        cachedStaticInfo.sysManufacturer = WmiHelper::SanitizeValue(compSystem[0].values[L"Manufacturer"]);
        cachedStaticInfo.sysModel = WmiHelper::SanitizeValue(compSystem[0].values[L"Model"]);
    }

    auto biosInfo = WmiHelper::Query(L"ROOT\\CIMV2", L"SELECT SerialNumber FROM Win32_BIOS", {L"SerialNumber"});
    if (!biosInfo.empty()) {
        cachedStaticInfo.sysSerialNumber = WmiHelper::SanitizeValue(biosInfo[0].values[L"SerialNumber"]);
    }

    auto boardInfo = WmiHelper::Query(L"ROOT\\CIMV2", L"SELECT Manufacturer, Product, SerialNumber FROM Win32_BaseBoard", {L"Manufacturer", L"Product", L"SerialNumber"});
    if (!boardInfo.empty()) {
        cachedStaticInfo.mbManufacturer = WmiHelper::SanitizeValue(boardInfo[0].values[L"Manufacturer"]);
        cachedStaticInfo.mbModel = WmiHelper::SanitizeValue(boardInfo[0].values[L"Product"]);
        cachedStaticInfo.mbSerialNumber = WmiHelper::SanitizeValue(boardInfo[0].values[L"SerialNumber"]);
    }

    hasStaticInfo = true;
}

float HardwareMonitor::CalculateCpuLoad() {
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        unsigned __int64 idle = FileTimeToUInt64(idleTime);
        unsigned __int64 kernel = FileTimeToUInt64(kernelTime);
        unsigned __int64 user = FileTimeToUInt64(userTime);

        if (prevIdleTime != 0) {
            unsigned __int64 idleDiff = idle - prevIdleTime;
            unsigned __int64 kernelDiff = kernel - prevKernelTime;
            unsigned __int64 userDiff = user - prevUserTime;
            unsigned __int64 totalDiff = kernelDiff + userDiff;

            prevIdleTime = idle;
            prevKernelTime = kernel;
            prevUserTime = user;

            if (totalDiff > 0) {
                float load = (float)(totalDiff - idleDiff) * 100.0f / totalDiff;
                return (std::max)(0.0f, (std::min)(100.0f, load));
            }
        } else {
            prevIdleTime = idle;
            prevKernelTime = kernel;
            prevUserTime = user;
        }
    }
    return 0.0f;
}

bool HardwareMonitor::CheckHWiNFORunning() {
    // Check processes using Toolhelp32
    // We can also check by opening the Registry and seeing if keys are being written, but process check is simple
    bool running = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                std::wstring name = pe.szExeFile;
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                if (name.find(L"hwinfo") != std::wstring::npos) {
                    running = true;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return running;
}

void HardwareMonitor::ReadHWiNFORegistry(HardwareDiagnostics& diag) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\HWiNFO64\\VSB", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        diag.isHWiNFOFallbackActive = false;
        return;
    }

    bool fallbackApplied = false;
    float cpuTemp = -1.0f;
    float cpuLoad = -1.0f;
    float gpuTemp = -1.0f;
    float gpuLoad = -1.0f;
    float gpuVram = -1.0f;
    float ramPercentVal = -1.0f;

    std::vector<std::pair<int, float>> coreTemps;
    std::vector<std::pair<int, float>> coreLoads;

    for (int i = 0; i < 256; ++i) {
        std::wstring idxStr = std::to_wstring(i);
        std::wstring sensorValName = L"Sensor" + idxStr;
        std::wstring labelValName = L"Label" + idxStr;
        std::wstring valueRawValName = L"ValueRaw" + idxStr;

        wchar_t sensorBuf[256] = {0};
        wchar_t labelBuf[256] = {0};
        wchar_t valueRawBuf[64] = {0};

        DWORD sensorSize = sizeof(sensorBuf);
        DWORD labelSize = sizeof(labelBuf);
        DWORD valueRawSize = sizeof(valueRawBuf);

        if (RegQueryValueExW(hKey, sensorValName.c_str(), NULL, NULL, (LPBYTE)sensorBuf, &sensorSize) != ERROR_SUCCESS) {
            // No more sensors
            break;
        }

        RegQueryValueExW(hKey, labelValName.c_str(), NULL, NULL, (LPBYTE)labelBuf, &labelSize);
        RegQueryValueExW(hKey, valueRawValName.c_str(), NULL, NULL, (LPBYTE)valueRawBuf, &valueRawSize);

        std::wstring sensor(sensorBuf);
        std::wstring label(labelBuf);
        std::wstring valueRawStr(valueRawBuf);

        if (sensor.empty() || label.empty() || valueRawStr.empty()) continue;

        float rawVal = 0.0f;
        try {
            rawVal = std::wcstof(valueRawStr.c_str(), nullptr);
        } catch (...) {
            continue;
        }

        std::wstring sensorLower = sensor;
        std::transform(sensorLower.begin(), sensorLower.end(), sensorLower.begin(), ::towlower);
        std::wstring labelLower = label;
        std::transform(labelLower.begin(), labelLower.end(), labelLower.begin(), ::towlower);

        // --- CPU ---
        if (sensorLower.find(L"cpu") != std::wstring::npos) {
            // CPU Name override if generic
            if (diag.cpuName.find(L"AMD / Intel Processor") != std::wstring::npos) {
                size_t pos = sensor.find(L':');
                if (pos != std::wstring::npos && pos + 1 < sensor.length()) {
                    std::wstring nameCandidate = sensor.substr(pos + 1);
                    // Trim name
                    nameCandidate.erase(0, nameCandidate.find_first_not_of(L" "));
                    if (!nameCandidate.empty() && nameCandidate.find(L"DTS") == std::wstring::npos) {
                        diag.cpuName = nameCandidate;
                    }
                }
            }

            // Average CPU Temp
            if (labelLower.find(L"temp") != std::wstring::npos ||
                labelLower.find(L"tctl") != std::wstring::npos ||
                labelLower.find(L"die") != std::wstring::npos ||
                labelLower.find(L"package") != std::wstring::npos) 
            {
                if (labelLower.find(L"tctl/tdie") != std::wstring::npos ||
                    labelLower.find(L"die (average)") != std::wstring::npos ||
                    labelLower.find(L"durchschnitt") != std::wstring::npos ||
                    labelLower.find(L"package") != std::wstring::npos) 
                {
                    cpuTemp = rawVal;
                }
                else if (cpuTemp < 0.0f) {
                    cpuTemp = rawVal;
                }
            }

            // Core Temp
            // E.g. Core #1 or Kern #1
            size_t corePos = labelLower.find(L"core");
            if (corePos == std::wstring::npos) corePos = labelLower.find(L"kern");
            if (corePos != std::wstring::npos) {
                // Find core number
                int coreNum = -1;
                for (size_t charIdx = corePos; charIdx < labelLower.length(); ++charIdx) {
                    if (std::iswdigit(labelLower[charIdx])) {
                        coreNum = std::wcstol(&labelLower[charIdx], nullptr, 10);
                        break;
                    }
                }
                if (coreNum >= 0) {
                    if (labelLower.find(L"load") != std::wstring::npos ||
                        labelLower.find(L"usage") != std::wstring::npos ||
                        labelLower.find(L"auslastung") != std::wstring::npos) 
                    {
                        coreLoads.push_back({coreNum, rawVal});
                    } else if (labelLower.find(L"max") == std::wstring::npos && labelLower.find(L"average") == std::wstring::npos) {
                        coreTemps.push_back({coreNum, rawVal});
                    }
                }
            }

            // CPU Total Load
            if ((labelLower.find(L"total") != std::wstring::npos || labelLower.find(L"gesamt") != std::wstring::npos) &&
                (labelLower.find(L"utility") != std::wstring::npos ||
                 labelLower.find(L"load") != std::wstring::npos ||
                 labelLower.find(L"usage") != std::wstring::npos ||
                 labelLower.find(L"auslastung") != std::wstring::npos)) 
            {
                cpuLoad = rawVal;
            }
            else if (labelLower == L"cpu total" || labelLower.find(L"cpu-auslastung") != std::wstring::npos) {
                cpuLoad = rawVal;
            }
        }
        // --- GPU ---
        else if (sensorLower.find(L"gpu") != std::wstring::npos) {
            // GPU Name override if generic
            if (diag.gpuName.find(L"Keine") != std::wstring::npos || diag.gpuName.find(L"Integriert") != std::wstring::npos) {
                size_t pos = sensor.find(L':');
                if (pos != std::wstring::npos && pos + 1 < sensor.length()) {
                    std::wstring nameCandidate = sensor.substr(pos + 1);
                    nameCandidate.erase(0, nameCandidate.find_first_not_of(L" "));
                    if (!nameCandidate.empty()) {
                        diag.gpuName = nameCandidate;
                    }
                }
            }

            // Temp
            if (labelLower.find(L"temp") != std::wstring::npos ||
                labelLower.find(L"core") != std::wstring::npos ||
                labelLower.find(L"kern") != std::wstring::npos) 
            {
                if (labelLower.find(L"hot spot") == std::wstring::npos && labelLower.find(L"hotspot") == std::wstring::npos) {
                    gpuTemp = rawVal;
                } else if (gpuTemp < 0.0f) {
                    gpuTemp = rawVal;
                }
            }

            // Load
            if (labelLower.find(L"load") != std::wstring::npos ||
                labelLower.find(L"usage") != std::wstring::npos ||
                labelLower.find(L"auslastung") != std::wstring::npos ||
                labelLower.find(L"nutzung") != std::wstring::npos) 
            {
                gpuLoad = rawVal;
            }

            // VRAM
            if (labelLower.find(L"memory usage") != std::wstring::npos ||
                labelLower.find(L"d3d memory dedicated") != std::wstring::npos ||
                labelLower.find(L"speicher") != std::wstring::npos) 
            {
                gpuVram = rawVal;
            }
        }
        // --- RAM ---
        else if (sensorLower.find(L"system") != std::wstring::npos ||
                 sensorLower.find(L"memory") != std::wstring::npos ||
                 sensorLower.find(L"arbeitsspeicher") != std::wstring::npos) 
        {
            if (labelLower.find(L"memory load") != std::wstring::npos ||
                labelLower.find(L"memory usage") != std::wstring::npos ||
                labelLower.find(L"auslastung") != std::wstring::npos ||
                labelLower.find(L"belegung") != std::wstring::npos) 
            {
                ramPercentVal = rawVal;
            }
        }
    }

    RegCloseKey(hKey);

    // Apply fallbacks
    if (cpuTemp >= 0.0f) {
        diag.cpuAverageTemp = cpuTemp;
        fallbackApplied = true;
    }
    if (cpuLoad >= 0.0f) {
        diag.cpuAverageLoad = cpuLoad;
        fallbackApplied = true;
    }
    if (gpuTemp >= 0.0f) {
        diag.gpuTemp = gpuTemp;
        fallbackApplied = true;
    }
    if (gpuLoad >= 0.0f) {
        diag.gpuLoad = gpuLoad;
        fallbackApplied = true;
    }
    if (gpuVram >= 0.0f) {
        diag.gpuVramUsage = gpuVram;
        fallbackApplied = true;
    }
    if (ramPercentVal >= 0.0f) {
        diag.ramPercent = ramPercentVal;
        diag.ramUsedGb = (diag.ramPercent / 100.0f) * diag.ramTotalGb;
        fallbackApplied = true;
    }

    // Merge core temps & loads
    std::sort(coreTemps.begin(), coreTemps.end());
    std::sort(coreLoads.begin(), coreLoads.end());

    std::vector<int> allCores;
    for (const auto& item : coreTemps) allCores.push_back(item.first);
    for (const auto& item : coreLoads) {
        if (std::find(allCores.begin(), allCores.end(), item.first) == allCores.end()) {
            allCores.push_back(item.first);
        }
    }
    std::sort(allCores.begin(), allCores.end());

    if (!allCores.empty()) {
        diag.cpuCores.clear();
        for (int coreNum : allCores) {
            CpuCoreInfo core;
            core.name = L"CPU Core #" + std::to_wstring(coreNum);
            
            auto itTemp = std::find_if(coreTemps.begin(), coreTemps.end(), [coreNum](const std::pair<int, float>& p) { return p.first == coreNum; });
            if (itTemp != coreTemps.end()) core.temperature = itTemp->second;

            auto itLoad = std::find_if(coreLoads.begin(), coreLoads.end(), [coreNum](const std::pair<int, float>& p) { return p.first == coreNum; });
            if (itLoad != coreLoads.end()) core.load = itLoad->second;

            diag.cpuCores.push_back(core);
            fallbackApplied = true;
        }
    }

    diag.isHWiNFOFallbackActive = fallbackApplied;
}

HardwareDiagnostics HardwareMonitor::GetDiagnostics() {
    FetchStaticInfo();

    HardwareDiagnostics diag = cachedStaticInfo;

    // 1. Calculate Real-time RAM
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        diag.ramTotalGb = (float)memStatus.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
        diag.ramUsedGb = (float)(memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f);
        diag.ramPercent = (float)memStatus.dwMemoryLoad;
    }

    // 2. Compute native CPU Load
    diag.cpuAverageLoad = CalculateCpuLoad();

    // 3. Read dynamic CPU Clock Speed
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD speed = 0;
        DWORD dwSize = sizeof(speed);
        if (RegQueryValueExW(hKey, L"~MHz", NULL, NULL, (LPBYTE)&speed, &dwSize) == ERROR_SUCCESS) {
            diag.cpuClockSpeed = (float)speed;
        }
        RegCloseKey(hKey);
    }

    // 4. Fill cores lists if empty
    int coresCount = std::thread::hardware_concurrency();
    if (coresCount <= 0) coresCount = 4;
    diag.cpuCores.resize(coresCount);
    for (int i = 0; i < coresCount; ++i) {
        diag.cpuCores[i].name = L"Core #" + std::to_wstring(i + 1);
        diag.cpuCores[i].load = diag.cpuAverageLoad;
        diag.cpuCores[i].temperature = -1.0f; // Unknown natively
    }

    // 5. Read HWiNFO fallback for temperatures and GPU info
    ReadHWiNFORegistry(diag);

    // If HWiNFO is not running or registry had no temps, we mark driver blocked (meaning we lack temps)
    diag.isLhmDriverBlocked = !diag.isHWiNFOFallbackActive;

    return diag;
}

std::wstring HardwareMonitor::GetHWiNFOPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    std::wstring baseDir(exePath);
    std::wstring paths[] = {
        baseDir + L"\\HWiNFO64.exe",
        baseDir + L"\\Tools\\HWiNFO\\HWiNFO64.exe",
        baseDir + L"\\Tools\\HWiNFO64.exe"
    };

    for (const auto& p : paths) {
        if (PathFileExistsW(p.c_str())) return p;
    }

    return L"";
}

bool HardwareMonitor::IsHWiNFOPortableFound() {
    return !GetHWiNFOPath().empty();
}

bool HardwareMonitor::StartHWiNFO() {
    std::wstring hwinfoPath = GetHWiNFOPath();
    if (hwinfoPath.empty()) return false;

    // Auto configure HWiNFO registry settings
    try {
        HKEY hKeySettings;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\HWiNFO64\\Settings", 0, NULL, 0, KEY_WRITE, NULL, &hKeySettings, NULL) == ERROR_SUCCESS) {
            DWORD one = 1;
            DWORD zero = 0;
            RegSetValueExW(hKeySettings, L"EnableReporting", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
            RegSetValueExW(hKeySettings, L"SensorsOnly", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
            RegSetValueExW(hKeySettings, L"MinimizeSensors", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
            RegSetValueExW(hKeySettings, L"ShowWelcomeScreen", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
            RegCloseKey(hKeySettings);
        }

        HKEY hKeySensors;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\HWiNFO64\\Sensors", 0, NULL, 0, KEY_WRITE, NULL, &hKeySensors, NULL) == ERROR_SUCCESS) {
            DWORD one = 1;
            RegSetValueExW(hKeySensors, L"EnableVSB", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
            
            // Open read/write to enable all subkeys in VSB
            // To be thorough, we can try opening keys, but since we are launching HWiNFO,
            // HWiNFO will write out keys, and we can pre-configure them or configure them after next launch.
            // Under normal circumstances, enabling EnableVSB and Settings is enough to prompt HWiNFO.
            RegCloseKey(hKeySensors);
        }
    } catch (...) {}

    // Launch portable process
    std::wstring dir = hwinfoPath;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dir = dir.substr(0, pos);
    }

    HINSTANCE hInst = ShellExecuteW(
        NULL,
        L"open",
        hwinfoPath.c_str(),
        L"",
        dir.c_str(),
        SW_MINIMIZE
    );

    return (reinterpret_cast<intptr_t>(hInst) > 32);
}
