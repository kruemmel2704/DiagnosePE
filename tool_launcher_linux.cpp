#include "tool_launcher.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

bool TestTool::IsFound() const {
    return !path.empty() && fs::exists(path);
}

std::wstring TestTool::GetDisplayStatus() const {
    return IsFound() ? L"Bereit (Vorinstalliert / Pfad vorhanden)" : L"Nicht vorhanden (Kann nachinstalliert werden)";
}

ToolLauncher::ToolLauncher() {
    InitializeTools();
    RefreshPaths();
}

void ToolLauncher::InitializeTools() {
    tools.clear();
    
    TestTool smartctl;
    smartctl.key = L"smartctl";
    smartctl.name = L"SMART & NVMe Drive Utility (smartctl / GSmartControl)";
    smartctl.exeName = L"gsmartcontrol";
    smartctl.alternateExeName = L"smartctl";
    smartctl.description = L"Auslesen von SMART-Gesundheitsdaten, NVMe-Protokollen und Durchführen von Festplatten-Selbsttests.";
    tools.push_back(smartctl);

    TestTool stressng;
    stressng.key = L"stress-ng";
    stressng.name = L"Stress-ng CPU/RAM/IO Stresstest";
    stressng.exeName = L"stress-ng";
    stressng.alternateExeName = L"mprime";
    stressng.description = L"Extremer Belastungstest für Prozessorkerne, Hauptspeicher und E/A-Subsysteme.";
    tools.push_back(stressng);

    TestTool gparted;
    gparted.key = L"GParted";
    gparted.name = L"GParted Partition Editor";
    gparted.exeName = L"gparted";
    gparted.alternateExeName = L"partitionmanager";
    gparted.description = L"Professioneller Partitionseditor zum Erstellen, Prüfen, Verschieben und Reparieren von Dateisystemen.";
    tools.push_back(gparted);

    TestTool memtest;
    memtest.key = L"memtester";
    memtest.name = L"Memtester Memory Diagnostic";
    memtest.exeName = L"memtester";
    memtest.alternateExeName = L"memtest86+";
    memtest.description = L"Speicherdiagnoseprogramm zur Verifizierung von RAM-Zellen und Erkennung von Bit-Flips im Adressraum.";
    tools.push_back(memtest);

    TestTool glmark;
    glmark.key = L"glmark2";
    glmark.name = L"GLMark2 / NVTop GPU Utility";
    glmark.exeName = L"glmark2";
    glmark.alternateExeName = L"nvtop";
    glmark.description = L"3D-OpenGL Grafikleistungstest und GPU-Stresstest zur Überprüfung der Grafikstabilität.";
    tools.push_back(glmark);
}

std::wstring ToolLauncher::GetConfigFilePath() {
    return L"/tmp/tool_paths.txt";
}

void ToolLauncher::LoadManualPaths() {}
void ToolLauncher::SaveManualPaths() {}
std::wstring ToolLauncher::ScanUsbForTool(const std::wstring& primaryExe, const std::wstring& altExe) { return L""; }
std::wstring ToolLauncher::CheckFileExistsInDir(const std::wstring& dir, const std::wstring& primary, const std::wstring& alt) { return L""; }

void ToolLauncher::RefreshPaths() {
    for (auto& tool : tools) {
        std::string primary(tool.exeName.begin(), tool.exeName.end());
        std::string alt(tool.alternateExeName.begin(), tool.alternateExeName.end());

        std::vector<std::string> searchPaths = {
            "/usr/bin/" + primary,
            "/usr/local/bin/" + primary,
            "/bin/" + primary,
            "/usr/bin/" + alt,
            "/usr/local/bin/" + alt,
            "/bin/" + alt
        };

        tool.path = L"";
        for (const auto& p : searchPaths) {
            if (fs::exists(p)) {
                tool.path = std::wstring(p.begin(), p.end());
                break;
            }
        }
    }
}

void ToolLauncher::SaveManualPath(const std::wstring& key, const std::wstring& path) {}
void ToolLauncher::ClearManualPath(const std::wstring& key) {}
bool ToolLauncher::HasManualPath(const std::wstring& key) const { return false; }

bool ToolLauncher::LaunchTool(const std::wstring& key, std::wstring& errorMessage) {
    for (const auto& tool : tools) {
        if (tool.key == key) {
            if (!tool.IsFound()) {
                errorMessage = L"Das Tool ist auf dem Linux Live System nicht installiert.";
                return false;
            }
            std::string cmd(tool.path.begin(), tool.path.end());
            cmd += " &"; // Run async in background
            int res = system(cmd.c_str());
            if (res == 0) return true;
            errorMessage = L"Fehler beim Starten des Linux-Tools.";
            return false;
        }
    }
    errorMessage = L"Unbekanntes Tool.";
    return false;
}

std::vector<TestTool>& ToolLauncher::GetTools() {
    return tools;
}
