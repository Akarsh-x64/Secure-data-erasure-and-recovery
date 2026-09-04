#include <iostream>
#include <string>
#include <memory>
#include <algorithm>

// Include our Engine Layers
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/File Systems/ext4/ext4.h"
#include "../Erasure/File Systems/XFS/XFS.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "   Secure Erasure Engine - Interactive    \n";
    std::cout << "==========================================\n\n";

    std::cout << "Path Formats for Windows:\n";
    std::cout << "  - For a physical drive (e.g. Drive 2): \\\\.\\PhysicalDrive2\n";
    std::cout << "  - For a mounted volume (e.g. E: drive): \\\\.\\E:\n\n";

    std::cout << "Enter the path to your target device / volume: ";
    std::string path;
    std::getline(std::cin, path);

    if (path.empty()) {
        std::cout << "Invalid path.\n";
        return 1;
    }

    // 1. Create the OS Layer
    Erasure::OS::WindowsStorageDevice osDevice;

    std::cout << "\nAttempting to open handle to: " << path << " ...\n";
    if (!osDevice.Open(path)) {
        std::cout << "[ERROR] Failed to open device! (Did you forget Administrator privileges or misspell the path?)\n";
        return 1;
    }
    std::cout << "[SUCCESS] Handle opened! Sector Size: " << osDevice.GetGeometry().bytesPerSector << " bytes.\n";

    // 2. Create the Hardware Layer and pass it the OS pipe
    Erasure::Hardware::HDDController hardware(&osDevice);

    // AGGRESSIVELY LOCK AND DISMOUNT THE VOLUME
    // If we don't do this, Windows OS will aggressively cache the file in RAM,
    // intercept our writes, and possibly even overwrite our erased sectors with its cached copy!
    std::cout << "\nAttempting to lock and dismount volume to bypass Windows Cache...\n";
    if (osDevice.LockVolume()) {
        std::cout << "  -> Volume Locked!\n";
    } else {
        std::cout << "  -> WARNING: Could not lock volume (It might be in use, or not a mounted partition).\n";
    }

    if (osDevice.DismountVolume()) {
        std::cout << "  -> Volume Dismounted! Windows Cache dropped.\n";
    }

    // Select Filesystem
    std::cout << "\nSelect Filesystem Driver:\n";
    std::cout << "  [1] exFAT\n";
    std::cout << "  [2] ext4\n";
    std::cout << "  [3] XFS\n";
    std::cout << "Enter choice (default 1): ";
    std::string choice;
    std::getline(std::cin, choice);

    std::string normChoice = choice;
    std::transform(normChoice.begin(), normChoice.end(), normChoice.begin(), ::tolower);

    std::unique_ptr<Erasure::Core::IFileSystemDriver> fsDriver;
    if (normChoice == "2" || normChoice == "ext4" || normChoice == "ext") {
        auto ext4 = std::make_unique<Erasure::FileSystems::Ext4Driver>(&hardware);
        std::cout << "\nAttempting to Mount ext4 and parse Superblock...\n";
        if (!ext4->Mount()) {
            std::cout << "[ERROR] Failed to mount. Ensure the target contains a valid ext4 filesystem.\n";
            return 1;
        }
        std::cout << "[SUCCESS] Valid ext4 filesystem found!\n";
        ext4->PrintSuperblockInfo();
        fsDriver = std::move(ext4);
    } else if (normChoice == "3" || normChoice == "xfs") {
        auto xfs = std::make_unique<Erasure::FileSystems::XfsDriver>(&hardware);
        std::cout << "\nAttempting to Mount XFS and parse Superblock...\n";
        if (!xfs->Mount()) {
            std::cout << "[ERROR] Failed to mount. Ensure the target contains a valid XFS filesystem.\n";
            return 1;
        }
        std::cout << "[SUCCESS] Valid XFS filesystem found!\n";
        xfs->PrintSuperblockInfo();
        fsDriver = std::move(xfs);
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

    // 4. Interactive Erasure Command Loop
    std::cout << "\n============================================================\n";
    std::cout << "           FILESYSTEM DRIVER READY FOR ERASURE              \n";
    std::cout << "============================================================\n";

    while (true) {
        std::cout << "\nCommands:\n";
        std::cout << "  - Enter relative file path (e.g. secret.txt or docs/data.pdf)\n";
        std::cout << "  - Type 'WIPE' to execute surgical volume-wide sanitization\n";
        std::cout << "  - Type 'EXIT' or 'QUIT' to close the device and exit\n";
        std::cout << "Action: ";

        std::string command;
        if (!std::getline(std::cin, command) || command.empty()) {
            std::cout << "No action provided. Exiting session.\n";
            break;
        }

        // Trim leading and trailing whitespace
        while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) command.erase(command.begin());
        while (!command.empty() && (command.back() == ' ' || command.back() == '\t' || command.back() == '\r')) command.pop_back();

        if (command == "EXIT" || command == "exit" || command == "QUIT" || command == "quit") {
            std::cout << "\nClosing device handle and terminating session. Goodbye!\n";
            break;
        }

        if (command == "WIPE" || command == "wipe") {
            std::cout << "\n************************************************************\n";
            std::cout << "  CRITICAL WARNING: FULL SURGICAL VOLUME WIPE REQUESTED!\n";
            std::cout << "  This will overwrite all user data blocks across the disk.\n";
            std::cout << "************************************************************\n";
            std::cout << "Type 'YES' to confirm full volume destruction: ";
            std::string confirm;
            std::getline(std::cin, confirm);

            if (confirm != "YES") {
                std::cout << "  -> Volume wipe aborted by user.\n";
                continue;
            }

            std::cout << "\nExecuting Surgical Volume Wipe...\n";
            if (fsDriver->WipeVolume()) {
                std::cout << "[SUCCESS] Volume wiped cleanly while preserving core filesystem structures.\n";
            } else {
                std::cout << "[FAILED] WipeVolume failed.\n";
            }
        } else {
            std::cout << "\nAttempting to securely delete '" << command << "'...\n";
            if (fsDriver->EraseFile(command)) {
                std::cout << "[SUCCESS] '" << command << "' was completely obliterated from the drive!\n";
            } else {
                std::cout << "[FAILED] Could not securely delete '" << command << "'.\n";
            }
        }
    }

    return 0;
}
