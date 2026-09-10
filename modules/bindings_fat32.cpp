#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "../Erasure/File Systems/FAT32/FAT32.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"

namespace py = pybind11;

PYBIND11_MODULE(fat32, m) {
    m.doc() = "Python bindings for FAT32 Secure Deletion & Formatting Engine";

    py::class_<Erasure::FileSystems::Fat32Driver>(m, "Fat32Driver")
        .def(py::init<Erasure::Hardware::HDDController*>(), py::arg("hardware"))
        .def("Mount", &Erasure::FileSystems::Fat32Driver::Mount)
        .def("PrintBootInfo", &Erasure::FileSystems::Fat32Driver::PrintBootInfo)
        .def("WipeVolume", &Erasure::FileSystems::Fat32Driver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::Fat32Driver::EraseFile, py::arg("relativePath"))
        .def("EraseDirectory", &Erasure::FileSystems::Fat32Driver::EraseDirectory, py::arg("relativePath"))
        .def("FormatDrive", &Erasure::FileSystems::Fat32Driver::FormatDrive, py::arg("fullDriveSanitize") = false)
        .def("GetBytesPerSector", &Erasure::FileSystems::Fat32Driver::GetBytesPerSector)
        .def("GetBytesPerCluster", &Erasure::FileSystems::Fat32Driver::GetBytesPerCluster)
        .def("GetFirstDataSector", &Erasure::FileSystems::Fat32Driver::GetFirstDataSector)
        .def("GetRootCluster", &Erasure::FileSystems::Fat32Driver::GetRootCluster)
        .def("GetTotalClusters", &Erasure::FileSystems::Fat32Driver::GetTotalClusters);
}
