FROM quay.io/pypa/manylinux_2_28_x86_64

RUN yum install -y cmake3

# Force the container to use Python 3.14 for EVERYTHING
ENV PYTHON_ROOT="/opt/python/cp314-cp314"
ENV PATH="${PYTHON_ROOT}/bin:$PATH"

# Install pybind11 explicitly into the 3.14 environment
RUN ${PYTHON_ROOT}/bin/python3 --version && \
       ${PYTHON_ROOT}/bin/python3 -m pip --version && \
       ${PYTHON_ROOT}/bin/python3 -m pip install pybind11 && \
       ${PYTHON_ROOT}/bin/python3 -m pybind11 --version

# Resolve the matching Python development headers from the selected interpreter.
# manylinux intentionally does not ship libpython; extension modules use the
# Python module development component and do not embed the interpreter.
RUN PYTHON_INCLUDE_DIR=$(${PYTHON_ROOT}/bin/python3 -c \
              'import sysconfig; print(sysconfig.get_path("include"))') && \
       test -f "${PYTHON_INCLUDE_DIR}/Python.h" && \
       printf 'Python include: %s\n' "${PYTHON_INCLUDE_DIR}"

WORKDIR /workspace
COPY . .
WORKDIR /workspace/modules

# Clean any copied build files, then compile the existing Erasure modules.
RUN rm -rf /workspace/modules/build && \
       PYTHON_INCLUDE_DIR=$(${PYTHON_ROOT}/bin/python3 -c \
              'import sysconfig; print(sysconfig.get_path("include"))') && \
    cmake3 -S /workspace/modules -B /workspace/modules/build \
           -DPython3_EXECUTABLE=${PYTHON_ROOT}/bin/python3 \
           -DPython3_ROOT_DIR=${PYTHON_ROOT} \
                 -DPython3_INCLUDE_DIR="${PYTHON_INCLUDE_DIR}" \
           -Dpybind11_DIR=$(${PYTHON_ROOT}/bin/python3 -m pybind11 --cmakedir) && \
    cmake3 --build /workspace/modules/build --parallel 2

# Build Recovery, including bundled TSK, using the same manylinux Python.
RUN rm -rf /workspace/recovery-build && \
       PYTHON_INCLUDE_DIR=$(${PYTHON_ROOT}/bin/python3 -c \
              'import sysconfig; print(sysconfig.get_path("include"))') && \
    cmake3 -S /workspace -B /workspace/recovery-build \
           -DRECOVERY_ENABLE_TSK=ON \
           -DPython3_EXECUTABLE=${PYTHON_ROOT}/bin/python3 \
           -DPython3_ROOT_DIR=${PYTHON_ROOT} \
                 -DPython3_INCLUDE_DIR="${PYTHON_INCLUDE_DIR}" \
           -Dpybind11_DIR=$(${PYTHON_ROOT}/bin/python3 -m pybind11 --cmakedir) && \
    cmake3 --build /workspace/recovery-build --parallel 2