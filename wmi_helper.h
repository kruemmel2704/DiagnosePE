#pragma once
#include <string>
#include <vector>
#include <map>

class WmiHelper {
public:
    struct WmiRow {
        std::map<std::wstring, std::wstring> values;
    };

    static bool Initialize();
    static void Uninitialize();

    static std::vector<WmiRow> Query(
        const std::wstring& namespacePath,
        const std::wstring& query,
        const std::vector<std::wstring>& properties
    );

    static std::wstring SanitizeValue(const std::wstring& val);
};
