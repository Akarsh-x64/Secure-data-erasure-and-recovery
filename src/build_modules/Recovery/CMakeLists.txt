cmake_minimum_required(VERSION 3.15)
project(RecoveryV2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find pybind11
find_package(pybind11 REQUIRED)

# Recovery v2 static library
add_library(recovery_v2_cpp STATIC
    recover.cpp
)

# Pybind11 Python module
pybind11_add_module(recovery_v2 bindings_recovery.cpp)
target_link_libraries(recovery_v2 PRIVATE recovery_v2_cpp)

# Output directory for the compiled pyd/so
set_target_properties(recovery_v2 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../modules
)
