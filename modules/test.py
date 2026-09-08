import subprocess
import core as sec

print("==========================================")
print("    Secure Erasure Engine - Python      ")
print("==========================================")

path = input("Enter the path to your target device / volume: ").strip()
if not path:
    print("Invalid path.")
    exit(1)

os_device = sec.LinuxStorageDevice()
print(f"\nAttempting to open handle to: {path} ...")
if not os_device.Open(path):
    print("[ERROR] Failed to open device! (Check admin privileges/path)")
    exit(1)

print("[SUCCESS] Handle opened!")
hardware = sec.HDDController(os_device)

print("\nAttempting to lock and dismount volume...")
if os_device.LockVolume():
    print("  -> Volume Locked!")
else:
    print("  -> WARNING: Could not lock volume.")

if os_device.DismountVolume():
    print("  -> Volume Dismounted!")

choice = input("\nSelect Filesystem Driver:\n  [1] exFAT\n  [2] ext4\nEnter choice (default 1): ").strip()

if choice == "2":
    fs_driver = sec.Ext4Driver(hardware)
    print("\nAttempting to Mount ext4...")
    if not fs_driver.Mount():
        print("[ERROR] Failed to mount ext4.")
        exit(1)
    fs_driver.PrintSuperblockInfo()
else:
    fs_driver = sec.ExFatDriver(hardware)
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
    os_device.Close()
    repair_cmd = ["sudo", "../_externals/e2fsck", "-f", "-y", path] if choice == "2" else ["sudo", "../_externals/fsck.exfat", "-y", path]
    result = subprocess.run(repair_cmd)
    if result.returncode == 0:
        print("[SUCCESS] Filesystem check completed cleanly.")
    else:
        print(f"[WARNING] Filesystem check exited with code {result.returncode}")