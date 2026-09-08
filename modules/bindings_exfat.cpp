#include <pybind11/pybind11.h>
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"

namespace py = pybind11;

PYBIND11_MODULE(exfat, m) {
    py::class_<Erasure::FileSystems::ExFatDriver>(m, "ExFatDriver")
        .def(py::init<Erasure::Hardware::HDDController*>())
        .def("Mount", &Erasure::FileSystems::ExFatDriver::Mount)
        .def("PrintVBRInfo", &Erasure::FileSystems::ExFatDriver::PrintVBRInfo)
        .def("WipeVolume", &Erasure::FileSystems::ExFatDriver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::ExFatDriver::EraseFile);
}