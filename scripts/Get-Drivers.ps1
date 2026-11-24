<#
.SYNOPSIS
    Laedt die in config\drivers.psd1 fest verdrahteten Treiberpakete,
    entpackt sie und legt die reinen INF-Treiber unter .\drivers\... ab.

.DESCRIPTION
    - GPU (NVIDIA/AMD) und Intel-Installer sind 7z/zip-Archive; es wird nur
      der INF-Unterordner (reiner Treiber) uebernommen, nicht der Setup-Teil.
    - Eintraege ohne 'Url' (z. B. Realtek, nur ueber Formular verteilt) werden
      uebersprungen; das Skript nennt die 'Page', auf der die Datei liegt.
    - Build-DiagnosePE.ps1 injiziert spaeter ALLES unter .\drivers\ per DISM.

.PARAMETER Only
    Nur bestimmte Katalog-Eintraege verarbeiten, z. B. -Only Nvidia,IntelWifi

.PARAMETER KeepDownloads
    Heruntergeladene Installer nach dem Entpacken nicht loeschen.
#>
[CmdletBinding()]
param(
    [string[]]$Only,
    [switch]$KeepDownloads
)

. "$PSScriptRoot\common.ps1"

$repo    = Get-RepoRoot
$catalog = Import-PowerShellDataFile (Join-Path $repo 'config\drivers.psd1')
$drvRoot = Join-Path $repo 'drivers'
$dlDir   = Join-Path $repo 'work\download\drivers'
$tmpDir  = Join-Path $repo 'work\extract'

$sevenZip = Get-SevenZip
if (-not $sevenZip) {
    Write-Warn2 "7-Zip nicht gefunden. GPU-/Intel-Installer koennen ohne 7-Zip nicht entpackt werden."
    Write-Warn2 "Bitte 7-Zip installieren: https://www.7-zip.org/  (reine .zip-Pakete gehen auch ohne.)"
}

New-Item -ItemType Directory -Force -Path $dlDir, $tmpDir | Out-Null

function Expand-Package {
    param([string]$Archive, [string]$Dest, [string]$Kind)
    if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null

    if ($Kind -eq 'zip' -and ($Archive -match '\.zip$')) {
        Expand-Archive -Path $Archive -DestinationPath $Dest -Force
        return $true
    }
    # 7z / selbstentpackende .exe / sonstige Archive
    if (-not $sevenZip) { Write-Warn2 "Ueberspringe Entpacken (kein 7-Zip): $Archive"; return $false }
    & $sevenZip x -y "-o$Dest" $Archive | Out-Null
    return ($LASTEXITCODE -eq 0)
}

$entries = $catalog.GetEnumerator()
if ($Only) { $entries = $entries | Where-Object { $Only -contains $_.Key } }

$summary = @()
foreach ($kv in $entries) {
    $key = $kv.Key; $d = $kv.Value
    Write-Step "$key - $($d.Name) ($($d.Version))"

    if ([string]::IsNullOrWhiteSpace($d.Url)) {
        Write-Warn2 "Keine Direkt-URL hinterlegt - uebersprungen."
        Write-Info  "Manuell laden auf: $($d.Page)"
        Write-Info  "Danach entpackte INF-Dateien nach: $(Join-Path $drvRoot $d.Target)"
        $summary += [pscustomobject]@{ Treiber=$key; Status='uebersprungen (keine URL)' }
        continue
    }

    $ext  = if ($d.Url -match '\.zip($|\?)') { 'zip' } else { 'exe' }
    $file = Join-Path $dlDir ("{0}.{1}" -f $key, $ext)

    if (-not (Test-Path $file)) {
        if (-not (Invoke-Download -Url $d.Url -OutFile $file)) {
            Write-Warn2 "Download fehlgeschlagen. Aktuelle Datei manuell holen auf: $($d.Page)"
            $summary += [pscustomobject]@{ Treiber=$key; Status='Download fehlgeschlagen' }
            continue
        }
    } else { Write-Info "Bereits vorhanden: $file" }

    $extractTo = Join-Path $tmpDir $key
    if (-not (Expand-Package -Archive $file -Dest $extractTo -Kind $d.Kind)) {
        Write-Warn2 "Entpacken fehlgeschlagen."
        $summary += [pscustomobject]@{ Treiber=$key; Status='Entpacken fehlgeschlagen' }
        continue
    }

    # INF-Unterordner bestimmen
    $infSource = if ($d.InfSubPath -and $d.InfSubPath -ne '.') { Join-Path $extractTo $d.InfSubPath } else { $extractTo }
    if (-not (Test-Path $infSource)) {
        # Fallback: ersten Ordner suchen, der ueberhaupt *.inf enthaelt
        $firstInf = Get-ChildItem -Path $extractTo -Recurse -Filter *.inf -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($firstInf) { $infSource = $firstInf.Directory.FullName }
        else { Write-Warn2 "Kein INF-Ordner gefunden (InfSubPath='$($d.InfSubPath)')."; $summary += [pscustomobject]@{ Treiber=$key; Status='keine INF gefunden' }; continue }
        Write-Warn2 "InfSubPath nicht vorhanden - nutze gefundenen INF-Ordner: $infSource"
    }

    $target = Join-Path $drvRoot $d.Target
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $infSource '*') -Destination $target -Recurse -Force

    $infCount = (Get-ChildItem -Path $target -Recurse -Filter *.inf -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Ok "$infCount INF-Datei(en) -> $target"
    $summary += [pscustomobject]@{ Treiber=$key; Status="OK ($infCount INF)" }

    if (-not $KeepDownloads) { Remove-Item -Force $file -ErrorAction SilentlyContinue }
}

Write-Step "Zusammenfassung Treiber"
$summary | Format-Table -AutoSize | Out-String | Write-Host
Write-Info "Alle Treiber liegen unter: $drvRoot (werden von Build-DiagnosePE.ps1 per DISM injiziert)."
