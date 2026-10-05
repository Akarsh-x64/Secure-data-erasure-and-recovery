import os
import sys
import subprocess
import sysconfig

def build():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    recovery_dir = os.path.join(os.path.dirname(__file__))
    output_dir = os.path.join(repo_root, 'modules')
    
    python_inc = sysconfig.get_path('include')
    python_lib = os.path.join(sysconfig.get_config_var('LIBDIR') or '', 'python314.lib')
    if not os.path.exists(python_lib):
        python_lib = os.path.join(sys.prefix, 'libs', 'python314.lib')
        
    pybind11_inc = os.path.join(sys.prefix, 'Lib', 'site-packages', 'pybind11', 'include')

    sources = [
        os.path.join(recovery_dir, 'bindings_recovery.cpp'),
        os.path.join(recovery_dir, 'recover.cpp')
    ]

    os.makedirs(output_dir, exist_ok=True)
    out_pyd = os.path.join(output_dir, 'recovery_v2.cp314-win_amd64.pyd')
    
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

    print("Building C++ Recovery 2.0 module using MSYS2...")
    print(" ".join(cmd))
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"SUCCESS: Built {out_pyd}")
    else:
        print(f"BUILD FAILED:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")

if __name__ == '__main__':
    build()
