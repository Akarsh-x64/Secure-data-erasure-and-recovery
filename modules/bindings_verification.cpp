#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>

#include "../Erasure/Verification/VerificationEngine.h"
#include "../Erasure/Verification/VerificationReport.h"
#include "../Erasure/Verification/StatisticalTests.h"
#include "../Erasure/Verification/SignatureCarver.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"

#ifdef _WIN32
#include "../Erasure/OS/Windows/WindowsStorageDevice.h"
using TargetOSDevice = Erasure::OS::WindowsStorageDevice;
#elif defined(__linux__)
#include "../Erasure/OS/Linux/LinuxStorageDevice.h"
using TargetOSDevice = Erasure::OS::LinuxStorageDevice;
#endif

namespace py = pybind11;

PYBIND11_MODULE(verification, m) {
    m.doc() = "Python bindings for Forensic Verification, Statistical Auditing & Signature Carving";

    // Enums
    py::enum_<Erasure::Verification::VerificationScope>(m, "VerificationScope")
        .value("FILE_ERASURE", Erasure::Verification::VerificationScope::FILE_ERASURE)
        .value("DIRECTORY_ERASURE", Erasure::Verification::VerificationScope::DIRECTORY_ERASURE)
        .value("VOLUME_WIPE", Erasure::Verification::VerificationScope::VOLUME_WIPE)
        .export_values();

    py::enum_<Erasure::Verification::SignatureCategory>(m, "SignatureCategory")
        .value("DOCUMENT", Erasure::Verification::SignatureCategory::DOCUMENT)
        .value("IMAGE", Erasure::Verification::SignatureCategory::IMAGE)
        .value("ARCHIVE", Erasure::Verification::SignatureCategory::ARCHIVE)
        .value("EXECUTABLE", Erasure::Verification::SignatureCategory::EXECUTABLE)
        .value("AUDIO_VIDEO", Erasure::Verification::SignatureCategory::AUDIO_VIDEO)
        .value("DATABASE_SYSTEM", Erasure::Verification::SignatureCategory::DATABASE_SYSTEM)
        .export_values();

    // Structures
    py::class_<Erasure::Verification::CarvedArtifact>(m, "CarvedArtifact")
        .def(py::init<>())
        .def_readwrite("signatureName", &Erasure::Verification::CarvedArtifact::signatureName)
        .def_readwrite("extension", &Erasure::Verification::CarvedArtifact::extension)
        .def_readwrite("category", &Erasure::Verification::CarvedArtifact::category)
        .def_readwrite("byteOffset", &Erasure::Verification::CarvedArtifact::byteOffset)
        .def_readwrite("lba", &Erasure::Verification::CarvedArtifact::lba);

    py::class_<Erasure::Verification::StatisticalAuditResult>(m, "StatisticalAuditResult")
        .def(py::init<>())
        .def_readwrite("shannonEntropy", &Erasure::Verification::StatisticalAuditResult::shannonEntropy)
        .def_readwrite("chiSquareValue", &Erasure::Verification::StatisticalAuditResult::chiSquareValue)
        .def_readwrite("chiSquarePValue", &Erasure::Verification::StatisticalAuditResult::chiSquarePValue)
        .def_readwrite("serialCorrelation", &Erasure::Verification::StatisticalAuditResult::serialCorrelation)
        .def_readwrite("monteCarloPi", &Erasure::Verification::StatisticalAuditResult::monteCarloPi)
        .def_readwrite("monteCarloPiErrorPercent", &Erasure::Verification::StatisticalAuditResult::monteCarloPiErrorPercent)
        .def_readwrite("totalBytesAnalyzed", &Erasure::Verification::StatisticalAuditResult::totalBytesAnalyzed)
        .def_readwrite("byteHistogram", &Erasure::Verification::StatisticalAuditResult::byteHistogram);

    py::class_<Erasure::Verification::AuditReport>(m, "AuditReport")
        .def(py::init<>())
        .def_readwrite("targetPath", &Erasure::Verification::AuditReport::targetPath)
        .def_readwrite("scope", &Erasure::Verification::AuditReport::scope)
        .def_readwrite("timestamp", &Erasure::Verification::AuditReport::timestamp)
        .def_readwrite("erasureStandard", &Erasure::Verification::AuditReport::erasureStandard)
        .def_readwrite("filesystemType", &Erasure::Verification::AuditReport::filesystemType)
        .def_readwrite("sectorsAudited", &Erasure::Verification::AuditReport::sectorsAudited)
        .def_readwrite("totalBytesAudited", &Erasure::Verification::AuditReport::totalBytesAudited)
        .def_readwrite("clusterSize", &Erasure::Verification::AuditReport::clusterSize)
        .def_readwrite("preWipeSha256", &Erasure::Verification::AuditReport::preWipeSha256)
        .def_readwrite("postWipeSha256", &Erasure::Verification::AuditReport::postWipeSha256)
        .def_readwrite("rawByteMatchRate", &Erasure::Verification::AuditReport::rawByteMatchRate)
        .def_readwrite("shannonEntropy", &Erasure::Verification::AuditReport::shannonEntropy)
        .def_readwrite("chiSquareValue", &Erasure::Verification::AuditReport::chiSquareValue)
        .def_readwrite("chiSquarePValue", &Erasure::Verification::AuditReport::chiSquarePValue)
        .def_readwrite("serialCorrelation", &Erasure::Verification::AuditReport::serialCorrelation)
        .def_readwrite("monteCarloPi", &Erasure::Verification::AuditReport::monteCarloPi)
        .def_readwrite("monteCarloPiError", &Erasure::Verification::AuditReport::monteCarloPiError)
        .def_readwrite("byteHistogram", &Erasure::Verification::AuditReport::byteHistogram)
        .def_readwrite("signaturesChecked", &Erasure::Verification::AuditReport::signaturesChecked)
        .def_readwrite("signaturesDetected", &Erasure::Verification::AuditReport::signaturesDetected)
        .def_readwrite("detectedArtifacts", &Erasure::Verification::AuditReport::detectedArtifacts)
        .def_readwrite("slackBytesAudited", &Erasure::Verification::AuditReport::slackBytesAudited)
        .def_readwrite("slackResidualBytes", &Erasure::Verification::AuditReport::slackResidualBytes)
        .def_readwrite("metadataCleared", &Erasure::Verification::AuditReport::metadataCleared)
        .def_readwrite("directoryUnlinked", &Erasure::Verification::AuditReport::directoryUnlinked)
        .def_readwrite("nistSamplesChecked", &Erasure::Verification::AuditReport::nistSamplesChecked)
        .def_readwrite("nistZonesCount", &Erasure::Verification::AuditReport::nistZonesCount)
        .def_readwrite("nistConfidencePercent", &Erasure::Verification::AuditReport::nistConfidencePercent)
        .def_readwrite("passed", &Erasure::Verification::AuditReport::passed)
        .def_readwrite("failureReason", &Erasure::Verification::AuditReport::failureReason)
        .def("ToJson", &Erasure::Verification::AuditReport::ToJson)
        .def("PrintTerminalReport", [](const Erasure::Verification::AuditReport& self) {
            self.PrintTerminalReport(std::cout);
        });

    // StatisticalTests
    py::class_<Erasure::Verification::StatisticalTests>(m, "StatisticalTests")
        .def_static("CalculateShannonEntropy", [](py::bytes buffer) {
            std::string s = buffer;
            return Erasure::Verification::StatisticalTests::CalculateShannonEntropy(
                reinterpret_cast<const uint8_t*>(s.data()), s.size());
        }, py::arg("buffer"))
        .def_static("CalculateChiSquare", [](py::bytes buffer) {
            std::string s = buffer;
            double pValue = 0.0;
            double chi = Erasure::Verification::StatisticalTests::CalculateChiSquare(
                reinterpret_cast<const uint8_t*>(s.data()), s.size(), pValue);
            return std::make_pair(chi, pValue);
        }, py::arg("buffer"))
        .def_static("CalculateSerialCorrelation", [](py::bytes buffer) {
            std::string s = buffer;
            return Erasure::Verification::StatisticalTests::CalculateSerialCorrelation(
                reinterpret_cast<const uint8_t*>(s.data()), s.size());
        }, py::arg("buffer"))
        .def_static("EstimateMonteCarloPi", [](py::bytes buffer) {
            std::string s = buffer;
            double errorPct = 0.0;
            double piVal = Erasure::Verification::StatisticalTests::EstimateMonteCarloPi(
                reinterpret_cast<const uint8_t*>(s.data()), s.size(), errorPct);
            return std::make_pair(piVal, errorPct);
        }, py::arg("buffer"))
        .def_static("ComputeSha256", [](py::bytes buffer) {
            std::string s = buffer;
            return Erasure::Verification::StatisticalTests::ComputeSha256(
                reinterpret_cast<const uint8_t*>(s.data()), s.size());
        }, py::arg("buffer"))
        .def_static("RunFullAudit", [](py::bytes buffer) {
            std::string s = buffer;
            return Erasure::Verification::StatisticalTests::RunFullAudit(
                reinterpret_cast<const uint8_t*>(s.data()), s.size());
        }, py::arg("buffer"))
        .def_static("RenderAsciiGauge", &Erasure::Verification::StatisticalTests::RenderAsciiGauge,
                    py::arg("value"), py::arg("maxVal"), py::arg("width"), py::arg("unit"))
        .def_static("RenderConfidenceGauge", &Erasure::Verification::StatisticalTests::RenderConfidenceGauge,
                    py::arg("confidencePercent"), py::arg("width") = 30)
        .def_static("RenderByteDistributionHistogram", &Erasure::Verification::StatisticalTests::RenderByteDistributionHistogram,
                    py::arg("hist256"), py::arg("width") = 32, py::arg("height") = 8);

    // SignatureCarver
    py::class_<Erasure::Verification::SignatureCarver>(m, "SignatureCarver")
        .def(py::init<>())
        .def("GetSignatureCount", &Erasure::Verification::SignatureCarver::GetSignatureCount)
        .def("ScanBuffer", [](const Erasure::Verification::SignatureCarver& self,
                              py::bytes buffer, uint64_t basePhysicalOffset, uint32_t sectorSize) {
            std::string s = buffer;
            return self.ScanBuffer(reinterpret_cast<const uint8_t*>(s.data()), s.size(),
                                   basePhysicalOffset, sectorSize);
        }, py::arg("buffer"), py::arg("basePhysicalOffset") = 0, py::arg("sectorSize") = 512);

    // VerificationEngine
    py::class_<Erasure::Verification::VerificationEngine>(m, "VerificationEngine")
        .def(py::init<Erasure::Hardware::HDDController*, TargetOSDevice*>(), py::arg("hardware"), py::arg("device"))
        .def("CapturePreWipeDigest", &Erasure::Verification::VerificationEngine::CapturePreWipeDigest, py::arg("sectors"))
        .def("AuditFileErasure", &Erasure::Verification::VerificationEngine::AuditFileErasure,
             py::arg("path"), py::arg("fsType"), py::arg("sectors"), py::arg("fileSize"),
             py::arg("clusterSize"), py::arg("preWipeSha256"), py::arg("metadataCleared") = true,
             py::arg("dirUnlinked") = true)
        .def("AuditDirectoryErasure", &Erasure::Verification::VerificationEngine::AuditDirectoryErasure,
             py::arg("path"), py::arg("fsType"), py::arg("childFiles"), py::arg("dirMetadataSectors"),
             py::arg("clusterSize"), py::arg("parentUnlinked") = true)
        .def("AuditVolumeWipe", &Erasure::Verification::VerificationEngine::AuditVolumeWipe,
             py::arg("fsType"), py::arg("firstDataSector"), py::arg("totalSectors"),
             py::arg("sectorsPerCluster"), py::arg("quarantinedSectors") = std::vector<uint64_t>{});
}
