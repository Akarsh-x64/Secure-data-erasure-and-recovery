#include <pybind11/pybind11.h>
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"

namespace py = pybind11;

PYBIND11_MODULE(hdd, m) {
    py::class_<Erasure::Hardware::HDDController>(m, "HDDController")
        .def(py::init<Erasure::OS::LinuxStorageDevice*>());
}