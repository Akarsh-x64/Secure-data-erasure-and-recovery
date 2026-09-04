#include <iostream>
#include <string>
#include <memory>

// Include Engine Layers
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/Core/IFileSystemDriver.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/File Systems/ext3/ext3.h"
#include "../Erasure/File Systems/ext2/ext2.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "   Secure Erasure Engine - Test Runner    \n";
    std::cout << "==========================================\n\n";

    std::cout << "Path Formats for Windows:\n";
    std::cout << "  - For a physical drive (e.g. Drive 1): \\\\.\\PhysicalDrive1\n";
    std::cout << "  - For a mounted volume (e.g. E: drive): \\\\.\\E:\n\n";

    std::cout << "Enter target path: ";
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
    // Bypasses OS caching
    std::cout << "\nAttempting to lock and dismount volume to bypass Windows Cache...\n";
    if (osDevice.LockVolume()) {
        std::cout << "  -> Volume Locked!\n";
    } else {
        std::cout << "  -> WARNING: Could not lock volume (It might be in use, or not a mounted partition).\n";
    }

    if (osDevice.DismountVolume()) {
        std::cout << "  -> Volume Dismounted! Windows Cache dropped.\n";
    }

    // 3. Select Filesystem Driver
    std::cout << "\nSelect Filesystem Driver:\n";
    std::cout << "  [1] exFAT\n";
    std::cout << "  [2] Ext3\n";
    std::cout << "  [3] Ext2\n";
    std::cout << "Enter choice (1, 2, or 3): ";
    std::string choice;
    std::getline(std::cin, choice);

    std::unique_ptr<Erasure::Core::IFileSystemDriver> fsDriver;

    if (choice == "2") {
        auto ext3Fs = std::make_unique<Erasure::FileSystems::Ext3Driver>(&hardware);

        std::cout << "\nAttempting to Mount Ext3 and read SuperBlock...\n";
        if (!ext3Fs->Mount()) {
            std::cout << "[ERROR] Failed to mount Ext3. Ensure the drive is formatted as Ext3.\n";
            return 1;
        }

        std::cout << "[SUCCESS] Valid Ext3 filesystem found!\n";
        ext3Fs->PrintSuperBlockInfo();
        fsDriver = std::move(ext3Fs);
    } else if (choice == "3") {
        auto ext2Fs = std::make_unique<Erasure::FileSystems::Ext2Driver>(&hardware);

        std::cout << "\nAttempting to Mount Ext2 and read SuperBlock...\n";
        if (!ext2Fs->Mount()) {
            std::cout << "[ERROR] Failed to mount Ext2. Ensure the drive is formatted as Ext2.\n";
            return 1;
        }

        std::cout << "[SUCCESS] Valid Ext2 filesystem found!\n";
        ext2Fs->PrintSuperBlockInfo();
        fsDriver = std::move(ext2Fs);
    } else {
        auto exFatFs = std::make_unique<Erasure::FileSystems::ExFatDriver>(&hardware);

        std::cout << "\nAttempting to Mount exFAT and read Sector 0 (VBR)...\n";
        if (!exFatFs->Mount()) {
            std::cout << "[ERROR] Failed to mount. Ensure the drive is formatted exactly as exFAT.\n";
            return 1;
        }

        std::cout << "[SUCCESS] Valid exFAT Volume Boot Record found!\n";
        exFatFs->PrintVBRInfo();
        fsDriver = std::move(exFatFs);
    }

    // 4. File Erasure or Full Wipe
    std::cout << "\nEnter the relative path/filename to securely delete (e.g. target.txt or folder/data.bin)\n";
    std::cout << "Or type 'WIPE' to obliterate the entire volume: ";
    std::string filename;
    std::getline(std::cin, filename);

    if (filename.empty()) {
        std::cout << "No filename provided. Exiting.\n";
        return 0;
    }

    if (filename == "WIPE") {
        std::cout << "\nWARNING: Initiating Full Volume Wipe!\n";
        if (fsDriver->WipeVolume()) {
            std::cout << "[SUCCESS] The drive is now an empty wasteland.\n";
        } else {
            std::cout << "[FAILED] WipeVolume failed.\n";
        }
    } else {
        std::cout << "\nNow attempting to securely delete '" << filename << "'...\n";
        if (fsDriver->EraseFile(filename)) {
            std::cout << "[SUCCESS] '" << filename << "' was completely obliterated from the drive!\n";
        } else {
            std::cout << "[FAILED] Could not delete '" << filename << "'\n";
        }
    }

    return 0;
}
