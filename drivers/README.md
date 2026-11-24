# Treiber für DiagnosePE

In diesen Ordner kommen die **reinen INF-Treiber** (kein Hersteller-Setup),
die beim Build per `DISM /Add-Driver /Recurse` in das WinPE-Image injiziert
werden. Große Treiberbinärdateien gehören **nicht ins Git-Repo** – die
`.gitignore` hält diesen Baum bis auf die Platzhalter leer.

## Automatisch befüllen (empfohlen)

```powershell
.\scripts\Get-Drivers.ps1
```

Lädt die in [`config/drivers.psd1`](../config/drivers.psd1) fest verdrahteten
Pakete, entpackt sie und legt die INF-Ordner hier ab. Voraussetzung:
**7-Zip** (für die GPU-/Intel-Installer).

## Ordnerstruktur

| Ordner | Inhalt | Quelle (Katalog-Schlüssel) |
|---|---|---|
| `gpu/nvidia/`   | NVIDIA Display-Treiber (`Display.Driver`) – bringt OpenGL- & Vulkan-ICD | `Nvidia` |
| `gpu/amd/`      | AMD Display-Treiber (`WT6A_INF`) – bringt OpenGL- & Vulkan-ICD | `Amd` |
| `net/lan/intel/`| Intel Ethernet NDIS-Treiber | `IntelLan` |
| `net/lan/realtek/`| Realtek PCIe GbE / 2.5G | `RealtekLan` |
| `net/wlan/intel/`| Intel Wi-Fi | `IntelWifi` |
| `net/wlan/realtek/`| Realtek Wi-Fi | `RealtekWifi` |

## Manuell befüllen

Falls ein Eintrag keine Direkt-URL hat (z. B. Realtek wird nur über ein
Formular verteilt), die Datei von der in `drivers.psd1` genannten `Page`
laden, entpacken und die `*.inf` (samt `*.sys`/`*.cat`) in den passenden
Ordner oben kopieren. Beim nächsten Build werden sie automatisch mitgenommen.

> Wichtig: Es müssen die **Treiber-INFs** sein, nicht der Installer (`setup.exe`).
> Hersteller-Setups laufen in WinPE nicht.
