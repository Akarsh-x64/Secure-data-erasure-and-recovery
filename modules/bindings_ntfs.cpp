#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "../Erasure/File Systems/NTFS/NTFS.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"

namespace py = pybind11;

PYBIND11_MODULE(ntfs, m) {
    m.doc() = "Python bindings for NTFS Secure Deletion & Formatting Engine";

    py::class_<Erasure::FileSystems::NTFS::NtfsExtent>(m, "NtfsExtent")
        .def_readonly("lcn", &Erasure::FileSystems::NTFS::NtfsExtent::lcn)
        .def_readonly("clusterCount", &Erasure::FileSystems::NTFS::NtfsExtent::clusterCount);

    py::class_<Erasure::FileSystems::NTFS::TargetLocations>(m, "TargetLocations")
        .def(py::init<>())
        .def_readonly("isValid", &Erasure::FileSystems::NTFS::TargetLocations::isValid)
        .def_readonly("isDirectory", &Erasure::FileSystems::NTFS::TargetLocations::isDirectory)
        .def_readonly("isResident", &Erasure::FileSystems::NTFS::TargetLocations::isResident)
        .def_readonly("path", &Erasure::FileSystems::NTFS::TargetLocations::path)
        .def_readonly("mftRecordNum", &Erasure::FileSystems::NTFS::TargetLocations::mftRecordNum)
        .def_readonly("mftSector", &Erasure::FileSystems::NTFS::TargetLocations::mftSector)
        .def_readonly("mftByteOffsetInSector", &Erasure::FileSystems::NTFS::TargetLocations::mftByteOffsetInSector)
        .def_readonly("mftRecordSize", &Erasure::FileSystems::NTFS::TargetLocations::mftRecordSize)
        .def_readonly("dataExtents", &Erasure::FileSystems::NTFS::TargetLocations::dataExtents)
        .def_readonly("fileSize", &Erasure::FileSystems::NTFS::TargetLocations::fileSize)
        .def_readonly("parentDirRecordNum", &Erasure::FileSystems::NTFS::TargetLocations::parentDirRecordNum)
        .def_readonly("parentIndexSector", &Erasure::FileSystems::NTFS::TargetLocations::parentIndexSector)
        .def_readonly("parentIndexByteOffset", &Erasure::FileSystems::NTFS::TargetLocations::parentIndexByteOffset)
        .def_readonly("parentIndexEntrySize", &Erasure::FileSystems::NTFS::TargetLocations::parentIndexEntrySize)
        .def_readonly("bitmapSector", &Erasure::FileSystems::NTFS::TargetLocations::bitmapSector)
        .def_readonly("bitmapByteOffsetInSector", &Erasure::FileSystems::NTFS::TargetLocations::bitmapByteOffsetInSector);

    py::class_<Erasure::FileSystems::NtfsDriver>(m, "NtfsDriver")
        .def(py::init<Erasure::Hardware::HDDController*>(), py::arg("hardware"))
        .def("Mount", &Erasure::FileSystems::NtfsDriver::Mount)
        .def("PrintBootInfo", &Erasure::FileSystems::NtfsDriver::PrintBootInfo)
        .def("WipeVolume", &Erasure::FileSystems::NtfsDriver::WipeVolume)
        .def("EraseFile", &Erasure::FileSystems::NtfsDriver::EraseFile, py::arg("relativePath"))
        .def("EraseDirectory", &Erasure::FileSystems::NtfsDriver::EraseDirectory, py::arg("relativePath"))
        .def("FormatDrive", &Erasure::FileSystems::NtfsDriver::FormatDrive, py::arg("fullDriveSanitize") = false)
        .def("VerifyAndErase", &Erasure::FileSystems::NtfsDriver::VerifyAndErase, py::arg("targetPath"))
        .def("VerifyAndFormatDrive", &Erasure::FileSystems::NtfsDriver::VerifyAndFormatDrive, py::arg("fullDriveSanitize") = false)
        .def("GetBytesPerSector", &Erasure::FileSystems::NtfsDriver::GetBytesPerSector)
        .def("GetBytesPerCluster", &Erasure::FileSystems::NtfsDriver::GetBytesPerCluster)
        .def("GetMftRecordSize", &Erasure::FileSystems::NtfsDriver::GetMftRecordSize)
        .def("ClusterToSector", &Erasure::FileSystems::NtfsDriver::ClusterToSector, py::arg("lcn"))
        .def("LocateTargetLocations", [](const Erasure::FileSystems::NtfsDriver& self, const std::string& path) {
            Erasure::FileSystems::NTFS::TargetLocations locs;
            bool ok = self.LocateTargetLocations(path, locs);
            return std::make_pair(ok, locs);
        }, py::arg("relativePath"));
}
