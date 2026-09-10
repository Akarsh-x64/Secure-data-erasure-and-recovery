cmake_minimum_required(VERSION 3.15)
project(recovery LANGUAGES CXX)

option(RECOVERY_ENABLE_TSK "Build Recovery with bundled TSK support" ON)

if(RECOVERY_ENABLE_TSK)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sleuthkit
                     ${CMAKE_CURRENT_BINARY_DIR}/sleuthkit)
endif()

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
    Audit/EvidenceManifest.cpp
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
set_target_properties(recovery PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(recover recover.cpp)
target_compile_features(recover PRIVATE cxx_std_17)
target_link_libraries(recover PRIVATE recovery)

if(RECOVERY_ENABLE_TSK)
    target_link_libraries(recovery PUBLIC TSK::tsk)
    target_compile_definitions(recovery PUBLIC RECOVERY_ENABLE_LIBTSK=1)
else()
    target_compile_definitions(recovery PUBLIC RECOVERY_DISABLE_LIBTSK=1)
endif()
