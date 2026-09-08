cmake_minimum_required(VERSION 3.15)
project(secure_erasure_core)

set(CMAKE_CXX_STANDARD 17)

execute_process(
  COMMAND python3 -m pybind11 --cmakedir
  OUTPUT_VARIABLE pybind11_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 REQUIRED)

pybind11_add_module(core 
    bindings.cpp
    ../Erasure/OS/Linux/LinuxStorageDevice.cpp
    ../Erasure/Hardware/Magnetic/HDDController.cpp
    "../Erasure/File Systems/exFAT/exFAT.cpp"
    "../Erasure/File Systems/ext4/ext4.cpp"
)