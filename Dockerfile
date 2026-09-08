FROM quay.io/pypa/manylinux_2_28_x86_64

RUN yum install -y cmake3

# Force the container to use Python 3.14 for EVERYTHING
ENV PYTHON_ROOT="/opt/python/cp314-cp314"
ENV PATH="${PYTHON_ROOT}/bin:$PATH"

# --- DEBUGGING: Print the Python version being used ---
RUN python3 --version && pip --version
# ------------------------------------------------------

# Install pybind11 explicitly into the 3.14 environment
RUN pip install pybind11

WORKDIR /workspace
COPY . .
WORKDIR /workspace/modules

# Clean any copied build files, then compile using 3.14
RUN rm -rf build && mkdir build && cd build && \
    cmake3 -DPYTHON_EXECUTABLE=${PYTHON_ROOT}/bin/python3 \
           -DPython_EXECUTABLE=${PYTHON_ROOT}/bin/python3 \
           -DPython3_EXECUTABLE=${PYTHON_ROOT}/bin/python3 \
           -DPYBIND11_PYTHON_VERSION=3.14 \
           -DPython3_ROOT_DIR=${PYTHON_ROOT} \
           -Dpybind11_DIR=$(${PYTHON_ROOT}/bin/python3 -m pybind11 --cmakedir) .. && \
    make