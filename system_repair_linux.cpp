#include "system_repair.h"
#include <cstdlib>
#include <string>

bool SystemRepair::RunRepair(const std::wstring& key, std::wstring& outDescription) {
    std::string cmd = "";

    if (key == L"sfc" || key == L"fsck") {
        cmd = "x-terminal-emulator -e 'sudo fsck -f /dev/sda1 || sudo fsck -f /dev/nvme0n1p1' &";
        outDescription = L"Linux Filesystem Check (fsck)";
    } else if (key == L"ntfsfix") {
        cmd = "x-terminal-emulator -e 'sudo ntfsfix /dev/sda2 || sudo ntfsfix /dev/nvme0n1p2' &";
        outDescription = L"NTFS Partition Quick Fix (ntfsfix)";
    } else if (key == L"grub_repair") {
        cmd = "x-terminal-emulator -e 'echo Running GRUB bootloader repair... && sudo update-grub' &";
        outDescription = L"GRUB Bootloader Repair (update-grub)";
    } else if (key == L"chkdsk" || key == L"badblocks") {
        cmd = "x-terminal-emulator -e 'sudo badblocks -sv /dev/sda || sudo badblocks -sv /dev/nvme0n1' &";
        outDescription = L"Disk Surface & Bad Blocks Scan (badblocks)";
    } else {
        outDescription = L"Unbekannte Linux-Reparaturfunktion";
        return false;
    }

    int res = std::system(cmd.c_str());
    return (res == 0);
}
