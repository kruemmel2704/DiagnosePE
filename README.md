# DiagnosePE — WinPE-Build für das CPM DiagnoseTool

**DiagnosePE** ist kein eigenes Programm mehr, sondern ein **Build-System**,
das eine bootfähige **WinPE-Umgebung** erzeugt, die das
[CPM **DiagnoseTool**](https://github.com/kruemmel2704/DiagnoseTool)
beim Start automatisch im **Vollbild** lädt.

Das Image bringt mit:

- 🎮 **GPU-Treiber** für **NVIDIA** und **AMD** (inkl. OpenGL- & Vulkan-ICD)
- 🔥 **FurMark** als GPU-Stresstest über **OpenGL/Vulkan** (kein Direct3D — das läuft in WinPE nicht)
- 🌐 **LAN-** und **WLAN-Treiber** (Intel/Realtek) + WLAN-Unterstützung (`netsh wlan`)
- 🖥️ **Vollbild-Autostart** des DiagnoseTool (PE-Modus mit Software-Rendering, also ohne D3D/ANGLE)
- 📶 **Netzwerk-Einstellungen nur in der PE-Edition**: LAN/WLAN im DiagnoseTool konfigurieren und Rückmeldung (Verbindung, IP, Ping) bekommen

> **Historie:** Dieses Repo war vorher ein C++/ImGui-Projekt mit Linux-Live-Build.
> Das wurde vollständig entfernt — der komplette Diagnose-Funktionsumfang
> steckt jetzt im **DiagnoseTool** (.NET/Avalonia), das hier nur noch in ein
> WinPE-Image verpackt wird.

---

## Warum WinPE statt Linux?

- Einheitliche **Windows-Treiber** (NVIDIA/AMD/Intel/Realtek) direkt injizierbar.
- Das **DiagnoseTool** läuft nativ (Windows-Sensorik via LibreHardwareMonitor,
  REST-API, PDF-Berichte) — kein Cross-Compile, kein Linux-Nachbau.
- WinPE bootet schlank von USB/CD und kann Hardware testen, **bevor** ein OS
  installiert ist.

**Direct3D fehlt in WinPE.** Deshalb rendert das DiagnoseTool im PE-Modus über
**Software/GDI** (Avalonia), und FurMark läuft über **OpenGL** (`--demo furmark-gl`)
bzw. **Vulkan** (`--demo furmark-vk`) — beide ICDs kommen mit den injizierten
GPU-Treibern.

---

## Voraussetzungen (Build-Rechner, Windows x64)

1. **Windows ADK** und das passende **„Windows PE add-on"** (gleiche Version!)
   - https://learn.microsoft.com/windows-hardware/get-started/adk-install
2. **7-Zip** (entpackt die GPU-/Intel-Treiber): https://www.7-zip.org
3. Optional für `-FromSource`: **.NET SDK 10** (sonst wird die fertige Release-Exe geladen)
4. Build immer in der **„Deployment and Imaging Tools Environment"** starten
   (Startmenü, als **Administrator**) — dort sind `copype`, `DISM` & Co. im Pfad.

---

## Schnellstart

```powershell
# in der administrativen "Deployment and Imaging Tools Environment":
cd <Pfad>\diagnosepe

# 1) alles bauen (App laden + Treiber laden + Image + ISO):
.\Build-DiagnosePE.ps1

# Ergebnis:
#   out\DiagnosePE.iso   -> in VM/auf Hardware booten
```

Zusätzlich einen bootfähigen USB-Stick schreiben (formatiert das Laufwerk!):

```powershell
.\Build-DiagnosePE.ps1 -UsbDrive F:
```

DiagnoseTool aus Quellcode statt Release-Exe (Ordner-Publish, RAM-schonender):

```powershell
.\Build-DiagnosePE.ps1 -FromSource
```

---

## Einzelschritte (optional)

```powershell
# nur DiagnoseTool beschaffen  (-> app\DiagnoseTool\)
.\scripts\Get-DiagnoseTool.ps1            # Release-Exe
.\scripts\Get-DiagnoseTool.ps1 -FromSource

# nur Treiber beschaffen        (-> drivers\...)
.\scripts\Get-Drivers.ps1
.\scripts\Get-Drivers.ps1 -Only Nvidia,IntelWifi

# Image bauen, App/Treiber bereits vorhanden:
.\Build-DiagnosePE.ps1 -SkipApp -SkipDrivers
```

---

## Treiber pflegen

Die konkret ausgewählten Pakete stehen in
[`config/drivers.psd1`](config/drivers.psd1) (NVIDIA, AMD, Intel-LAN, Realtek-LAN,
Intel-WLAN, Realtek-WLAN). GPU-Treiber erscheinen fast monatlich neu — zum
Aktualisieren einfach `Version` + `Url` anpassen. Realtek verteilt nur über ein
Formular; dort die Datei von der `Page` holen und die `Url` eintragen (oder die
INFs direkt nach `drivers/net/...` legen). Details: [`drivers/README.md`](drivers/README.md).

---

## Was im Image passiert

1. `copype amd64` → frisches WinPE-Grundgerüst, `boot.wim` wird gemountet.
2. **Optional Components**: WMI, .NET, Scripting, PowerShell, EnhancedStorage,
   SecureStartup, FMAPI, Dot3Svc und **WinPE-WiFi-Package** (WLAN).
3. **Treiber** aus `drivers\` werden per `DISM /Add-Driver /Recurse` injiziert.
4. **DiagnoseTool + FurMark** landen unter `X:\DiagnoseTool`, die Boot-Skripte
   unter `X:\DiagnoseTool\boot`, und `winpeshl.ini` startet alles im Vollbild.
5. `boot.wim` wird committet, daraus entsteht `out\DiagnosePE.iso`.

Beim Booten:
`winpeshl.ini` → `Start-DiagnosePE.cmd` (`wpeinit`, Netzwerk) →
`Start-DiagnosePE.ps1` (startet **WlanSvc** für WLAN, setzt `DIAGNOSETOOL_PE=1`,
startet das DiagnoseTool im Vollbild).

---

## Grenzen / Hinweise

- **Bauen/Testen nur auf Windows** mit ADK + echter/virtueller GPU. Dieses Repo
  enthält nur das Build-System; das Image selbst wird auf dem Build-Rechner erzeugt.
- WinPE-Scratch-Space ist auf **512 MB** begrenzt. Bei Speicherengpässen die
  **`-FromSource`**-Variante (Ordner-Publish) nutzen — dann muss die .NET-Runtime
  nicht erst in den RAM entpackt werden.
- WLAN in WinPE benötigt das **WinPE-WiFi-Package** (ist eingebunden) und einen
  passenden **WLAN-Treiber** in `drivers/net/wlan/`. Verbindung/Status werden im
  DiagnoseTool (PE-Modus) unter **Netzwerk** gesteuert.
- Secure Boot kann unsignierte Images blocken — ggf. im BIOS deaktivieren oder
  signiertes Boot-Medium verwenden.

---

## Verzeichnisstruktur

```
diagnosepe/
├─ Build-DiagnosePE.ps1        # Master-Build (ADK/DISM -> ISO/USB)
├─ config/drivers.psd1         # fest verdrahtete Treiber (Version/URL)
├─ scripts/
│  ├─ common.ps1               # gemeinsame Helfer (ADK-Pfade, Download, 7-Zip)
│  ├─ Get-DiagnoseTool.ps1     # Release-Exe laden  (oder -FromSource bauen)
│  └─ Get-Drivers.ps1          # Treiber laden/entpacken -> drivers\
├─ winpe/
│  ├─ winpeshl.ini             # Vollbild-Autostart-Shell
│  ├─ Start-DiagnosePE.cmd     # wpeinit + Netzwerk
│  └─ Start-DiagnosePE.ps1     # WlanSvc + App im Vollbild starten
├─ drivers/                    # INF-Treiber (per .gitignore leer gehalten)
└─ Logo/
```
