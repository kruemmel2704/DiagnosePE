#include "system_repair.h"
#include <cstdlib>
#include <string>

bool SystemRepair::RunRepair(const std::wstring& key, std::wstring& outDescription) {
    std::string cmd = "";

    if (key == L"fsck") {
        cmd = "xterm -title 'Linux Dateisystem-Check' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'echo Führe Dateisystemprüfung (fsck) aus... && sudo fsck -f /dev/sda1 || sudo fsck -f /dev/nvme0n1p1; echo && read -p \"Drücke Enter zum Schließen...\"' &";
        outDescription = L"Linux Dateisystem-Check (fsck)";
    } else if (key == L"badblocks") {
        cmd = "xterm -title 'Oberflächenprüfung' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'echo Scanne Datenträger nach defekten Blöcken (badblocks)... && sudo badblocks -sv /dev/sda || sudo badblocks -sv /dev/nvme0n1; echo && read -p \"Drücke Enter zum Schließen...\"' &";
        outDescription = L"Oberflächenprüfung (badblocks)";
    } else if (key == L"grub_repair") {
        cmd = "xterm -title 'GRUB Bootloader Reparatur' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'echo Generiere GRUB-Konfiguration neu... && sudo grub-mkconfig -o /boot/grub/grub.cfg; echo && read -p \"Drücke Enter zum Schließen...\"' &";
        outDescription = L"GRUB Bootloader-Reparatur";
    } else if (key == L"ntfsfix") {
        cmd = "xterm -title 'Windows NTFS Reparatur' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'sudo /usr/local/bin/windows_repair.sh ntfsfix' &";
        outDescription = L"Windows NTFS Dateisystem-Reparatur";
    } else if (key == L"chntpw") {
        cmd = "xterm -title 'Windows Passwort Zurücksetzen' -bg '#1a1d24' -fg '#c5cdd8' -fa 'Monospace' -fs 11 -e 'sudo /usr/local/bin/windows_repair.sh chntpw' &";
        outDescription = L"Windows Passwort-Reset (chntpw)";
    } else {
        outDescription = L"Unbekannte Reparaturfunktion";
        return false;
    }

    int res = std::system(cmd.c_str());
    return (res == 0);
}
