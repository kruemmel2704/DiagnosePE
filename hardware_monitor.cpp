#include "hardware_monitor.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unistd.h>

namespace fs = std::filesystem;

static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    std::wstring wstr(str.begin(), str.end());
    return wstr;
}

static std::string ReadFileFirstLine(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string line;
    std::getline(file, line);
    line.erase(line.find_last_not_of(" \n\r\t") + 1);
    return line;
}

static std::string GetCommandOutput(const std::string& cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    // Trim trailing whitespace/newlines
    if (!result.empty()) {
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
    }
    return result;
}

static std::wstring DetectGpuName() {
    // 1. Try lspci
    std::string lspciOut = GetCommandOutput("lspci | grep -E -i \"vga|3d|display\"");
    if (!lspciOut.empty()) {
        std::stringstream ss(lspciOut);
        std::string line;
        std::vector<std::wstring> gpus;
        while (std::getline(ss, line)) {
            size_t colon = line.find(':');
            if (colon != std::string::npos && colon + 1 < line.length()) {
                size_t secondColon = line.find(':', colon + 1);
                std::string gpuStr;
                if (secondColon != std::string::npos && secondColon + 1 < line.length()) {
                    gpuStr = line.substr(secondColon + 1);
                } else {
                    gpuStr = line.substr(colon + 1);
                }
                
                // Trim leading/trailing whitespace
                gpuStr.erase(0, gpuStr.find_first_not_of(" \t"));
                gpuStr.erase(gpuStr.find_last_not_of(" \t\r\n") + 1);
                
                if (!gpuStr.empty()) {
                    gpus.push_back(StringToWString(gpuStr));
                }
            }
        }
        if (!gpus.empty()) {
            std::wstring combined = gpus[0];
            for (size_t i = 1; i < gpus.size(); ++i) {
                combined += L" / " + gpus[i];
            }
            return combined;
        }
    }

    // 2. Fallback: Scan PCI devices in sysfs directly
    try {
        if (fs::exists("/sys/bus/pci/devices")) {
            std::vector<std::wstring> sysGpus;
            for (const auto& entry : fs::directory_iterator("/sys/bus/pci/devices")) {
                std::string classPath = entry.path().string() + "/class";
                std::ifstream classFile(classPath);
                if (classFile.is_open()) {
                    std::string classCode;
                    classFile >> classCode;
                    if (classCode.rfind("0x03", 0) == 0 || classCode.rfind("03", 0) == 0) {
                        std::ifstream vendorFile(entry.path().string() + "/vendor");
                        std::ifstream deviceFile(entry.path().string() + "/device");
                        std::string vendor, device;
                        if (vendorFile >> vendor && deviceFile >> device) {
                            vendor.erase(0, vendor.find_first_not_of(" \t"));
                            vendor.erase(vendor.find_last_not_of(" \t\r\n") + 1);
                            device.erase(0, device.find_first_not_of(" \t"));
                            device.erase(device.find_last_not_of(" \t\r\n") + 1);
                            
                            std::wstring gpuName = L"PCI Graphics Adapter";
                            if (vendor.find("1002") != std::string::npos) {
                                gpuName = L"AMD Radeon Graphics";
                            } else if (vendor.find("10de") != std::string::npos) {
                                gpuName = L"NVIDIA GeForce Graphics";
                            } else if (vendor.find("8086") != std::string::npos) {
                                gpuName = L"Intel HD / Iris / Arc Graphics";
                            } else if (vendor.find("80ee") != std::string::npos) {
                                gpuName = L"VirtualBox Graphics Adapter";
                            } else if (vendor.find("15ad") != std::string::npos) {
                                gpuName = L"VMware SVGA II Adapter";
                            } else if (vendor.find("1414") != std::string::npos) {
                                gpuName = L"Hyper-V Video Adapter";
                            } else if (vendor.find("1b36") != std::string::npos) {
                                gpuName = L"QEMU QXL Video Adapter";
                            } else {
                                gpuName += L" (" + StringToWString(vendor) + L":" + StringToWString(device) + L")";
                            }
                            sysGpus.push_back(gpuName);
                        }
                    }
                }
            }
            if (!sysGpus.empty()) {
                std::wstring combined = sysGpus[0];
                for (size_t i = 1; i < sysGpus.size(); ++i) {
                    combined += L" / " + sysGpus[i];
                }
                return combined;
            }
        }
    } catch (...) {}

    // 3. Fallback: Check /sys/class/drm/card*/device/vendor
    try {
        for (int i = 0; i < 4; ++i) {
            std::string cardPath = "/sys/class/drm/card" + std::to_string(i) + "/device";
            if (fs::exists(cardPath)) {
                std::ifstream vendorFile(cardPath + "/vendor");
                std::ifstream deviceFile(cardPath + "/device");
                std::string vendor, device;
                if (vendorFile >> vendor && deviceFile >> device) {
                    std::wstring gpuName = L"PCI Graphics Adapter";
                    if (vendor.find("1002") != std::string::npos) {
                        return L"AMD Radeon Graphics";
                    } else if (vendor.find("10de") != std::string::npos) {
                        return L"NVIDIA GeForce Graphics";
                    } else if (vendor.find("8086") != std::string::npos) {
                        return L"Intel HD / Iris / Arc Graphics";
                    } else if (vendor.find("80ee") != std::string::npos) {
                        return L"VirtualBox Graphics Adapter";
                    } else if (vendor.find("15ad") != std::string::npos) {
                        return L"VMware SVGA II Adapter";
                    } else if (vendor.find("1414") != std::string::npos) {
                        return L"Hyper-V Video Adapter";
                    } else if (vendor.find("1b36") != std::string::npos) {
                        return L"QEMU QXL Video Adapter";
                    } else {
                        return gpuName + L" (" + StringToWString(vendor) + L":" + StringToWString(device) + L")";
                    }
                }
            }
        }
    } catch (...) {}

    return L"Integrated / Discrete GPU";
}

HardwareMonitor::HardwareMonitor() {
    FetchStaticInfo();
}

void HardwareMonitor::FetchStaticInfo() {
    cachedStaticInfo.sysManufacturer = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/sys_vendor"));
    cachedStaticInfo.sysModel = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/product_name"));
    cachedStaticInfo.sysSerialNumber = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/product_serial"));
    cachedStaticInfo.mbManufacturer = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/board_vendor"));
    cachedStaticInfo.mbModel = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/board_name"));
    cachedStaticInfo.mbSerialNumber = StringToWString(ReadFileFirstLine("/sys/class/dmi/id/board_serial"));

    if (cachedStaticInfo.sysManufacturer.empty()) cachedStaticInfo.sysManufacturer = L"Generic PC System";
    if (cachedStaticInfo.sysModel.empty()) cachedStaticInfo.sysModel = L"Linux Host System";
    if (cachedStaticInfo.mbManufacturer.empty()) cachedStaticInfo.mbManufacturer = L"Standard Motherboard";

    // CPU Name from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos || line.find("Hardware") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos && colon + 2 < line.length()) {
                    cachedStaticInfo.cpuName = StringToWString(line.substr(colon + 2));
                    break;
                }
            }
        }
    }

    // GPU Name detection
    cachedStaticInfo.gpuName = DetectGpuName();

    hasStaticInfo = true;
}

float HardwareMonitor::CalculateCpuLoad() {
    std::ifstream statFile("/proc/stat");
    if (!statFile.is_open()) return 0.0f;

    std::string cpuLabel;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    statFile >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    unsigned long long totalIdle = idle + iowait;
    unsigned long long totalNonIdle = user + nice + system + irq + softirq + steal;
    unsigned long long total = totalIdle + totalNonIdle;

    static unsigned long long prevTotal = 0;
    static unsigned long long prevIdle = 0;

    unsigned long long totalDiff = total - prevTotal;
    unsigned long long idleDiff = totalIdle - prevIdle;

    prevTotal = total;
    prevIdle = totalIdle;

    if (totalDiff == 0) return 0.0f;

    float cpuPercentage = (float)(totalDiff - idleDiff) / (float)totalDiff * 100.0f;
    return (cpuPercentage >= 0.0f && cpuPercentage <= 100.0f) ? cpuPercentage : 0.0f;
}

HardwareDiagnostics HardwareMonitor::GetDiagnostics() {
    HardwareDiagnostics diag = cachedStaticInfo;

    // CPU Load & Clock Speed
    diag.cpuAverageLoad = CalculateCpuLoad();
    std::string freqStr = ReadFileFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (!freqStr.empty()) {
        try {
            diag.cpuClockSpeed = std::stof(freqStr) / 1000.0f;
        } catch (...) {
            diag.cpuClockSpeed = 0.0f;
        }
    }

    // Temperatures
    float sumCpuTemp = 0.0f;
    int tempCount = 0;
    diag.cpuCores.clear();

    try {
        if (fs::exists("/sys/class/hwmon")) {
            for (const auto& entry : fs::directory_iterator("/sys/class/hwmon")) {
                std::string hwmonName = ReadFileFirstLine(entry.path().string() + "/name");
                if (hwmonName == "coretemp" || hwmonName == "k10temp" || hwmonName == "zenpower" || hwmonName == "acpitz" || hwmonName == "cpu_thermal") {
                    for (const auto& sensorFile : fs::directory_iterator(entry.path())) {
                        std::string fileName = sensorFile.path().filename().string();
                        if (fileName.rfind("temp", 0) == 0 && fileName.find("_input") != std::string::npos) {
                            std::string valStr = ReadFileFirstLine(sensorFile.path().string());
                            if (!valStr.empty()) {
                                float tempC = std::stof(valStr) / 1000.0f;
                                if (tempC > 0.0f && tempC < 150.0f) {
                                    sumCpuTemp += tempC;
                                    tempCount++;
                                    CpuCoreInfo core;
                                    core.name = L"Core " + std::to_wstring(tempCount);
                                    core.temperature = tempC;
                                    core.load = diag.cpuAverageLoad;
                                    diag.cpuCores.push_back(core);
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {}

    if (tempCount > 0) {
        diag.cpuAverageTemp = sumCpuTemp / tempCount;
    } else {
        diag.cpuAverageTemp = 42.0f;
    }

    // Memory
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        unsigned long long memTotalKb = 0;
        unsigned long long memAvailableKb = 0;
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::stringstream ss(line.substr(9));
                ss >> memTotalKb;
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::stringstream ss(line.substr(13));
                ss >> memAvailableKb;
            }
        }
        if (memTotalKb > 0) {
            diag.ramTotalGb = (float)memTotalKb / (1024.0f * 1024.0f);
            float ramFreeGb = (float)memAvailableKb / (1024.0f * 1024.0f);
            diag.ramUsedGb = diag.ramTotalGb - ramFreeGb;
            diag.ramPercent = (diag.ramUsedGb / diag.ramTotalGb) * 100.0f;
        }
    }

    // GPU
    try {
        if (fs::exists("/sys/class/drm/card0/device/gpu_busy_percent")) {
            std::string busyStr = ReadFileFirstLine("/sys/class/drm/card0/device/gpu_busy_percent");
            if (!busyStr.empty()) diag.gpuLoad = std::stof(busyStr);
        }
    } catch (...) {}

    return diag;
}
