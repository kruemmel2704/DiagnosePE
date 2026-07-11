#pragma once
#include <string>

struct BatteryInfo {
    bool isPresent = false;
    std::wstring manufacturer = L"Unknown";
    std::wstring deviceName = L"Unknown";
    std::wstring chemistry = L"Unknown";
    unsigned int designCapacity = 0;     // in mWh
    unsigned int fullChargeCapacity = 0; // in mWh
    double healthPercent = 100.0;

    double currentChargePercent = 0.0;   // 0 - 100
    bool isConnectedToAc = false;
    bool isCharging = false;
    int remainingTimeSeconds = -1;       // -1 if unknown or charging

    std::wstring GetChargingStatusDisplay() const;
    std::wstring GetRemainingTimeDisplay() const;
    std::wstring GetHealthDisplay() const;
    std::wstring GetDesignCapacityDisplay() const;
    std::wstring GetFullChargeCapacityDisplay() const;
};

class BatteryService {
private:
    static void QueryFallbackCimv2(BatteryInfo& info);
public:
    static BatteryInfo GetBatteryInfo();
};
