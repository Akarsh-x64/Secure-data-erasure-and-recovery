#include <pybind11/pybind11.h>
#include <memory>

#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/exFAT/exFAT.h"
#include "../Erasure/File Systems/ext4/ext4.h"

namespace py = pybind11;

PYBIND11_MODULE(secure_erasure_core, m) {
    py::class_<Erasure::OS::LinuxStorageDevice>(m, "LinuxStorageDevice")
        .def(py::init<>())
        .def("Open", &Erasure::OS::LinuxStorageDevice::Open)
        .def("Close", &Erasure::OS::LinuxStorageDevice::Close)
        .def("LockVolume", &Erasure::OS::LinuxStorageDevice::LockVolume)
        .def("DismountVolume", &Erasure::OS::LinuxStorageDevice::DismountVolume);

    py::class_<Erasure::Hardware::HDDController>(m, "HDDController")
        .def(py::init<Erasure::OS::LinuxStorageDevice*>());

    py::class_<Erasure::FileSystems::Ext4Driver>(m, "Ext4Driver")
        .def(py::init<Erasure::Hardware::HDDController*>())
        .def("Mount", &Erasure::FileSystems::Ext4Driver::Mount)
        .def("PrintSuperblockInfo", &Erasure::FileSystems::Ext4Driver::PrintSuperblockInfo)
        .def("WipeVolume", &Erasure::FileSystems::Ext4Driver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::Ext4Driver::EraseFile);

    py::class_<Erasure::FileSystems::ExFatDriver>(m, "ExFatDriver")
        .def(py::init<Erasure::Hardware::HDDController*>())
        .def("Mount", &Erasure::FileSystems::ExFatDriver::Mount)
        .def("PrintVBRInfo", &Erasure::FileSystems::ExFatDriver::PrintVBRInfo)
        .def("WipeVolume", &Erasure::FileSystems::ExFatDriver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::ExFatDriver::EraseFile);
}
