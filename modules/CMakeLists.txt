cmake_minimum_required(VERSION 3.15)
project(secure_erasure_core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

    execute_process(
      COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
      OUTPUT_VARIABLE pybind11_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    find_package(pybind11 REQUIRED)
elseif(UNIX)
    
    execute_process(
      COMMAND python3 -m pybind11 --cmakedir
      OUTPUT_VARIABLE pybind11_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    find_package(pybind11 REQUIRED)
endif()

# 1. Compile underlying C++ code into STATIC libraries
if(WIN32)
    add_library(osdevice_cpp STATIC ../Erasure/OS/Windows/WindowsStorageDevice.cpp)
elseif(UNIX)
    add_library(osdevice_cpp STATIC ../Erasure/OS/Linux/LinuxStorageDevice.cpp)
endif()

add_library(hdd_cpp STATIC ../Erasure/Hardware/Magnetic/HDDController.cpp)
target_link_libraries(hdd_cpp PRIVATE osdevice_cpp)

add_library(exfat_cpp STATIC "../Erasure/File Systems/exFAT/exFAT.cpp")
target_link_libraries(exfat_cpp PRIVATE hdd_cpp osdevice_cpp)

add_library(ext4_cpp STATIC "../Erasure/File Systems/ext4/ext4.cpp")
target_link_libraries(ext4_cpp PRIVATE hdd_cpp osdevice_cpp)

add_library(ntfs_cpp STATIC "../Erasure/File Systems/NTFS/NTFS.cpp")
target_link_libraries(ntfs_cpp PRIVATE hdd_cpp osdevice_cpp)

add_library(fat32_cpp STATIC "../Erasure/File Systems/FAT32/FAT32.cpp")
target_link_libraries(fat32_cpp PRIVATE hdd_cpp osdevice_cpp)

add_library(verification_cpp STATIC 
    "../Erasure/Verification/VerificationEngine.cpp"
    "../Erasure/Verification/StatisticalTests.cpp"
    "../Erasure/Verification/SignatureCarver.cpp"
    "../Erasure/Verification/VerificationReport.cpp"
)
target_link_libraries(verification_cpp PRIVATE hdd_cpp osdevice_cpp)

# 2. Expose them to Python using pybind11 modules and link the static libraries
pybind11_add_module(osdevice bindings_os.cpp)
target_link_libraries(osdevice PRIVATE osdevice_cpp)

pybind11_add_module(hdd bindings_hw.cpp)
target_link_libraries(hdd PRIVATE hdd_cpp osdevice_cpp)

pybind11_add_module(exfat bindings_exfat.cpp)
target_link_libraries(exfat PRIVATE exfat_cpp hdd_cpp osdevice_cpp)

pybind11_add_module(ext4 bindings_ext4.cpp)
target_link_libraries(ext4 PRIVATE ext4_cpp hdd_cpp osdevice_cpp)

pybind11_add_module(ntfs bindings_ntfs.cpp)
target_link_libraries(ntfs PRIVATE ntfs_cpp hdd_cpp osdevice_cpp)

pybind11_add_module(fat32 bindings_fat32.cpp)
target_link_libraries(fat32 PRIVATE fat32_cpp hdd_cpp osdevice_cpp)

pybind11_add_module(verification bindings_verification.cpp)
target_link_libraries(verification PRIVATE verification_cpp hdd_cpp osdevice_cpp)