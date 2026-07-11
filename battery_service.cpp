#include "battery_service.h"
#include "wmi_helper.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
    swprintf_s(buf, L"%02d Std. %02d Min.", hours, minutes);
    return buf;
}

std::wstring BatteryInfo::GetHealthDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf_s(buf, L"%.1f %%", healthPercent);
    return buf;
}

std::wstring BatteryInfo::GetDesignCapacityDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf_s(buf, L"%u mWh", designCapacity);
    return buf;
}

std::wstring BatteryInfo::GetFullChargeCapacityDisplay() const {
    if (!isPresent) return L"--";
    wchar_t buf[64];
    swprintf_s(buf, L"%u mWh", fullChargeCapacity);
    return buf;
}

void BatteryService::QueryFallbackCimv2(BatteryInfo& info) {
    auto list = WmiHelper::Query(
        L"ROOT\\CIMV2",
        L"SELECT EstimatedChargeRemaining, DesignCapacity, FullChargeCapacity, DeviceID FROM Win32_Battery",
        {L"EstimatedChargeRemaining", L"DesignCapacity", L"FullChargeCapacity", L"DeviceID"}
    );

    if (list.empty()) return;

    info.isPresent = true;
    const auto& row = list[0];

    try {
        if (row.values.count(L"DesignCapacity") && row.values.at(L"DesignCapacity") != L"Unbekannt") {
            info.designCapacity = std::wcstoul(row.values.at(L"DesignCapacity").c_str(), nullptr, 10);
        }
        if (row.values.count(L"FullChargeCapacity") && row.values.at(L"FullChargeCapacity") != L"Unbekannt") {
            info.fullChargeCapacity = std::wcstoul(row.values.at(L"FullChargeCapacity").c_str(), nullptr, 10);
        }
        if (row.values.count(L"DeviceID") && row.values.at(L"DeviceID") != L"Unbekannt") {
            info.deviceName = row.values.at(L"DeviceID");
        }
        if (info.currentChargePercent == 0.0 && row.values.count(L"EstimatedChargeRemaining") && row.values.at(L"EstimatedChargeRemaining") != L"Unbekannt") {
            info.currentChargePercent = std::wcstod(row.values.at(L"EstimatedChargeRemaining").c_str(), nullptr);
        }

        if (info.designCapacity > 0) {
            info.healthPercent = ((double)info.fullChargeCapacity / info.designCapacity) * 100.0;
            if (info.healthPercent > 100.0) info.healthPercent = 100.0;
        }
    } catch (...) {}
}

BatteryInfo BatteryService::GetBatteryInfo() {
    BatteryInfo info;

    // 1. Get real-time status using Win32 API
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.BatteryLifePercent != 255) {
            info.currentChargePercent = status.BatteryLifePercent;
            info.isPresent = (status.BatteryFlag & 128) == 0;
        } else {
            info.isPresent = false;
        }
        
        info.isConnectedToAc = (status.ACLineStatus == 1);
        info.isCharging = (status.BatteryFlag & 8) != 0;
        info.remainingTimeSeconds = status.BatteryLifeTime;
    } else {
        info.isPresent = false;
    }

    if (!info.isPresent) {
        return info;
    }

    // 2. Query static information using WMI root\wmi
    auto staticData = WmiHelper::Query(
        L"ROOT\\WMI",
        L"SELECT DesignedCapacity, ManufacturerName, DeviceName, Chemistry FROM BatteryStaticData",
        {L"DesignedCapacity", L"ManufacturerName", L"DeviceName", L"Chemistry"}
    );

    bool wmiSuccess = false;
    if (!staticData.empty()) {
        const auto& row = staticData[0];
        try {
            if (row.values.count(L"DesignedCapacity") && row.values.at(L"DesignedCapacity") != L"Unbekannt") {
                info.designCapacity = std::wcstoul(row.values.at(L"DesignedCapacity").c_str(), nullptr, 10);
            }
            if (row.values.count(L"ManufacturerName")) {
                info.manufacturer = row.values.at(L"ManufacturerName");
            }
            if (row.values.count(L"DeviceName")) {
                info.deviceName = row.values.at(L"DeviceName");
            }
            if (row.values.count(L"Chemistry")) {
                info.chemistry = row.values.at(L"Chemistry");
            }
            wmiSuccess = true;
        } catch (...) {}
    }

    auto fullCapacityData = WmiHelper::Query(
        L"ROOT\\WMI",
        L"SELECT FullChargedCapacity FROM BatteryFullChargedCapacity",
        {L"FullChargedCapacity"}
    );

    if (!fullCapacityData.empty()) {
        const auto& row = fullCapacityData[0];
        try {
            if (row.values.count(L"FullChargedCapacity") && row.values.at(L"FullChargedCapacity") != L"Unbekannt") {
                info.fullChargeCapacity = std::wcstoul(row.values.at(L"FullChargedCapacity").c_str(), nullptr, 10);
            }
        } catch (...) {}
    }

    if (wmiSuccess && info.designCapacity > 0) {
        info.healthPercent = ((double)info.fullChargeCapacity / info.designCapacity) * 100.0;
        if (info.healthPercent > 100.0) info.healthPercent = 100.0;
    } else {
        // Try fallback
        QueryFallbackCimv2(info);
    }

    return info;
}
