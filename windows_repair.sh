#!/bin/bash
# ==========================================================
#     Linux-based Windows Repair & Password Reset Tool
# ==========================================================
# This script is called from the DiagnoseLinux interface
# to repair local Windows partitions offline or reset passwords.

set -e

action="$1"

if [ -z "$action" ]; then
    echo "Fehler: Keine Aktion angegeben."
    echo "Nutzung: $0 [ntfsfix|chntpw]"
    read -p "Drücke Enter zum Beenden..."
    exit 1
fi

echo "=========================================================="
echo "    Starte Offline-Wartung für Windows-Systeme            "
echo "=========================================================="
echo ""

# Scan for NTFS partitions
# Use lsblk to find partitions with ntfs type
partitions=$(lsblk -lo NAME,FSTYPE | grep ntfs | awk '{print $1}' || true)

if [ -z "$partitions" ]; then
    echo "[!] Es wurden keine NTFS-Partitionen (Windows-Installationen) gefunden."
    echo "    Bitte stelle sicher, dass die Festplatten korrekt angeschlossen sind."
    echo ""
    read -p "Drücke Enter zum Beenden..."
    exit 0
fi

echo "Erkannte Windows (NTFS) Partitionen:"
for p in $partitions; do
    size=$(lsblk -no SIZE "/dev/$p" | head -n 1)
    echo "  - /dev/$p ($size)"
done
echo ""

if [ "$action" = "ntfsfix" ]; then
    echo "[+] Führe NTFS-Dateisystemprüfung (ntfsfix) aus..."
    echo "----------------------------------------------------------"
    for p in $partitions; do
        echo "[+] Prüfe /dev/$p..."
        # ntfsfix -b (clear bad sectors list if any) and -d (clear dirty flag)
        if sudo ntfsfix -b -d "/dev/$p"; then
            echo "[SUCCESS] /dev/$p wurde erfolgreich repariert."
        else
            echo "[FEHLER] Fehler bei der Reparatur von /dev/$p."
        fi
        echo ""
    done
    echo "----------------------------------------------------------"
    echo "[+] NTFS-Dateisystemprüfung abgeschlossen."
    read -p "Drücke Enter zum Beenden..."

elif [ "$action" = "chntpw" ]; then
    # If there's only one NTFS partition, select it automatically, otherwise ask
    count=$(echo "$partitions" | wc -w)
    selected_part=""
    
    if [ "$count" -eq 1 ]; then
        selected_part="$partitions"
        echo "[+] Nur eine Partition gefunden. Wähle automatisch: /dev/$selected_part"
    else
        echo "Bitte wähle die Windows-Systempartition (meistens die größte NTFS-Partition):"
        select p in $partitions; do
            if [ -n "$p" ]; then
                selected_part="$p"
                break
            else
                echo "Ungültige Auswahl."
            fi
        done
    fi

    dev="/dev/$selected_part"
    mountpoint="/mnt/win_repair"
    
    sudo mkdir -p "$mountpoint"
    sudo umount "$dev" 2>/dev/null || true
    
    echo ""
    echo "[+] Mounte $dev nach $mountpoint..."
    # Mount using ntfs-3g with rw and force options in case the filesystem is dirty
    if sudo mount -t ntfs-3g -o rw,force "$dev" "$mountpoint"; then
        # Search for SAM hive case-insensitively
        echo "[+] Suche nach der SAM-Registrierungsdatenbank..."
        sam_path=$(find "$mountpoint" -ipath "*/Windows/System32/config/SAM" | head -n 1 || true)
        
        if [ -n "$sam_path" ] && [ -f "$sam_path" ]; then
            echo "[SUCCESS] SAM-Datenbank gefunden: $sam_path"
            echo ""
            echo "=========================================================="
            echo "  Öffne interaktiven Passwort-Editor (chntpw)..."
            echo "  Befolge die Anweisungen auf dem Bildschirm:"
            echo "  - Wähle '1' um Benutzerpasswörter zu bearbeiten/löschen"
            echo "  - Gib den Benutzernamen oder die RID des Kontos ein"
            echo "  - Drücke '1' um das Passwort zu löschen (blank)"
            echo "  - Drücke 'q' gefolgt von 'y' zum Speichern und Beenden!"
            echo "=========================================================="
            echo ""
            
            # Run chntpw interactively
            sudo chntpw -i "$sam_path"
            
            echo ""
            echo "[+] Trenne Laufwerk $dev..."
            sudo umount "$mountpoint"
            echo "[SUCCESS] Vorgang beendet. Du kannst den PC nun neu starten."
        else
            echo "[!] SAM-Registrierungsdatenbank wurde nicht gefunden."
            echo "    Ist dies die richtige Windows-Systempartition?"
            sudo umount "$mountpoint"
        fi
    else
        echo "[!] Fehler beim Mounten von $dev."
        echo "    Bitte führe zuerst die 'NTFS-Dateisystem-Reparatur' aus,"
        echo "    um eventuelle Sperren (Dirty Flags) aufzulösen."
    fi
    echo ""
    read -p "Drücke Enter zum Beenden..."
fi
