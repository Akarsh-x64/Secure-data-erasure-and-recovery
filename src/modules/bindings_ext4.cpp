#include <pybind11/pybind11.h>
#include "../Erasure/File Systems/ext4/ext4.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"

namespace py = pybind11;

PYBIND11_MODULE(ext4, m) {
    py::class_<Erasure::FileSystems::Ext4Driver>(m, "Ext4Driver")
        .def(py::init<Erasure::Hardware::HDDController*>())
        .def("Mount", &Erasure::FileSystems::Ext4Driver::Mount)
        .def("PrintSuperblockInfo", &Erasure::FileSystems::Ext4Driver::PrintSuperblockInfo)
        .def("WipeVolume", &Erasure::FileSystems::Ext4Driver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::Ext4Driver::EraseFile);
}