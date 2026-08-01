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

    TestTool gpustress;
    gpustress.key = L"gpu-stress";
    gpustress.name = L"Intensiver GPU Stresstest (stress-ng)";
    gpustress.exeName = L"stress-ng";
    gpustress.alternateExeName = L"furmark2";
    gpustress.description = L"Maximaler Belastungstest für den Grafikprozessor (GPU) mittels stress-ng OpenGL Shader-Stressor.";
    tools.push_back(gpustress);

    TestTool furmark;
    furmark.key = L"FurMark";
    furmark.name = L"GPU Stresstest (FurMark 2)";
    furmark.exeName = L"furmark2";
    furmark.alternateExeName = L"furmark";
    furmark.description = L"Grafik-Stresstest zur Auslastung der GPU und Überprüfung der thermischen Stabilität.";
    tools.push_back(furmark);
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
            std::string cmd;
            std::string path(tool.path.begin(), tool.path.end());
            if (key == L"gpu-stress") {
                cmd = "stress-ng --gpu 2 --gpu-frag 100 &";
            } else if (key == L"stress-ng") {
                cmd = "stress-ng --cpu 0 --io 2 --vm 2 --vm-bytes 512M &";
            } else if (key == L"memtester") {
                cmd = "xterm -title 'Memtester Speicherdiagnose' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'sudo memtester 256M 1; echo && read -p \"Drücke Enter zum Schließen...\"' &";
            } else {
                if (path.find("nvtop") != std::string::npos) {
                    cmd = "xterm -title 'NVTop GPU Monitor' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e '" + path + "' &";
                } else if (path.find("smartctl") != std::string::npos && path.find("gsmartcontrol") == std::string::npos) {
                    cmd = "xterm -title 'Smartctl Drive Info' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'sudo " + path + " -a /dev/sda || sudo " + path + " -a /dev/nvme0n1; echo && read -p \"Drücke Enter zum Schließen...\"' &";
                } else {
                    cmd = path + " &";
                }
            }
            int res = system(cmd.c_str());
            if (res == 0) return true;
            errorMessage = L"Fehler beim Starten des Linux-Tools.";
            return false;
        }
    }
    errorMessage = L"Unbekanntes Tool.";
    return false;
}

bool ToolLauncher::IsToolRunning(const std::wstring& key) {
    for (const auto& tool : tools) {
        if (tool.key == key) {
            std::string exe(tool.exeName.begin(), tool.exeName.end());
            std::string alt(tool.alternateExeName.begin(), tool.alternateExeName.end());
            std::string cmd;
            if (key == L"gpu-stress") {
                cmd = "pgrep -f \"[s]tress-ng --gpu\" >/dev/null 2>&1";
            } else if (key == L"stress-ng") {
                cmd = "pgrep -f \"[s]tress-ng\" | grep -v \"--gpu\" >/dev/null 2>&1";
            } else {
                cmd = "pgrep -x \"" + exe + "\" >/dev/null 2>&1 || pgrep -x \"" + alt + "\" >/dev/null 2>&1";
            }
            return system(cmd.c_str()) == 0;
        }
    }
    return false;
}

bool ToolLauncher::StopTool(const std::wstring& key) {
    for (const auto& tool : tools) {
        if (tool.key == key) {
            std::string exe(tool.exeName.begin(), tool.exeName.end());
            std::string alt(tool.alternateExeName.begin(), tool.alternateExeName.end());
            std::string cmd;
            if (key == L"gpu-stress") {
                cmd = "pkill -9 -f \"stress-ng --gpu\" >/dev/null 2>&1";
            } else if (key == L"stress-ng") {
                cmd = "for pid in $(pgrep -f \"[s]tress-ng\" | grep -v \"--gpu\"); do kill -9 $pid; done >/dev/null 2>&1";
            } else {
                cmd = "pkill -x \"" + exe + "\" >/dev/null 2>&1 || pkill -x \"" + alt + "\" >/dev/null 2>&1";
            }
            system(cmd.c_str());
            return true;
        }
    }
    return false;
}

std::vector<TestTool>& ToolLauncher::GetTools() {
    return tools;
}
