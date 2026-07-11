#include "tool_launcher.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

bool TestTool::IsFound() const {
    return !path.empty() && PathFileExistsW(path.c_str());
}

std::wstring TestTool::GetDisplayStatus() const {
    return IsFound() ? L"Bereit (Gefunden)" : L"Nicht gefunden (USB einstecken oder Pfad ausw\u00E4hlen)";
}

ToolLauncher::ToolLauncher() {
    InitializeTools();
    LoadManualPaths();
    RefreshPaths();
}

void ToolLauncher::InitializeTools() {
    tools.clear();
    
    TestTool occt;
    occt.key = L"OCCT";
    occt.name = L"OCCT";
    occt.exeName = L"OCCT.exe";
    occt.alternateExeName = L"OCCTPersonal.exe";
    occt.description = L"Hervorragendes Tool zur CPU-, GPU- und Netzteil-Stabilit\u00E4tsprüfung und Fehlererkennung.";
    tools.push_back(occt);

    TestTool furmark;
    furmark.key = L"FurMark";
    furmark.name = L"FurMark";
    furmark.exeName = L"FurMark.exe";
    furmark.alternateExeName = L"FurMark2.exe";
    furmark.description = L"Intensives GPU-Belastungstest-Tool (auch bekannt als \"Donut-Generator\"), um die Grafikstabilit\u00E4t zu verifizieren.";
    tools.push_back(furmark);

    TestTool prime;
    prime.key = L"Prime95";
    prime.name = L"Prime95";
    prime.exeName = L"prime95.exe";
    prime.alternateExeName = L"prime95w.exe";
    prime.description = L"Legend\u00E4rer CPU-Stresstest, der Primzahlenberechnungen nutzt, um Hardwarefehler aufzudecken.";
    tools.push_back(prime);

    TestTool victoria;
    victoria.key = L"Victoria";
    victoria.name = L"Victoria SSD/HDD Utility";
    victoria.exeName = L"Victoria.exe";
    victoria.alternateExeName = L"Victoria64.exe";
    victoria.description = L"Erweitertes Laufwerk-Diagnoseprogramm. Scant die Speicheroberfl\u00E4che, misst Zugriffszeiten, liest SMART-Daten und remapt defekte Sektoren.";
    tools.push_back(victoria);
}

std::wstring ToolLauncher::GetConfigFilePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    
    std::wstring localPath = std::wstring(exePath) + L"\\tool_paths.txt";
    
    // Check if we can write to local path
    std::ofstream testFile(localPath, std::ios::app);
    if (testFile.is_open()) {
        testFile.close();
        return localPath;
    }

    // Fallback to AppData
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring appDataFolder = std::wstring(appData) + L"\\DiagnoseTool";
        CreateDirectoryW(appDataFolder.c_str(), NULL);
        return appDataFolder + L"\\tool_paths.txt";
    }

    return L"tool_paths.txt";
}

void ToolLauncher::LoadManualPaths() {
    manualPaths.clear();
    std::wstring configPath = GetConfigFilePath();
    std::wifstream file(configPath);
    if (!file.is_open()) return;

    std::wstring line;
    while (std::getline(file, line)) {
        size_t eqPos = line.find(L'=');
        if (eqPos != std::wstring::npos) {
            std::wstring key = line.substr(0, eqPos);
            std::wstring val = line.substr(eqPos + 1);
            // Trim spaces
            key.erase(0, key.find_first_not_of(L" "));
            key.erase(key.find_last_not_of(L" ") + 1);
            val.erase(0, val.find_first_not_of(L" "));
            val.erase(val.find_last_not_of(L" ") + 1);

            if (PathFileExistsW(val.c_str())) {
                manualPaths[key] = val;
            }
        }
    }
    file.close();
}

void ToolLauncher::SaveManualPaths() {
    std::wstring configPath = GetConfigFilePath();
    std::wofstream file(configPath, std::ios::trunc);
    if (!file.is_open()) return;

    for (const auto& pair : manualPaths) {
        file << pair.first << L"=" << pair.second << L"\n";
    }
    file.close();
}

std::wstring ToolLauncher::CheckFileExistsInDir(const std::wstring& dir, const std::wstring& primary, const std::wstring& alt) {
    try {
        fs::path dirPath(dir);
        fs::path primPath = dirPath / primary;
        if (fs::exists(primPath) && !fs::is_directory(primPath)) {
            return primPath.wstring();
        }

        if (!alt.empty()) {
            fs::path altPath = dirPath / alt;
            if (fs::exists(altPath) && !fs::is_directory(altPath)) {
                return altPath.wstring();
            }
        }
    } catch (...) {}
    return L"";
}

std::wstring ToolLauncher::ScanUsbForTool(const std::wstring& primaryExe, const std::wstring& altExe) {
    wchar_t driveStrings[256];
    DWORD size = GetLogicalDriveStringsW(256, driveStrings);
    if (size == 0) return L"";

    wchar_t systemDirectory[MAX_PATH];
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    std::wstring systemDrive = L"C:\\";
    if (wcslen(systemDirectory) >= 2) {
        systemDrive = std::wstring(systemDirectory, 3); // "C:\"
    }

    wchar_t* drive = driveStrings;
    while (*drive) {
        std::wstring drivePath(drive);
        
        // Skip system drive (C:) for quick scanning of external drives
        if (_wcsicmp(drivePath.c_str(), systemDrive.c_str()) == 0) {
            drive += wcslen(drive) + 1;
            continue;
        }

        UINT type = GetDriveTypeW(drivePath.c_str());
        // Scan removable (USB sticks) and fixed drives (like external HDDs/SSDs)
        if (type == DRIVE_REMOVABLE || type == DRIVE_FIXED) {
            // 1. Check root
            std::wstring found = CheckFileExistsInDir(drivePath, primaryExe, altExe);
            if (!found.empty()) return found;

            // 2. Check first-level directories
            try {
                for (const auto& entry : fs::directory_iterator(drivePath, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_directory()) {
                        std::wstring subDir = entry.path().wstring();
                        found = CheckFileExistsInDir(subDir, primaryExe, altExe);
                        if (!found.empty()) return found;

                        std::wstring folderName = entry.path().filename().wstring();
                        std::transform(folderName.begin(), folderName.end(), folderName.begin(), ::towlower);

                        // If folder matches search patterns, scan second-level subdirectories
                        if (folderName == L"tools" || folderName == L"testing" || folderName.find(L"diagnose") != std::wstring::npos) {
                            try {
                                for (const auto& subEntry : fs::directory_iterator(entry.path(), fs::directory_options::skip_permission_denied)) {
                                    if (subEntry.is_directory()) {
                                        found = CheckFileExistsInDir(subEntry.path().wstring(), primaryExe, altExe);
                                        if (!found.empty()) return found;
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                }
            } catch (...) {}
        }
        
        drive += wcslen(drive) + 1;
    }

    return L"";
}

void ToolLauncher::RefreshPaths() {
    for (auto& tool : tools) {
        // 1. Manual override
        if (manualPaths.count(tool.key) && PathFileExistsW(manualPaths[tool.key].c_str())) {
            tool.path = manualPaths[tool.key];
            continue;
        }

        // 2. USB scan
        std::wstring usbPath = ScanUsbForTool(tool.exeName, tool.alternateExeName);
        if (!usbPath.empty()) {
            tool.path = usbPath;
        } else {
            tool.path = L"";
        }
    }
}

void ToolLauncher::SaveManualPath(const std::wstring& key, const std::wstring& path) {
    if (PathFileExistsW(path.c_str())) {
        manualPaths[key] = path;
        for (auto& tool : tools) {
            if (tool.key == key) {
                tool.path = path;
                break;
            }
        }
        SaveManualPaths();
    }
}

void ToolLauncher::ClearManualPath(const std::wstring& key) {
    if (manualPaths.count(key)) {
        manualPaths.erase(key);
        SaveManualPaths();
        RefreshPaths();
    }
}

bool ToolLauncher::HasManualPath(const std::wstring& key) const {
    return manualPaths.count(key) > 0;
}

bool ToolLauncher::LaunchTool(const std::wstring& key, std::wstring& errorMessage) {
    errorMessage = L"";
    TestTool* targetTool = nullptr;
    for (auto& tool : tools) {
        if (tool.key == key) {
            targetTool = &tool;
            break;
        }
    }

    if (!targetTool) {
        errorMessage = L"Tool-Konfiguration nicht gefunden.";
        return false;
    }

    if (!targetTool->IsFound()) {
        errorMessage = L"Ausführbare Datei konnte nicht gefunden werden. Bitte USB-Stick anschließen oder Pfad manuell wählen.";
        return false;
    }

    std::wstring arguments = L"";
    if (key == L"FurMark") {
        std::wstring pathLower = targetTool->path;
        std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::towlower);
        
        bool isFurMark2 = (pathLower.find(L"furmark2") != std::wstring::npos || pathLower.find(L"furmark 2") != std::wstring::npos);
        
        if (isFurMark2) {
            // FurMark 2: Run standard 1080p OpenGL donut stress test directly
            arguments = L"--demo furmark-gl --p1080";
        } else {
            // FurMark 1.x: Run standard 1080p donut stress test directly without GUI
            arguments = L"/nogui /width=1920 /height=1080 /run_stress";
        }
    }

    // Launch process independently
    std::wstring dir = targetTool->path;
    size_t pos = dir.find_last_of(L"\\/");
    std::wstring workingDir = (pos != std::wstring::npos) ? dir.substr(0, pos) : L"";

    HINSTANCE hInst = ShellExecuteW(
        NULL,
        L"open",
        targetTool->path.c_str(),
        arguments.c_str(),
        workingDir.empty() ? NULL : workingDir.c_str(),
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<intptr_t>(hInst) > 32) {
        return true;
    }

    errorMessage = L"Starten des Prozesses fehlgeschlagen. Error Code: " + std::to_wstring(reinterpret_cast<intptr_t>(hInst));
    return false;
}

std::vector<TestTool>& ToolLauncher::GetTools() {
    return tools;
}
