#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>

// Include our Engine Layers
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/File Systems/ext4/ext4.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "    Secure Erasure Engine - Interactive     \n";
    std::cout << "==========================================\n\n";

    std::cout << "Path Formats for Linux:\n";
    std::cout << "  - For a physical drive: /dev/nvme0n1 or /dev/sda\n";
    std::cout << "  - For a partition: /dev/sda1\n";

    std::cout << "Enter the path to your target device / volume: ";
    std::string path;
    std::getline(std::cin, path);

    if (path.empty()) {
        std::cout << "Invalid path.\n";
        return 1;
    }

    // 1. Create the OS Layer
    Erasure::OS::LinuxStorageDevice osDevice;

    std::cout << "\nAttempting to open handle to: " << path << " ...\n";
    if (!osDevice.Open(path)) {
        std::cout << "[ERROR] Failed to open device! (Did you forget Administrator privileges or misspell the path?)\n";
        return 1;
    }
    std::cout << "[SUCCESS] Handle opened! Sector Size: " << osDevice.GetGeometry().bytesPerSector << " bytes.\n";

    // 2. Create the Hardware Layer and pass it the OS pipe
    Erasure::Hardware::HDDController hardware(&osDevice);

    // AGGRESSIVELY LOCK AND DISMOUNT THE VOLUME
    std::cout << "\nAttempting to lock and dismount volume to bypass OS Cache...\n";
    if (osDevice.LockVolume()) {
        std::cout << "  -> Volume Locked!\n";
    } else {
        std::cout << "  -> WARNING: Could not lock volume (It might be in use, or not a mounted partition).\n";
    }

    if (osDevice.DismountVolume()) {
        std::cout << "  -> Volume Dismounted! OS Cache dropped.\n";
    }

    // Select Filesystem
    std::cout << "\nSelect Filesystem Driver:\n";
    std::cout << "  [1] exFAT\n";
    std::cout << "  [2] ext4\n";
    std::cout << "Enter choice (default 1): ";
    std::string choice;
    std::getline(std::cin, choice);

    std::unique_ptr<Erasure::Core::IFileSystemDriver> fsDriver;
    if (choice == "2") {
        auto ext4 = std::make_unique<Erasure::FileSystems::Ext4Driver>(&hardware);
        std::cout << "\nAttempting to Mount ext4 and parse Superblock...\n";
        if (!ext4->Mount()) {
            std::cout << "[ERROR] Failed to mount. Ensure the target contains a valid ext4 filesystem.\n";
            return 1;
        }
        std::cout << "[SUCCESS] Valid ext4 filesystem found!\n";
        ext4->PrintSuperblockInfo();
        fsDriver = std::move(ext4);
    } else {
        auto exFat = std::make_unique<Erasure::FileSystems::ExFatDriver>(&hardware);
        std::cout << "\nAttempting to Mount exFAT and read Sector 0 (VBR)...\n";
        if (!exFat->Mount()) {
            std::cout << "[ERROR] Failed to mount. Ensure the drive is formatted exactly as exFAT.\n";
            return 1;
        }
        std::cout << "[SUCCESS] Valid exFAT Volume Boot Record found!\n";
        exFat->PrintVBRInfo();
        fsDriver = std::move(exFat);
    }

    std::cout << "\nEnter the exact relative path to securely delete (e.g. target.txt or docs/secret.pdf)\n";
    std::cout << "Or type 'WIPE' to obliterate the entire volume: ";
    std::string filename;
    std::getline(std::cin, filename);

    if (filename.empty()) {
        std::cout << "No filename provided. Exiting.\n";
        return 0;
    }

    bool success = false;
    if (filename == "WIPE") {
        std::cout << "\nWARNING: Initiating Full Volume Wipe!\n";
        success = fsDriver->WipeVolume();
        if (success) {
            std::cout << "[SUCCESS] Volume wiped cleanly while preserving core filesystem structures.\n";
        } else {
            std::cout << "[FAILED] WipeVolume failed.\n";
        }
    } else {
        std::cout << "\nNow attempting to securely delete '" << filename << "'...\n";
        success = fsDriver->EraseFile(filename);
        if (success) {
            std::cout << "[SUCCESS] '" << filename << "' was completely obliterated from the drive!\n";
        } else {
            std::cout << "[FAILED] Could not delete " << filename << "\n";
        }
    }

    if (success) {
        std::cout << "[System] Triggering native OS filesystem consistency check...\n";
        osDevice.Close();

        std::string repairCommand;
        if (choice == "2") {
            repairCommand = "sudo ../_externals/e2fsck -f -y " + path;
        } else {
            repairCommand = "sudo ../_externals/fsck.exfat -y " + path;
        }

        int result = std::system(repairCommand.c_str());
        if (result == 0) {
            std::cout << "[SUCCESS] Native OS metadata check completed cleanly. Filesystem is fully consistent.\n";
        } else {
            std::cout << "[WARNING] Filesystem check completed with exit code: " << result << "\n";
        }
    }

    return 0;
}