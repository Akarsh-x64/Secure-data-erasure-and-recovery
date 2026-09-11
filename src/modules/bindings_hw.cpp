#include <pybind11/pybind11.h>
#include "../Erasure/Hardware/Magnetic/HDDController.h"

#ifdef _WIN32
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
using TargetOSDevice = Erasure::OS::WindowsStorageDevice;
#elif defined(__linux__)
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
using TargetOSDevice = Erasure::OS::LinuxStorageDevice;
#endif

namespace py = pybind11;

PYBIND11_MODULE(hdd, m) {
    py::class_<Erasure::Hardware::HDDController>(m, "HDDController")
        .def(py::init<TargetOSDevice*>(), py::arg("device"));
}