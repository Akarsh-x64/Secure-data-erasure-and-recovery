// #include <pybind11/pybind11.h>
// #include "../Erasure/OS/Linux/LinuxStorageDevice.h"

// namespace py = pybind11;

// PYBIND11_MODULE(osdevice, m) {
//     py::class_<Erasure::OS::LinuxStorageDevice>(m, "LinuxStorageDevice")
//         .def(py::init<>())
//         .def("Open", &Erasure::OS::LinuxStorageDevice::Open)
//         .def("Close", &Erasure::OS::LinuxStorageDevice::Close)
//         .def("LockVolume", &Erasure::OS::LinuxStorageDevice::LockVolume)
//         .def("DismountVolume", &Erasure::OS::LinuxStorageDevice::DismountVolume);
// }

#include <pybind11/pybind11.h>

#ifdef _WIN32
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
#elif defined(__linux__)
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
#endif

namespace py = pybind11;

PYBIND11_MODULE(osdevice, m) {
#ifdef _WIN32
    py::class_<Erasure::OS::WindowsStorageDevice>(m, "WindowsStorageDevice")
        .def(py::init<>())
        .def("Open", &Erasure::OS::WindowsStorageDevice::Open, "Open handle to Windows device")
        .def("LockVolume", &Erasure::OS::WindowsStorageDevice::LockVolume, "Lock Windows volume")
        .def("DismountVolume", &Erasure::OS::WindowsStorageDevice::DismountVolume, "Dismount Windows volume")
        .def("GetGeometry", &Erasure::OS::WindowsStorageDevice::GetGeometry, "Get drive geometry details");
#elif defined(__linux__)
    py::class_<Erasure::OS::LinuxStorageDevice>(m, "LinuxStorageDevice")
        .def(py::init<>())
        .def("Open", &Erasure::OS::LinuxStorageDevice::Open, "Open handle to device path")
        .def("LockVolume", &Erasure::OS::LinuxStorageDevice::LockVolume, "Lock the volume")
        .def("DismountVolume", &Erasure::OS::LinuxStorageDevice::DismountVolume, "Dismount the volume")
        .def("Close", &Erasure::OS::LinuxStorageDevice::Close, "Close device handle");
#endif
}