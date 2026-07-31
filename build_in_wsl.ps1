# PowerShell helper script to build DiagnoseLinux.iso on Windows via WSL2

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "    DiagnoseLinux Live ISO Builder (Windows WSL2)         " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Convert current Windows path to WSL path format (/mnt/c/...)
$winPath = Get-Location
$driveLetter = $winPath.Drive.Name.ToLower()
$relativePath = $winPath.Path.Substring(3).Replace('\', '/')
$wslPath = "/mnt/$driveLetter/$relativePath"

Write-Host "[+] Projektpfad in WSL: $wslPath" -ForegroundColor Yellow

if (Get-Command "wsl" -ErrorAction SilentlyContinue) {
    Write-Host "[+] Starte 100% Linux Live-Build..." -ForegroundColor Green
    
    $wslArgs = "-d archlinux -u root bash -c ""cd '$wslPath' && chmod +x build_linux_live.sh && ./build_linux_live.sh"""
    
    $process = Start-Process -FilePath "wsl.exe" -ArgumentList $wslArgs -NoNewWindow -PassThru -Wait

    # Check if process succeeded before showing success info
    if ($process.ExitCode -ne 0) {
        Write-Host "[!] WSL-Build fehlgeschlagen. Der Befehl gab Exitcode $($process.ExitCode) zurück." -ForegroundColor Red
        Exit 1
    }

    if (Test-Path "DiagnoseLinux.iso") {
        # Grant Hyper-V / Windows service permissions to access ISO
        icacls DiagnoseLinux.iso /grant "Everyone:(R)" | Out-Null
        icacls DiagnoseLinux.iso /grant "Jeder:(R)" | Out-Null
        icacls DiagnoseLinux.iso /grant "*S-1-5-83-0:(R)" | Out-Null
        icacls DiagnoseLinux.iso /grant "NT VIRTUAL MACHINE\Virtual Machines:(R)" | Out-Null
        
        Write-Host ""
        Write-Host "==========================================================" -ForegroundColor Green
        Write-Host " [SUCCESS] DiagnoseLinux.iso wurde erfolgreich erstellt!" -ForegroundColor Green
        Write-Host " Hyper-V Leserechte wurden automatisch erteilt!" -ForegroundColor Yellow
        Write-Host " Pfad: $(Get-Location)\DiagnoseLinux.iso" -ForegroundColor White
        Write-Host "==========================================================" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "[!] WSL-Build wurde mit Fehlercode $($process.ExitCode) beendet." -ForegroundColor Red
        Write-Host "Tipp: Falls WSL Fehler E_FAIL anzeigt, führe 'wsl --shutdown' in der PowerShell aus." -ForegroundColor Yellow
    }
} else {
    Write-Host "[!] WSL2 ist auf diesem Windows-System noch nicht installiert." -ForegroundColor Red
    Write-Host "Bitte installiere WSL2 in der PowerShell mit: wsl --install" -ForegroundColor Yellow
}
