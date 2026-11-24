<#
.SYNOPSIS
    Baut ein bootfaehiges WinPE-Image (DiagnosePE), das das DiagnoseTool im
    Vollbild startet - inkl. NVIDIA-/AMD-GPU-Treibern, LAN-/WLAN-Treibern,
    WLAN-Unterstuetzung (netsh wlan) und FurMark (OpenGL/Vulkan statt D3D).

.DESCRIPTION
    Voraussetzungen (siehe README.md):
      * Windows 10/11 x64
      * Windows ADK  +  "Windows PE add-on" (gleiche Version!)
      * 7-Zip (zum Entpacken der GPU-/Intel-Treiber)
      * Ausfuehrung in der *administrativen* "Deployment and Imaging Tools
        Environment"-Eingabeaufforderung.

    Ablauf:
      1) copype        -> frisches WinPE-Grundgeruest
      2) boot.wim mounten, Scratch-Space erhoehen
      3) Optional Components hinzufuegen (WMI, .NET, PowerShell, WLAN, ...)
      4) Treiber aus .\drivers\ per DISM injizieren (GPU + LAN + WLAN)
      5) DiagnoseTool + FurMark + Boot-Skripte ins Image kopieren,
         winpeshl.ini auf Vollbild-Autostart setzen
      6) boot.wim committen, ISO (und optional bootfaehigen USB) erzeugen

    App/Treiber vorher beschaffen:
      .\scripts\Get-DiagnoseTool.ps1          (Release-Exe; oder -FromSource)
      .\scripts\Get-Drivers.ps1               (feste Treiber aus config\drivers.psd1)
    Build-DiagnosePE.ps1 ruft beide bei Bedarf automatisch auf.

.EXAMPLE
    .\Build-DiagnosePE.ps1
    .\Build-DiagnosePE.ps1 -FromSource -OutputIso D:\DiagnosePE.iso
    .\Build-DiagnosePE.ps1 -UsbDrive F:        # zusaetzlich bootf. USB schreiben
#>
[CmdletBinding()]
param(
    [string]$Arch       = 'amd64',
    [string]$WorkDir    = (Join-Path $PSScriptRoot 'work\winpe'),
    [string]$OutputIso  = (Join-Path $PSScriptRoot 'out\DiagnosePE.iso'),
    [string]$UsbDrive,                       # z. B. 'F:' -> bootfaehigen USB-Stick schreiben
    [int]   $ScratchMb  = 512,               # RAM-Schreibspeicher in WinPE (max 512)
    [switch]$FromSource,                     # DiagnoseTool aus Quellcode bauen statt Release laden
    [switch]$SkipApp,                        # App-Beschaffung ueberspringen (schon in .\app)
    [switch]$SkipDrivers,                    # Treiber-Beschaffung ueberspringen (schon in .\drivers)
    [switch]$NoWiFi                          # WLAN-Komponente weglassen
)

. "$PSScriptRoot\scripts\common.ps1"

$repo = Get-RepoRoot
Assert-Admin
$adk = Get-AdkPaths -Arch $Arch
Write-Ok "ADK gefunden: $($adk.Adk)"

$media = Join-Path $WorkDir 'media'
$mount = Join-Path $WorkDir 'mount'
$bootWim = Join-Path $media 'sources\boot.wim'
$appDir  = Join-Path $repo 'app\DiagnoseTool'
$drvDir  = Join-Path $repo 'drivers'

# =====================================================================
# 0) App + Treiber beschaffen (falls noch nicht vorhanden)
# =====================================================================
if (-not $SkipApp) {
    Write-Step "DiagnoseTool beschaffen"
    $gdArgs = @{}
    if ($FromSource) { $gdArgs['FromSource'] = $true }
    & (Join-Path $repo 'scripts\Get-DiagnoseTool.ps1') @gdArgs
}
if (-not (Test-Path (Join-Path $appDir 'DiagnoseTool.exe'))) {
    Fail "DiagnoseTool.exe fehlt unter $appDir. Erst .\scripts\Get-DiagnoseTool.ps1 ausfuehren (oder -SkipApp weglassen)."
}

if (-not $SkipDrivers) {
    Write-Step "Treiber beschaffen"
    & (Join-Path $repo 'scripts\Get-Drivers.ps1')
}
$infTotal = (Get-ChildItem -Path $drvDir -Recurse -Filter *.inf -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Info "Treiber-INFs im Katalog: $infTotal"
if ($infTotal -eq 0) { Write-Warn2 "Keine INF-Treiber unter .\drivers gefunden - Image bootet, aber ggf. ohne GPU/NIC-Treiber." }

# =====================================================================
# 1) copype - frisches Grundgeruest
# =====================================================================
Write-Step "copype: WinPE-Grundgeruest erstellen"
if (Test-Path $WorkDir) {
    # evtl. haengenden Mount sauber loesen
    & $adk.Dism /Unmount-Image /MountDir:$mount /Discard 2>$null | Out-Null
    Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}
& cmd.exe /c "`"$($adk.CopyPe)`" $Arch `"$WorkDir`""
if (-not (Test-Path $bootWim)) { Fail "copype fehlgeschlagen - boot.wim nicht erstellt." }
Write-Ok "Grundgeruest: $WorkDir"

# =====================================================================
# 2) boot.wim mounten + Scratch-Space
# =====================================================================
Write-Step "boot.wim mounten"
New-Item -ItemType Directory -Force -Path $mount | Out-Null
& $adk.Dism /Mount-Image /ImageFile:$bootWim /Index:1 /MountDir:$mount
if ($LASTEXITCODE -ne 0) { Fail "Mounten von boot.wim fehlgeschlagen." }
& $adk.Dism /Image:$mount /Set-ScratchSpace:$ScratchMb | Out-Null
Write-Ok "Gemountet nach $mount (Scratch $ScratchMb MB)"

try {
    # =================================================================
    # 3) Optional Components
    # =================================================================
    Write-Step "Optional Components hinzufuegen"
    $lang = 'en-us'
    $ocPath = $adk.OcPath

    function Add-Oc {
        param([string]$Name)
        $base = Join-Path $ocPath "$Name.cab"
        if (-not (Test-Path $base)) { Write-Warn2 "OC fehlt (uebersprungen): $Name"; return }
        & $adk.Dism /Image:$mount /Add-Package /PackagePath:$base | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Warn2 "OC fehlgeschlagen: $Name" } else { Write-Ok $Name }
        $langCab = Join-Path $ocPath "$lang\$Name`_$lang.cab"
        if (Test-Path $langCab) { & $adk.Dism /Image:$mount /Add-Package /PackagePath:$langCab | Out-Null }
    }

    # Reihenfolge = Abhaengigkeiten (WMI -> NetFx -> Scripting -> PowerShell -> ...)
    $ocList = @(
        'WinPE-WMI',
        'WinPE-NetFX',
        'WinPE-Scripting',
        'WinPE-PowerShell',
        'WinPE-StorageWMI',
        'WinPE-DismCmdlets',
        'WinPE-EnhancedStorage',
        'WinPE-SecureStartup',
        'WinPE-FMAPI',
        'WinPE-Dot3Svc',          # 802.1x / kabelgebundene Authentifizierung
        'WinPE-PlatformId'
    )
    if (-not $NoWiFi) { $ocList += 'WinPE-WiFi-Package' }   # WLAN (netsh wlan / WlanSvc)
    foreach ($oc in $ocList) { Add-Oc $oc }

    # =================================================================
    # 4) Treiber injizieren (GPU + LAN + WLAN)
    # =================================================================
    Write-Step "Treiber injizieren (DISM /Add-Driver /Recurse)"
    if ($infTotal -gt 0) {
        & $adk.Dism /Image:$mount /Add-Driver /Driver:$drvDir /Recurse /ForceUnsigned
        if ($LASTEXITCODE -ne 0) { Write-Warn2 "DISM /Add-Driver meldete Fehler (ExitCode $LASTEXITCODE) - Log pruefen." }
        else { Write-Ok "Treiber injiziert." }
    } else { Write-Warn2 "Keine Treiber zum Injizieren." }

    # =================================================================
    # 5) DiagnoseTool + FurMark + Boot-Skripte einbetten
    # =================================================================
    Write-Step "DiagnoseTool + Boot-Konfiguration einbetten"
    $destApp  = Join-Path $mount 'DiagnoseTool'
    $destBoot = Join-Path $destApp 'boot'
    New-Item -ItemType Directory -Force -Path $destBoot | Out-Null

    Copy-Item -Path (Join-Path $appDir '*') -Destination $destApp -Recurse -Force
    Copy-Item -Path (Join-Path $repo 'winpe\Start-DiagnosePE.cmd') -Destination $destBoot -Force
    Copy-Item -Path (Join-Path $repo 'winpe\Start-DiagnosePE.ps1') -Destination $destBoot -Force

    # winpeshl.ini -> Vollbild-Autostart (ersetzt die Standard-Shell)
    Copy-Item -Path (Join-Path $repo 'winpe\winpeshl.ini') `
              -Destination (Join-Path $mount 'Windows\System32\winpeshl.ini') -Force
    Write-Ok "App unter X:\DiagnoseTool, Autostart via winpeshl.ini gesetzt."

} finally {
    # =================================================================
    # 6) Unmounten + committen
    # =================================================================
    Write-Step "boot.wim committen (unmount)"
    & $adk.Dism /Unmount-Image /MountDir:$mount /Commit
    if ($LASTEXITCODE -ne 0) { Fail "Unmount/Commit fehlgeschlagen - Mount ggf. mit 'Dism /Cleanup-Mountpoints' bereinigen." }
    Write-Ok "boot.wim committet."
}

# =====================================================================
# 7) ISO (und optional USB) erzeugen
# =====================================================================
Write-Step "ISO erzeugen"
$isoDir = Split-Path -Parent $OutputIso
if ($isoDir -and -not (Test-Path $isoDir)) { New-Item -ItemType Directory -Force -Path $isoDir | Out-Null }
if (Test-Path $OutputIso) { Remove-Item -Force $OutputIso }
& cmd.exe /c "`"$($adk.MakeWinPEMedia)`" /ISO `"$WorkDir`" `"$OutputIso`""
if (-not (Test-Path $OutputIso)) { Fail "ISO-Erstellung fehlgeschlagen." }
Write-Ok "ISO fertig: $OutputIso"

if ($UsbDrive) {
    Write-Step "Bootfaehigen USB-Stick schreiben ($UsbDrive)"
    Write-Warn2 "ACHTUNG: $UsbDrive wird formatiert!"
    & cmd.exe /c "`"$($adk.MakeWinPEMedia)`" /UFD `"$WorkDir`" $UsbDrive"
    Write-Ok "USB-Stick $UsbDrive ist bootfaehig."
}

Write-Host ""
Write-Ok   "DiagnosePE-Build abgeschlossen."
Write-Info "ISO: $OutputIso"
Write-Info "In VM/Hardware booten -> DiagnoseTool startet automatisch im Vollbild (PE-Modus)."
