import os
import subprocess
import sys
import tempfile

def test_vhd():
    temp_dir = tempfile.gettempdir()
    cmd_path = os.path.join(temp_dir, "create_test_vhd.cmd")
    vhd_path = os.path.join(temp_dir, "SanitizeX_TestDrive.vhd")
    script_path = os.path.join(temp_dir, "dp_vhd_script.txt")

    content = f"""@echo off
echo Preparing VHD diskpart script...
(
echo select vdisk file="{vhd_path}"
echo detach vdisk
echo create vdisk file="{vhd_path}" maximum=500 type=fixed
echo select vdisk file="{vhd_path}"
echo attach vdisk
echo convert mbr
echo create partition primary
echo format fs=ntfs label="SanitizeX_Test" quick
echo assign
) > "{script_path}"

diskpart /s "{script_path}"
"""
    with open(cmd_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"Created script at {cmd_path}")
    # Try running via PowerShell with UAC elevation (Verb RunAs)
    ps_cmd = f'powershell -NoProfile -Command "Start-Process cmd -ArgumentList \'/c \"\"{cmd_path}\"\"\' -Verb RunAs -Wait"'
    print("Running:", ps_cmd)
    res = subprocess.run(ps_cmd, shell=True, capture_output=True, text=True)
    print("STDOUT:", res.stdout)
    print("STDERR:", res.stderr)

if __name__ == "__main__":
    test_vhd()
