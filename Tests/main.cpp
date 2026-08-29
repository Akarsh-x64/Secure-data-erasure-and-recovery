#include <iostream>
#include <string>

// Include our Engine Layers
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "   Secure Erasure Engine - VBR Test       \n";
    std::cout << "==========================================\n\n";

    std::cout << "Path Formats for Windows:\n";
    std::cout << "  - For a physical drive (e.g. Drive 2): \\\\.\\PhysicalDrive2\n";
    std::cout << "  - For a mounted volume (e.g. E: drive): \\\\.\\E:\n\n";
    
    std::cout << "Enter the path to your exFAT virtual disk: ";
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

    // 3. Create the File System Layer and pass it the Hardware Demolition expert
    Erasure::FileSystems::ExFatDriver exFatFs(&hardware);

    std::cout << "\nAttempting to Mount exFAT and read Sector 0 (VBR)...\n";
    if (!exFatFs.Mount()) {
        std::cout << "[ERROR] Failed to mount. Ensure the drive is formatted exactly as exFAT.\n";
        return 1;
    }

    // 4. Print the success!
    std::cout << "[SUCCESS] Valid exFAT Volume Boot Record found!\n";
    exFatFs.PrintVBRInfo();

    std::cout << "\nEnter the exact filename to securely delete (e.g., target.txt)\n";
    std::cout << "Or type 'WIPE' to obliterate the entire volume: ";
    std::string filename;
    std::getline(std::cin, filename);

    if (filename.empty()) {
        std::cout << "No filename provided. Exiting.\n";
        return 0;
    }

    if (filename == "WIPE") {
        std::cout << "\nWARNING: Initiating Full Volume Wipe!\n";
        if (exFatFs.WipeVolume()) {
            std::cout << "[SUCCESS] The drive is now an empty wasteland.\n";
        } else {
            std::cout << "[FAILED] WipeVolume failed.\n";
        }
    } else {
        std::cout << "\nNow attempting to securely delete '" << filename << "'...\n";
        if (exFatFs.EraseFile(filename)) {
            std::cout << "[SUCCESS] " << filename << " was completely obliterated from the drive!\n";
        } else {
            std::cout << "[FAILED] Could not delete " << filename << "\n";
        }
    }

    return 0;
}
