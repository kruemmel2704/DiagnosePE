# =====================================================================
#  DiagnosePE - Treiber-Katalog (fest verdrahtet)
# ---------------------------------------------------------------------
#  Hier stehen die konkret ausgewaehlten Treiberpakete, die
#  scripts/Get-Drivers.ps1 herunterlaedt, entpackt und nach
#  .\drivers\... ablegt. Build-DiagnosePE.ps1 injiziert sie dann per
#  DISM in das WinPE-Boot-Image.
#
#  Stand: 24.11.2026. GPU-Treiber (NVIDIA/AMD) veroeffentlichen fast
#  monatlich neue Versionen - zum Aktualisieren nur 'Version' und 'Url'
#  anpassen. 'Page' ist die offizielle Hersteller-Downloadseite; falls
#  eine 'Url' einmal nicht mehr aufloest, dort die aktuelle Datei holen
#  und 'Url' ersetzen.
#
#  'InfSubPath' = relativer Pfad INNERHALB des entpackten Pakets, der die
#  .inf-Dateien (reiner Treiber, kein Setup) enthaelt. Nur dieser Ordner
#  wird nach .\drivers\... uebernommen, damit DISM saubere INF-Treiber
#  bekommt (Hersteller-Installer/Setup sind in WinPE nicht lauffaehig).
# =====================================================================
@{
    # ---------------- Grafik: NVIDIA (OpenGL + Vulkan ICD enthalten) ----
    Nvidia = @{
        Name       = 'NVIDIA GeForce Game Ready (Desktop, DCH)'
        Version    = '621.24'
        # Vorhersagbares NVIDIA-Direktmuster: .../Windows/<Ver>/<Ver>-desktop-win10-win11-64bit-international-dch-whql.exe
        Url        = 'https://us.download.nvidia.com/Windows/621.24/621.24-desktop-win10-win11-64bit-international-dch-whql.exe'
        Page       = 'https://www.nvidia.com/download/index.aspx'
        # 7z-Selbstentpacker -> der reine Anzeigetreiber liegt unter Display.Driver
        InfSubPath = 'Display.Driver'
        Target     = 'gpu\nvidia'
        Kind       = '7z'
    }

    # ---------------- Grafik: AMD Radeon (OpenGL + Vulkan ICD enthalten) -
    Amd = @{
        Name       = 'AMD Software: Adrenalin Edition (WHQL)'
        Version    = '26.11.2'
        # ACHTUNG: AMD rotiert seine Direkt-URLs und hat kein stabiles Direktmuster.
        # Daher standardmaessig LEER (wird uebersprungen). Aktuelle "Windows 10/11
        # 64-bit"-EXE von 'Page' holen und hier eintragen, dann wird sie injiziert.
        Url        = ''
        Page       = 'https://www.amd.com/en/support/download/drivers.html'
        # AMD-Installer (7z) -> reine Display-INFs liegen unter Packages\Drivers\Display\WT6A_INF
        InfSubPath = 'Packages\Drivers\Display\WT6A_INF'
        Target     = 'gpu\amd'
        Kind       = '7z'
    }

    # ---------------- LAN: Intel Ethernet (Komplettpaket) ---------------
    IntelLan = @{
        Name       = 'Intel Ethernet Adapter Complete Driver Pack'
        Version    = '31.3'
        # Intel-Downloadmirror; Mirror-ID kann sich aendern -> ggf. auf 'Page' pruefen.
        Url        = 'https://downloadmirror.intel.com/851598/Release_31.3.zip'
        Page       = 'https://www.intel.com/content/www/us/en/download/15084/intel-ethernet-adapter-complete-driver-pack.html'
        # Im ZIP liegen je Adapterfamilie NDIS-Ordner; der ganze Windows-x64-Zweig wird uebernommen.
        InfSubPath = 'PROXGB\Winx64'
        Target     = 'net\lan\intel'
        Kind       = 'zip'
    }

    # ---------------- LAN: Realtek PCIe GbE / 2.5G ----------------------
    RealtekLan = @{
        Name       = 'Realtek PCIe GBE & 2.5G Ethernet (Win10/11 x64)'
        Version    = '2025-aktuell'
        # Realtek verteilt nur ueber Formular/Spiegel. Standardmaessig NICHT automatisch
        # ladbar -> Datei auf 'Page' holen und 'Url' setzen, sonst wird dieser Eintrag
        # von Get-Drivers.ps1 uebersprungen (mit Hinweis).
        Url        = ''
        Page       = 'https://www.realtek.com/Download/List?cate_id=584'
        InfSubPath = '.'
        Target     = 'net\lan\realtek'
        Kind       = 'zip'
    }

    # ---------------- WLAN: Intel Wi-Fi --------------------------------
    IntelWifi = @{
        Name       = 'Intel Wi-Fi Drivers (Windows 10/11 x64)'
        Version    = '23.150.0'
        # Intel Wi-Fi "Driver only"-Paket (entpackbar). Mirror-ID ggf. auf 'Page' pruefen.
        Url        = 'https://downloadmirror.intel.com/848895/WiFi-23.150.0-Driver64-Win10-Win11.exe'
        Page       = 'https://www.intel.com/content/www/us/en/download/19351/intel-wireless-wi-fi-drivers-for-windows-10-and-windows-11.html'
        InfSubPath = '.'
        Target     = 'net\wlan\intel'
        Kind       = '7z'
    }

    # ---------------- WLAN: Realtek / MediaTek -------------------------
    RealtekWifi = @{
        Name       = 'Realtek Wi-Fi (RTL88xx, Win10/11 x64)'
        Version    = '2025-aktuell'
        # Wie Realtek-LAN: nur ueber Formular. Standardmaessig uebersprungen,
        # bis eine 'Url' gesetzt ist.
        Url        = ''
        Page       = 'https://www.realtek.com/Download/List?cate_id=585'
        InfSubPath = '.'
        Target     = 'net\wlan\realtek'
        Kind       = 'zip'
    }
}
