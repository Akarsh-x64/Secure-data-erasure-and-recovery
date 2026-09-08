cmake_minimum_required(VERSION 3.15)
project(recovery LANGUAGES CXX)

option(RECOVERY_ENABLE_TSK "Build Recovery with explicit libtsk support" OFF)

set(RECOVERY_SOURCES
    Acquisition/LinuxReadOnlyStorage.cpp
    Carving/Core/RecoveryOrchestrator.cpp
    Carving/PhotoRec/PhotoRecCarver.cpp
    Carving/PhotoRec/PhotoRecProcessRunner.cpp
    Carving/Verification/IsoBmffVerifier.cpp
    Carving/Verification/JpegVerifier.cpp
    Carving/Verification/OoxmlVerifier.cpp
    Carving/Verification/PdfVerifier.cpp
    Carving/Verification/PngVerifier.cpp
    Carving/Verification/Sha256.cpp
    Carving/Verification/VerificationEngine.cpp
    Carving/Verification/ZipVerifier.cpp
    Filesystems/Ext4Detector.cpp
    Filesystems/FilesystemDetector.cpp
    Filesystems/NTFS/DataRunParser.cpp
    Filesystems/NTFS/MFTParser.cpp
    Filesystems/NTFS/NTFSFileSystem.cpp
    Metadata/MetadataStore.cpp
    Partitions/GPTParser.cpp
    Partitions/MBRParser.cpp
    TSK/TskFileSystem.cpp
    TSK/TskImageBridge.cpp
)

if(WIN32)
    list(REMOVE_ITEM RECOVERY_SOURCES Acquisition/LinuxReadOnlyStorage.cpp)
    list(APPEND RECOVERY_SOURCES Acquisition/WindowsReadOnlyStorage.cpp)
endif()

add_library(recovery STATIC ${RECOVERY_SOURCES})
target_compile_features(recovery PUBLIC cxx_std_17)
target_include_directories(recovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(RECOVERY_ENABLE_TSK)
    find_path(RECOVERY_TSK_INCLUDE_DIR NAMES tsk/libtsk.h REQUIRED)
    find_library(RECOVERY_TSK_LIBRARY NAMES tsk tsk3 REQUIRED)
    target_include_directories(recovery PRIVATE ${RECOVERY_TSK_INCLUDE_DIR})
    target_link_libraries(recovery PUBLIC ${RECOVERY_TSK_LIBRARY})
    target_compile_definitions(recovery PUBLIC RECOVERY_ENABLE_LIBTSK=1)
else()
    target_compile_definitions(recovery PUBLIC RECOVERY_DISABLE_LIBTSK=1)
endif()
