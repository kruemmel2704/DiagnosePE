# =====================================================================
#  DiagnosePE - gemeinsame Hilfsfunktionen (wird von anderen Skripten
#  per Dot-Sourcing eingebunden: . "$PSScriptRoot\common.ps1")
# =====================================================================

$ErrorActionPreference = 'Stop'

function Write-Step  { param([string]$Msg) Write-Host "`n==> $Msg" -ForegroundColor Cyan }
function Write-Info  { param([string]$Msg) Write-Host "    $Msg" -ForegroundColor Gray }
function Write-Ok    { param([string]$Msg) Write-Host "    [OK] $Msg" -ForegroundColor Green }
function Write-Warn2 { param([string]$Msg) Write-Host "    [!]  $Msg" -ForegroundColor Yellow }
function Fail        { param([string]$Msg) throw "DiagnosePE: $Msg" }

# Wurzelverzeichnis des Repos (eine Ebene ueber \scripts)
function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-Admin {
    if (-not (Test-IsAdmin)) {
        Fail "Dieses Skript muss in einer *administrativen* 'Deployment and Imaging Tools Environment'-Eingabeaufforderung laufen."
    }
}

# 7-Zip finden (noetig zum Entpacken der NVIDIA-/AMD-/Intel-Installer).
function Get-SevenZip {
    $candidates = @(
        "$env:ProgramFiles\7-Zip\7z.exe",
        "${env:ProgramFiles(x86)}\7-Zip\7z.exe",
        "$env:ProgramW6432\7-Zip\7z.exe"
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Datei robust herunterladen (mehrere Versuche, fortschrittsarm).
function Invoke-Download {
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$OutFile,
        [int]$Retries = 4
    )
    $dir = Split-Path -Parent $OutFile
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

    for ($i = 1; $i -le $Retries; $i++) {
        try {
            Write-Info "Lade (Versuch $i/$Retries): $Url"
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing -MaximumRedirection 10
            if ((Get-Item $OutFile).Length -lt 1024) { Fail "Download zu klein (vermutlich Fehlerseite)." }
            Write-Ok ("Gespeichert: {0} ({1:N1} MB)" -f $OutFile, ((Get-Item $OutFile).Length / 1MB))
            return $true
        } catch {
            Write-Warn2 "Fehlgeschlagen: $($_.Exception.Message)"
            Start-Sleep -Seconds ([math]::Min(30, 5 * $i))
        }
    }
    return $false
}

# ADK- / WinPE-Pfade ermitteln (copype, DISM, OC-Pakete, MakeWinPEMedia).
function Get-AdkPaths {
    param([string]$Arch = 'amd64')

    # ProgramFiles(x86) eindeutig ueber die .NET-API holen (die ${env:...(x86)}-
    # Interpolation ist unzuverlaessig und lieferte in CI einen falschen Pfad).
    $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $pf   = [Environment]::GetEnvironmentVariable('ProgramFiles')
    $kit  = 'Windows Kits\10\Assessment and Deployment Kit'
    $roots = @(
        $(if ($pf86) { Join-Path $pf86 $kit }),
        $(if ($pf)   { Join-Path $pf   $kit })
    ) | Where-Object { $_ -and (Test-Path $_) }

    if (-not $roots) {
        Fail "Windows ADK nicht gefunden (gesucht unter '$pf86' / '$pf'). Bitte Windows ADK + 'Windows PE add-on' installieren (siehe README)."
    }
    $adk = $roots[0]

    $peRoot  = Join-Path $adk "Windows Preinstallation Environment"
    $copype  = Join-Path $peRoot "copype.cmd"
    $makemed = Join-Path (Join-Path $adk "Deployment Tools\$Arch\Oscdimg") "oscdimg.exe"
    $ocPath  = Join-Path $peRoot "$Arch\WinPE_OCs"
    $makePe  = Join-Path $peRoot "MakeWinPEMedia.cmd"

    if (-not (Test-Path $copype)) { Fail "copype.cmd nicht gefunden ($copype). 'Windows PE add-on' fehlt." }
    if (-not (Test-Path $ocPath)) { Fail "WinPE_OCs nicht gefunden ($ocPath). 'Windows PE add-on' fehlt." }

    $dism = Join-Path $adk "Deployment Tools\$Arch\DISM\dism.exe"
    if (-not (Test-Path $dism)) { $dism = "$env:SystemRoot\System32\Dism.exe" }

    return [pscustomobject]@{
        Adk           = $adk
        PeRoot        = $peRoot
        CopyPe        = $copype
        MakeWinPEMedia= $makePe
        Oscdimg       = $makemed
        OcPath        = $ocPath
        Dism          = $dism
        Arch          = $Arch
    }
}

# Setzt die Umgebungsvariablen, die copype.cmd / MakeWinPEMedia.cmd erwarten
# (sonst nur ueber "Deployment and Imaging Tools Environment" gesetzt). Damit
# laeuft der Build auch aus einer normalen (administrativen) Shell/CI heraus.
function Set-AdkEnvironment {
    param([Parameter(Mandatory)] $Adk)
    $env:WinPERoot   = $Adk.PeRoot
    $env:OSCDImgRoot = Split-Path -Parent $Adk.Oscdimg
    $env:DISMRoot    = Split-Path -Parent $Adk.Dism
    $env:WinPERootNoArch = $Adk.PeRoot
}
