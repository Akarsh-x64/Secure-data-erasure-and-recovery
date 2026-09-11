import os
import sys
import subprocess
import sysconfig

def build():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    recovery_dir = os.path.join(repo_root, 'Recovery')
    output_dir = os.path.join(repo_root, 'modules')
    
    python_inc = sysconfig.get_path('include')
    python_lib = os.path.join(sysconfig.get_config_var('LIBDIR') or '', 'python314.lib')
    if not os.path.exists(python_lib):
        python_lib = os.path.join(sys.prefix, 'libs', 'python314.lib')
        
    pybind11_inc = os.path.join(sys.prefix, 'Lib', 'site-packages', 'pybind11', 'include')

    sources = [
        os.path.join(recovery_dir, 'bindings_recovery.cpp'),
        os.path.join(recovery_dir, 'recover.cpp'),
        os.path.join(recovery_dir, 'Acquisition', 'WindowsReadOnlyStorage.cpp'),
        os.path.join(recovery_dir, 'Audit', 'EvidenceManifest.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Core', 'RecoveryOrchestrator.cpp'),
        os.path.join(recovery_dir, 'Carving', 'PhotoRec', 'PhotoRecCarver.cpp'),
        os.path.join(recovery_dir, 'Carving', 'PhotoRec', 'PhotoRecProcessRunner.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'IsoBmffVerifier.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'JpegVerifier.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'OoxmlVerifier.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'PdfVerifier.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'PngVerifier.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'Sha256.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'VerificationEngine.cpp'),
        os.path.join(recovery_dir, 'Carving', 'Verification', 'ZipVerifier.cpp'),
        os.path.join(recovery_dir, 'Filesystems', 'Ext4Detector.cpp'),
        os.path.join(recovery_dir, 'Filesystems', 'FilesystemDetector.cpp'),
        os.path.join(recovery_dir, 'Filesystems', 'NTFS', 'DataRunParser.cpp'),
        os.path.join(recovery_dir, 'Filesystems', 'NTFS', 'MFTParser.cpp'),
        os.path.join(recovery_dir, 'Filesystems', 'NTFS', 'NTFSFileSystem.cpp'),
        os.path.join(recovery_dir, 'Metadata', 'MetadataStore.cpp'),
        os.path.join(recovery_dir, 'Partitions', 'GPTParser.cpp'),
        os.path.join(recovery_dir, 'Partitions', 'MBRParser.cpp'),
        os.path.join(recovery_dir, 'TSK', 'TskFileSystem.cpp'),
        os.path.join(recovery_dir, 'TSK', 'TskImageBridge.cpp'),
    ]

    out_pyd = os.path.join(output_dir, 'recovery.cp314-win_amd64.pyd')
    
    cmd = [
        r'C:\msys64\ucrt64\bin\g++.exe',
        '-shared',
        '-std=c++17',
        '-O2',
        '-static-libgcc',
        '-static-libstdc++',
        f'-I{python_inc}',
        f'-I{pybind11_inc}',
        f'-I{recovery_dir}',
        '-DNOMINMAX',
        '-o', out_pyd,
    ] + sources + [
        python_lib
    ]


    print("Building C++ recovery module...")
    print(" ".join(cmd))
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"SUCCESS: Built {out_pyd}")
    else:
        print(f"BUILD FAILED:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")

if __name__ == '__main__':
    build()
