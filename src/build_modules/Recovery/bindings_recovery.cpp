#include "recover.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl/filesystem.h>

namespace py = pybind11;

PYBIND11_MODULE(recovery_v2, module) {
    module.doc() = "Secure Data Erasure and Recovery C++ bindings - Version 2.0 (Subprocess Orchestration)";

    module.def(
        "recover_metadata",
        &Recovery::RecoverMetadata,
        py::arg("disk_image"),
        py::arg("output_root") = "Recovery/output",
        "Recover files using filesystem metadata and TSK Subprocess.");

    module.def(
        "recover_carving",
        &Recovery::RecoverCarving,
        py::arg("disk_image"),
        py::arg("output_root") = "Recovery/output",
        "Recover files using PhotoRec carving Subprocess.");
}
