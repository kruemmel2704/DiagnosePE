#pragma once
#include <string>
#include <vector>
#include <map>

struct TestTool {
    std::wstring key;
    std::wstring name;
    std::wstring exeName;
    std::wstring alternateExeName;
    std::wstring description;
    std::wstring path; // Found path, empty if not found

    bool IsFound() const;
    std::wstring GetDisplayStatus() const;
};

class ToolLauncher {
private:
    std::vector<TestTool> tools;
    std::map<std::wstring, std::wstring> manualPaths;

    std::wstring GetConfigFilePath();
    void LoadManualPaths();
    void SaveManualPaths();
    std::wstring ScanUsbForTool(const std::wstring& primaryExe, const std::wstring& altExe);
    std::wstring CheckFileExistsInDir(const std::wstring& dir, const std::wstring& primary, const std::wstring& alt);

public:
    ToolLauncher();
    
    void InitializeTools();
    void RefreshPaths();
    void SaveManualPath(const std::wstring& key, const std::wstring& path);
    void ClearManualPath(const std::wstring& key);
    bool HasManualPath(const std::wstring& key) const;
    bool LaunchTool(const std::wstring& key, std::wstring& errorMessage);

    std::vector<TestTool>& GetTools();
};
