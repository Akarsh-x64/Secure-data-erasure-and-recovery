#include "../../Acquisition/LinuxReadOnlyStorage.h"

#include <iomanip>
#include <iostream>
#include <vector>

int main()
{
    Recovery::Acquisition::LinuxReadOnlyStorage storage;

    if (!storage.Open("./test.img"))    
    {
        std::cerr << "Failed to open test image\n";
        return 1;
    }

    std::cout << "Size: "
              << storage.GetSize()
              << " bytes\n";

    std::cout << "Sector size: "
              << storage.GetSectorSize()
              << " bytes\n";

    std::vector<uint8_t> buffer(32);

    if (!storage.Read(4096, buffer.size(), buffer.data()))
    {
        std::cerr << "Read failed\n";
        return 1;
    }

    std::cout << "Data at offset 4096:\n";

    for (uint8_t byte : buffer)
    {
        std::cout
            << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(byte)
            << ' ';
    }

    std::cout << '\n';

    storage.Close();

    return 0;
}