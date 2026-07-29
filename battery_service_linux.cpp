#include "battery_service.h"
#include "platform_compat.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <cwchar>

namespace fs = std::filesystem;

static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    return std::wstring(str.begin(), str.end());
}

static std::string ReadFileFirstLine(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string line;
    std::getline(file, line);
    line.erase(line.find_last_not_of(" \n\r\t") + 1);
    return line;
}

std::wstring BatteryInfo::GetChargingStatusDisplay() const {
    if (!isPresent) return L"Keine Batterie erkannt";
    if (isConnectedToAc) {
        return isCharging ? L"Wird geladen" : L"Netzbetrieb (Voll / Nicht ladend)";
    }
    return L"Entlädt sich";
}

std::wstring BatteryInfo::GetRemainingTimeDisplay() const {
    if (!isPresent) return L"--";
    if (isConnectedToAc) return L"-- (Netzbetrieb)";
    if (remainingTimeSeconds < 0) return L"Berechne...";
    
    int hours = remainingTimeSeconds / 3600;
    int minutes = (remainingTimeSeconds % 3600) / 60;
    
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d Std. %02d Min.", hours, minutes);
    return buf;
}

std::wstring BatteryInfo::GetHealthDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf(buf, 64, L"%.1f %%", healthPercent);
    return buf;
}

std::wstring BatteryInfo::GetDesignCapacityDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf(buf, 64, L"%u mWh", designCapacity);
    return buf;
}

std::wstring BatteryInfo::GetFullChargeCapacityDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf(buf, 64, L"%u mWh", fullChargeCapacity);
    return buf;
}

void BatteryService::QueryFallbackCimv2(BatteryInfo& info) {
    // Legacy Win32 WMI fallback - not used on Linux
}

BatteryInfo BatteryService::GetBatteryInfo() {
    BatteryInfo info;
    info.isPresent = false;

    // Check AC status
    try {
        if (fs::exists("/sys/class/power_supply")) {
            for (const auto& entry : fs::directory_iterator("/sys/class/power_supply")) {
                std::string name = entry.path().filename().string();
                if (name.find("AC") != std::string::npos || name.find("ADP") != std::string::npos) {
                    std::string onlineStr = ReadFileFirstLine(entry.path().string() + "/online");
                    if (onlineStr == "1") {
                        info.isConnectedToAc = true;
                    }
                }
            }

            // Check Battery status
            for (const auto& entry : fs::directory_iterator("/sys/class/power_supply")) {
                std::string name = entry.path().filename().string();
                if (name.rfind("BAT", 0) == 0) {
                    info.isPresent = true;
                    std::string batPath = entry.path().string();

                    info.manufacturer = StringToWString(ReadFileFirstLine(batPath + "/manufacturer"));
                    info.deviceName = StringToWString(ReadFileFirstLine(batPath + "/model_name"));
                    info.chemistry = StringToWString(ReadFileFirstLine(batPath + "/technology"));

                    std::string statusStr = ReadFileFirstLine(batPath + "/status");
                    info.isCharging = (statusStr == "Charging");

                    std::string capStr = ReadFileFirstLine(batPath + "/capacity");
                    if (!capStr.empty()) {
                        info.currentChargePercent = std::stod(capStr);
                    }

                    std::string designStr = ReadFileFirstLine(batPath + "/energy_full_design");
                    if (designStr.empty()) designStr = ReadFileFirstLine(batPath + "/charge_full_design");
                    if (!designStr.empty()) {
                        info.designCapacity = std::stoul(designStr) / 1000; // uWh to mWh
                    }

                    std::string fullStr = ReadFileFirstLine(batPath + "/energy_full");
                    if (fullStr.empty()) fullStr = ReadFileFirstLine(batPath + "/charge_full");
                    if (!fullStr.empty()) {
                        info.fullChargeCapacity = std::stoul(fullStr) / 1000; // uWh to mWh
                    }

                    if (info.designCapacity > 0 && info.fullChargeCapacity > 0) {
                        info.healthPercent = ((double)info.fullChargeCapacity / (double)info.designCapacity) * 100.0;
                        if (info.healthPercent > 100.0) info.healthPercent = 100.0;
                    }

                    break;
                }
            }
        }
    } catch (...) {}

    return info;
}
