# =====================================================================
#  Start-DiagnosePE.ps1 - Netzwerk/WLAN einrichten und DiagnoseTool
#  im Vollbild starten. Laeuft INNERHALB der WinPE-Umgebung.
# =====================================================================
$ErrorActionPreference = 'SilentlyContinue'

$appHome = Split-Path -Parent $PSScriptRoot        # X:\DiagnoseTool
$exe     = Join-Path $appHome 'DiagnoseTool.exe'
$log     = 'X:\DiagnosePE.log'

function Log { param($m) "$([DateTime]::Now.ToString('HH:mm:ss'))  $m" | Tee-Object -FilePath $log -Append | Out-Null }

Log "DiagnosePE-Startlogik laeuft."

# --- PE-Kennzeichnung fuer das DiagnoseTool ---------------------------
$env:DIAGNOSETOOL_PE            = '1'
$env:DIAGNOSETOOL_NO_UPDATE_CHECK = '1'
# Falls doch die Single-File-Exe verwendet wird: Entpackziel auf den
# RAM-Datentraeger legen (vermeidet Probleme mit %TEMP%).
$env:DOTNET_BUNDLE_EXTRACT_BASE_DIR = 'X:\dtx'
New-Item -ItemType Directory -Force -Path 'X:\dtx' | Out-Null

# --- WLAN-Dienst starten (WinPE-WiFi Optional Component) ---------------
# WlanSvc ermoeglicht 'netsh wlan' (Scan/Verbinden). Erst verfuegbar,
# wenn im Build das Paket WinPE-WiFi-Package eingebunden wurde.
$wlan = Get-Service -Name WlanSvc -ErrorAction SilentlyContinue
if ($wlan) {
    Log "Starte WLAN-Dienst (WlanSvc)..."
    Start-Service WlanSvc -ErrorAction SilentlyContinue
    # kurze Wartezeit, bis der Dienst die Adapter erfasst hat
    Start-Sleep -Seconds 3
    Log "WlanSvc Status: $((Get-Service WlanSvc).Status)"
} else {
    Log "WlanSvc nicht vorhanden - WLAN-Unterstuetzung wurde beim Build nicht eingebunden."
}

# --- Kabel-LAN sicherstellen (wpeinit hat DHCP bereits angestossen) ----
Log "Netzwerkadapter:"
Get-CimInstance Win32_NetworkAdapter -Filter "NetConnectionStatus=2" |
    ForEach-Object { Log "  verbunden: $($_.Name)" }

# --- DiagnoseTool im Vollbild starten ---------------------------------
if (-not (Test-Path $exe)) {
    Log "FEHLER: DiagnoseTool.exe nicht gefunden unter $exe"
    return
}
Log "Starte DiagnoseTool: $exe"
# Im Vordergrund starten und warten - solange die App laeuft, haelt die
# Umgebung. Die App setzt selbst WindowState=FullScreen (PE-Modus).
$p = Start-Process -FilePath $exe -WorkingDirectory $appHome -PassThru
if ($p) { $p.WaitForExit() ; Log "DiagnoseTool beendet (ExitCode $($p.ExitCode))." }
