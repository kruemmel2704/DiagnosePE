@echo off
rem =====================================================================
rem  Start-DiagnosePE.cmd - erster Prozess in der DiagnosePE-Umgebung
rem  (per winpeshl.ini gestartet). Initialisiert WinPE und uebergibt an
rem  PowerShell, das Netzwerk/WLAN einrichtet und die App im Vollbild startet.
rem =====================================================================
set DIAGPE_HOME=%~dp0..
set DIAGNOSETOOL_PE=1
set DIAGNOSETOOL_NO_UPDATE_CHECK=1

echo [DiagnosePE] WinPE wird initialisiert...
rem Netzwerkstack + kabelgebundenes DHCP initialisieren
wpeinit

rem Firewall in der Diagnoseumgebung aus (REST-API / Discovery des DiagnoseTool)
wpeutil DisableFirewall >nul 2>&1

rem Hohe Aufloesung versuchen (ignoriert, wenn der GPU-Treiber sie nicht kann)
wpeutil UpdateBootInfo >nul 2>&1

echo [DiagnosePE] Starte Diagnoseumgebung...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-DiagnosePE.ps1"

rem Faellt das DiagnoseTool aus oder wird es beendet: Not-Shell zum Debuggen.
echo.
echo [DiagnosePE] Das DiagnoseTool wurde beendet. Diagnose-Eingabeaufforderung:
cmd.exe
