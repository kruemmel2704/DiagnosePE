#include "system_repair.h"
#include <windows.h>
#include <shellapi.h>

bool SystemRepair::RunRepair(const std::wstring& key, std::wstring& outDescription) {
    std::wstring commandArgs = L"";
    
    if (key == L"sfc") {
        commandArgs = L"/k sfc /scannow";
        outDescription = L"System File Checker (SFC)";
    } else if (key == L"dism_restore") {
        commandArgs = L"/k dism /online /cleanup-image /restorehealth";
        outDescription = L"DISM Restore Health";
    } else if (key == L"dism_check") {
        commandArgs = L"/k dism /online /cleanup-image /checkhealth";
        outDescription = L"DISM Check Health";
    } else if (key == L"chkdsk") {
        commandArgs = L"/k chkdsk C:";
        outDescription = L"Chkdsk C: (Read-only)";
    } else {
        outDescription = L"Unbekannter Befehl";
        return false;
    }

    HINSTANCE hInst = ShellExecuteW(
        NULL,
        L"runas", // Triggers elevation prompt
        L"cmd.exe",
        commandArgs.c_str(),
        NULL,
        SW_SHOW
    );

    return (reinterpret_cast<intptr_t>(hInst) > 32);
}
