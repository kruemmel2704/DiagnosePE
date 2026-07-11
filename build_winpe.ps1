# Build Custom WinPE ISO for CPM Diagnose Tool
# Auto-elevates to Administrator if run from non-elevated prompt.

$ErrorActionPreference = "Stop"

# 0. Check and Request Elevation
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Dieses Skript erfordert Administratorrechte. Starte neu als Administrator..." -ForegroundColor Yellow
    Start-Sleep -Seconds 1
    Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    Exit
}

try {
    # 1. Paths configuration
    $scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
    $compiledExe = Join-Path $scriptPath "bin\Release\DiagnosePE.exe"
    if (-not (Test-Path $compiledExe)) {
        # Check fallback local path
        $compiledExe = Join-Path $scriptPath "DiagnosePE.exe"
    }

    $isoOutput = Join-Path $scriptPath "DiagnosePE.iso"
    $adkToolsDir = "C:\Program Files (x86)\Windows Kits\10\Assessment and Deployment Kit"
    $dandisEnvBat = Join-Path $adkToolsDir "Deployment Tools\DandISetEnv.bat"
    $winPeToolsDir = Join-Path $adkToolsDir "Windows Preinstallation Environment"
    $copypeCmd = Join-Path $winPeToolsDir "copype.cmd"
    $makeWinPeMediaCmd = Join-Path $winPeToolsDir "MakeWinPEMedia.cmd"

    # Temporary build directory
    $buildDir = "C:\WinPE_amd64"
    $mountDir = Join-Path $buildDir "mount"

    Write-Host "=== CPM WinPE ISO Builder ===" -ForegroundColor Cyan

    # 2. Check prerequisites
    if (-not (Test-Path $compiledExe)) {
        Write-Error "DiagnosePE.exe nicht gefunden! Bitte kompiliere das C++-Projekt zuerst in Release/x64."
    }
    Write-Host "[OK] DiagnosePE.exe gefunden." -ForegroundColor Green

    if (-not (Test-Path $dandisEnvBat)) {
        Write-Error "DandISetEnv.bat nicht gefunden unter '$dandisEnvBat'. Bitte stelle sicher, dass das Windows ADK installiert ist."
    }
    if (-not (Test-Path $copypeCmd)) {
        Write-Error "WinPE-Add-on (copype.cmd) nicht gefunden unter '$winPeToolsDir'. Bitte stelle sicher, dass das WinPE Add-on installiert ist."
    }
    Write-Host "[OK] WinPE ADK Tools gefunden." -ForegroundColor Green

    # Test if destination ISO is locked before starting
    if (Test-Path $isoOutput) {
        try {
            $stream = [System.IO.File]::Open($isoOutput, 'Open', 'Write')
            $stream.Close()
        } catch {
            Write-Error "Die Ausgabedatei '$isoOutput' ist gesperrt (wird vermutlich von Hyper-V verwendet). Bitte schalte die VM aus und versuche es erneut."
        }
    }

    # 3. Clean up old build folder if it exists
    if (Test-Path $buildDir) {
        Write-Host "Bereinige altes temporäres Buildverzeichnis '$buildDir'..." -ForegroundColor Yellow
        
        # Try unmounting if left mounted
        & dism.exe /Get-MountedImageInfo | Out-Null
        # Force unmount if mounted
        & dism.exe /Unmount-Image /MountDir:$mountDir /Discard 2>$null
        
        # Wait and force delete
        Start-Sleep -Seconds 2
        Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    # 4. Copy WinPE template (calling DandISetEnv.bat first to set up WinPERoot environment variables)
    Write-Host "Erstelle WinPE template Verzeichnis..." -ForegroundColor Blue
    & cmd.exe /c "call `"$dandisEnvBat`" && `"$copypeCmd`" amd64 `"$buildDir`""

    # 5. Mount boot.wim
    Write-Host "Mounte boot.wim..." -ForegroundColor Blue
    $bootWim = Join-Path $buildDir "media\sources\boot.wim"
    & dism.exe /Mount-Image /ImageFile:$bootWim /Index:1 /MountDir:$mountDir

    # 6. Install WMI packages into WinPE (required for WMI system query metrics)
    Write-Host "Installiere WMI Optional Component in WinPE..." -ForegroundColor Blue
    $ocDir = Join-Path $adkToolsDir "Windows Preinstallation Environment\amd64\WinPE_OCs"
    $wmiCab = Join-Path $ocDir "WinPE-WMI.cab"
    if (Test-Path $wmiCab) {
        & dism.exe /Image:$mountDir /Add-Package /PackagePath:$wmiCab
        
        # Copy localization CAB
        $wmiDeCab = Join-Path $ocDir "de-de\WinPE-WMI_de-de.cab"
        $wmiEnCab = Join-Path $ocDir "en-us\WinPE-WMI_en-us.cab"
        if (Test-Path $wmiDeCab) {
            & dism.exe /Image:$mountDir /Add-Package /PackagePath:$wmiDeCab
        } elseif (Test-Path $wmiEnCab) {
            & dism.exe /Image:$mountDir /Add-Package /PackagePath:$wmiEnCab
        }
    } else {
        Write-Warning "WinPE-WMI.cab nicht gefunden! WMI-Abfragen funktionieren im WinPE eventuell nicht."
    }

    # 7. Copy DirectX runtime DLLs from host if not present (required for ImGui D3D11 WARP renderer)
    Write-Host "Kopiere DirectX-Bibliotheken für WARP-Render-Support..." -ForegroundColor Blue
    $system32Dir = Join-Path $mountDir "Windows\System32"
    $dlls = @("d3d11.dll", "dxgi.dll", "d3d10warp.dll", "d3dcompiler_47.dll")
    foreach ($dll in $dlls) {
        $targetPath = Join-Path $system32Dir $dll
        if (-not (Test-Path $targetPath)) {
            Write-Host "Kopiere $dll vom Host..." -ForegroundColor Green
            Copy-Item -Path "C:\Windows\System32\$dll" -Destination $system32Dir -Force
        } else {
            Write-Host "$dll ist bereits im WinPE-Image vorhanden (wird nicht überschrieben)." -ForegroundColor Yellow
        }
    }

    # 8. Create Target Directories and Copy files
    Write-Host "Kopiere CPM Diagnose Tool und Abhängigkeiten..." -ForegroundColor Blue
    $targetAppDir = Join-Path $mountDir "Program Files\DiagnoseTool"
    New-Item -ItemType Directory -Path $targetAppDir -Force | Out-Null

    Copy-Item -Path $compiledExe -Destination $targetAppDir -Force

    # Copy external Tools folder if it exists in the workspace
    $localToolsDir = Join-Path $scriptPath "Tools"
    if (Test-Path $localToolsDir) {
        Write-Host "Kopiere externe Diagnose-Tools (HWiNFO, OCCT, etc.)..." -ForegroundColor Green
        Copy-Item -Path $localToolsDir -Destination $targetAppDir -Recurse -Force
    } else {
        Write-Host "Hinweis: Kein lokaler 'Tools'-Ordner gefunden. Stresstest-Utilities (OCCT, Victoria, FurMark, HWiNFO) müssen im WinPE vom USB-Stick geladen werden." -ForegroundColor Yellow
    }

    # 9. Configure Autostart (edit startnet.cmd)
    Write-Host "Konfiguriere Autostart für das Diagnose Tool..." -ForegroundColor Blue
    $startnetCmd = Join-Path $mountDir "Windows\System32\startnet.cmd"
    $startnetContent = @"
wpeinit
ping 127.0.0.1 -n 3 >nul
cd "\Program Files\DiagnoseTool"
DiagnosePE.exe
"@
    Set-Content -Path $startnetCmd -Value $startnetContent -Encoding Ascii

    # 10. Unmount and Commit changes
    Write-Host "Speichere und unmounte boot.wim (kann einen Moment dauern)..." -ForegroundColor Blue
    & dism.exe /Unmount-Image /MountDir:$mountDir /Commit

    # 11. Build bootable ISO (calling DandISetEnv.bat first to set up OSCDImgRoot environment variables)
    Write-Host "Generiere bootbares ISO-Image unter '$isoOutput'..." -ForegroundColor Blue
    if (Test-Path $isoOutput) {
        Remove-Item $isoOutput -Force
    }
    & cmd.exe /c "call `"$dandisEnvBat`" && `"$makeWinPeMediaCmd`" /iso `"$buildDir`" `"$isoOutput`""

    # 12. Final Cleanup
    Write-Host "Lösche temporäres Buildverzeichnis..." -ForegroundColor Blue
    Remove-Item -Path $buildDir -Recurse -Force

    Write-Host "=============================================" -ForegroundColor Cyan
    Write-Host "[ERFOLGREICH] WinPE ISO wurde erstellt:" -ForegroundColor Green
    Write-Host "$isoOutput" -ForegroundColor White
    Write-Host "=============================================" -ForegroundColor Cyan
    Start-Sleep -Seconds 2
} catch {
    Write-Host ""
    Write-Host "!!! FEHLER BEIM BUILD-PROZESS !!!" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ""
    Read-Host "Drücke Enter zum Schließen dieses Fensters..."
    Exit 1
}
