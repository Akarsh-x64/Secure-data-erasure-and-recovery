# import subprocess
# import core as sec

# print("==========================================")
# print("    Secure Erasure Engine - Python      ")
# print("==========================================")

# path = input("Enter the path to your target device / volume: ").strip()
# if not path:
#     print("Invalid path.")
#     exit(1)

# os_device = sec.LinuxStorageDevice()
# print(f"\nAttempting to open handle to: {path} ...")
# if not os_device.Open(path):
#     print("[ERROR] Failed to open device! (Check admin privileges/path)")
#     exit(1)

# print("[SUCCESS] Handle opened!")
# hardware = sec.HDDController(os_device)

# print("\nAttempting to lock and dismount volume...")
# if os_device.LockVolume():
#     print("  -> Volume Locked!")
# else:
#     print("  -> WARNING: Could not lock volume.")

# if os_device.DismountVolume():
#     print("  -> Volume Dismounted!")

# choice = input("\nSelect Filesystem Driver:\n  [1] exFAT\n  [2] ext4\nEnter choice (default 1): ").strip()

# if choice == "2":
#     fs_driver = sec.Ext4Driver(hardware)
#     print("\nAttempting to Mount ext4...")
#     if not fs_driver.Mount():
#         print("[ERROR] Failed to mount ext4.")
#         exit(1)
#     fs_driver.PrintSuperblockInfo()
# else:
#     fs_driver = sec.ExFatDriver(hardware)
#     print("\nAttempting to Mount exFAT...")
#     if not fs_driver.Mount():
#         print("[ERROR] Failed to mount exFAT.")
#         exit(1)
#     fs_driver.PrintVBRInfo()

# filename = input("\nEnter exact relative path to delete or 'WIPE': ").strip()
# success = False

# if filename == "WIPE":
#     success = fs_driver.WipeVolume()
# elif filename:
#     success = fs_driver.EraseFile(filename)

# if success:
#     os_device.Close()
#     repair_cmd = ["sudo", "../_externals/e2fsck", "-f", "-y", path] if choice == "2" else ["sudo", "../_externals/fsck.exfat", "-y", path]
#     result = subprocess.run(repair_cmd)
#     if result.returncode == 0:
#         print("[SUCCESS] Filesystem check completed cleanly.")
#     else:
#         print(f"[WARNING] Filesystem check exited with code {result.returncode}")
import os
import subprocess
import osdevice
import hdd
import exfat
import ext4
import sys
import ctypes

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def ensure_admin():
    if os.name == 'nt':
        try:
            is_admin = ctypes.windll.shell32.IsUserAnAdmin()
        except:
            is_admin = False
        
        if not is_admin:
            print("[INFO] Requesting Windows Administrator privileges...")
            # Relaunch the script and prompt for UAC elevation
            ctypes.windll.shell32.ShellExecuteW(
                None, "runas", sys.executable, " ".join(sys.argv), None, 1
            )
            sys.exit(0) # Exit the non-admin instance
    else:
        # Optional: Linux admin check
        if os.geteuid() != 0:
            print("[ERROR] Please run this script with sudo.")
            sys.exit(1)
ensure_admin()

print("==========================================")
print("    Secure Erasure Engine - Python      ")
print("==========================================")

path = input("Enter the path to your target device / volume: ").strip()
if not path:
    print("Invalid path.")
    exit(1)

if os.name == 'nt':
    device = osdevice.WindowsStorageDevice()
else:
    device = osdevice.LinuxStorageDevice()

print(f"\nAttempting to open handle to: {path} ...")
if not device.Open(path):
    print("[ERROR] Failed to open device! (Check admin privileges/path)")
    exit(1)

print("[SUCCESS] Handle opened!")
hdd_controller = hdd.HDDController(device)

print("\nAttempting to lock and dismount volume...")
if device.LockVolume():
    print("  -> Volume Locked!")
else:
    print("  -> WARNING: Could not lock volume.")

if device.DismountVolume():
    print("  -> Volume Dismounted!")

choice = input("\nSelect Filesystem Driver:\n  [1] exFAT\n  [2] ext4\nEnter choice (default 1): ").strip()

if choice == "2":
    fs_driver = ext4.Ext4Driver(hdd_controller)
    print("\nAttempting to Mount ext4...")
    if not fs_driver.Mount():
        print("[ERROR] Failed to mount ext4.")
        exit(1)
    fs_driver.PrintSuperblockInfo()
else:
    fs_driver = exfat.ExFatDriver(hdd_controller)
    print("\nAttempting to Mount exFAT...")
    if not fs_driver.Mount():
        print("[ERROR] Failed to mount exFAT.")
        exit(1)
    fs_driver.PrintVBRInfo()

filename = input("\nEnter exact relative path to delete or 'WIPE': ").strip()
success = False

if filename == "WIPE":
    success = fs_driver.WipeVolume()
elif filename:
    success = fs_driver.EraseFile(filename)

if success:
    device.Close()
    
    # Ensure path points to a partition if on Windows, or use WSL block devices
    if os.name == 'nt':
        e2fsck_bin = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "_externals", "e2fsck.exe"))
        exfat_bin = os.path.abspath(os.path.join(SCRIPT_DIR, "_externals", "fsck.exfat.exe"))
        
        target_bin = e2fsck_bin if choice == "2" else exfat_bin

        if not os.path.exists(target_bin):
            print(f"[ERROR] Executable not found at resolved path: {target_bin}")
            while(input()):
                pass
            sys.exit(1)

        # Note: e2fsck requires a partition path (e.g., \\.\PhysicalDrive1p1) 
        # or you should run the check via WSL where ext4 tools natively map partitions.
        repair_cmd = [target_bin, "-f", "-y", path]
    else:
        e2fsck_bin = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "_externals", "e2fsck"))
        exfat_bin = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "_externals", "fsck.exfat"))
        target_bin = e2fsck_bin if choice == "2" else exfat_bin
        repair_cmd = ["sudo", target_bin, "-f", "-y", path]

    try:
        result = subprocess.run(repair_cmd)
        if result.returncode == 0:
            print("[SUCCESS] Filesystem check completed cleanly.")
        else:
            print(f"[WARNING] Filesystem check exited with code {result.returncode}")
    except Exception as e:
        print(f"[ERROR] Failed to run repair tool: {e}")
    while(input()):
        pass