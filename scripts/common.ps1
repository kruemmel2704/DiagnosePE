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

    # Robuste Erkennung: copype.cmd direkt suchen (die ${env:...(x86)}-
    # Interpolation ist unzuverlaessig). Suchwurzeln = alle plausiblen
    # "Windows Kits"-Baeume; aus dem Fund werden alle Pfade abgeleitet.
    $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $pf   = [Environment]::GetEnvironmentVariable('ProgramFiles')
    $searchRoots = @(
        $(if ($pf86) { Join-Path $pf86 'Windows Kits' }),
        $(if ($pf)   { Join-Path $pf   'Windows Kits' }),
        'C:\Program Files (x86)\Windows Kits',
        'C:\Program Files\Windows Kits'
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

    $copype = $null
    foreach ($r in $searchRoots) {
        $hit = Get-ChildItem -Path $r -Recurse -Filter 'copype.cmd' -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { $copype = $hit.FullName; break }
    }
    if (-not $copype) {
        Fail "copype.cmd nicht gefunden (gesucht unter: $($searchRoots -join ', ')). 'Windows PE add-on' fehlt."
    }

    $peRoot = Split-Path -Parent $copype                      # ...\Windows Preinstallation Environment
    $adk    = Split-Path -Parent $peRoot                      # ...\Assessment and Deployment Kit
    $ocPath = Join-Path $peRoot "$Arch\WinPE_OCs"
    $makePe = Join-Path $peRoot "MakeWinPEMedia.cmd"

    # oscdimg.exe ebenfalls robust suchen (fuer MakeWinPEMedia / ISO)
    $makemed = $null
    $oscHit = Get-ChildItem -Path (Join-Path $adk 'Deployment Tools') -Recurse -Filter 'oscdimg.exe' -ErrorAction SilentlyContinue |
              Where-Object { $_.FullName -match "\\$Arch\\" } | Select-Object -First 1
    if (-not $oscHit) {
        $oscHit = Get-ChildItem -Path (Join-Path $adk 'Deployment Tools') -Recurse -Filter 'oscdimg.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($oscHit) { $makemed = $oscHit.FullName }

    if (-not (Test-Path $ocPath)) { Fail "WinPE_OCs nicht gefunden ($ocPath). 'Windows PE add-on' fehlt." }

    Write-Info "ADK-Root:  $adk"
    Write-Info "WinPERoot: $peRoot"
    Write-Info "oscdimg:   $makemed"

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
    $env:WinPERoot = $Adk.PeRoot
    $env:WinPERootNoArch = $Adk.PeRoot
    if ($Adk.Oscdimg) {
        $oscDir = Split-Path -Parent $Adk.Oscdimg
        $env:OSCDImgRoot = $oscDir
        # MakeWinPEMedia.cmd ruft 'oscdimg' ohne Pfad auf -> Verzeichnis in PATH legen.
        if (($env:PATH -split ';') -notcontains $oscDir) { $env:PATH = "$oscDir;$env:PATH" }
    }
    if ($Adk.Dism) { $env:DISMRoot = Split-Path -Parent $Adk.Dism }
}
