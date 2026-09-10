cmake_minimum_required(VERSION 3.15)
project(recovery LANGUAGES CXX)

option(RECOVERY_ENABLE_TSK "Build Recovery with bundled TSK support" ON)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    RESULT_VARIABLE PYBIND11_CMAKEDIR_RESULT
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE PYBIND11_CMAKEDIR_ERROR)
if(NOT PYBIND11_CMAKEDIR_RESULT EQUAL 0 OR pybind11_DIR STREQUAL "")
    message(FATAL_ERROR
        "Could not locate pybind11 using ${Python3_EXECUTABLE}: "
        "${PYBIND11_CMAKEDIR_ERROR}")
endif()
find_package(pybind11 CONFIG REQUIRED)

if(RECOVERY_ENABLE_TSK)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sleuthkit
                     ${CMAKE_CURRENT_BINARY_DIR}/sleuthkit)
endif()

set(RECOVERY_SOURCES
    recover.cpp
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

if(MSVC)
    target_compile_definitions(recovery PRIVATE NOMINMAX)
endif()

add_executable(recover recover_main.cpp)
target_compile_features(recover PRIVATE cxx_std_17)
target_link_libraries(recover PRIVATE recovery)

pybind11_add_module(recovery_python bindings_recovery.cpp)
set_target_properties(recovery_python PROPERTIES OUTPUT_NAME recovery)
if(MSVC)
    set_target_properties(recovery_python PROPERTIES ARCHIVE_OUTPUT_NAME recovery_python)
endif()

if(RECOVERY_ENABLE_TSK)
    target_link_libraries(recovery PUBLIC TSK::tsk)
    target_link_libraries(recovery_python PRIVATE recovery TSK::tsk)
    target_compile_definitions(recovery PUBLIC RECOVERY_ENABLE_LIBTSK=1)
else()
    target_link_libraries(recovery_python PRIVATE recovery)
    target_compile_definitions(recovery PUBLIC RECOVERY_DISABLE_LIBTSK=1)
endif()
