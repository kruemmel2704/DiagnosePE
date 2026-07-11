#pragma once
#include <string>

class SystemRepair {
public:
    static bool RunRepair(const std::wstring& key, std::wstring& outDescription);
};
