import ctypes
from ctypes import wintypes
import sys

# Define constants
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
IOCTL_DISK_GET_DRIVE_GEOMETRY_EX = 0x000700A0
IOCTL_DISK_GET_DRIVE_GEOMETRY = 0x00070000

kernel32 = ctypes.windll.kernel32

def test_drive(drive_letter):
    print(f"--- Testing Drive {drive_letter} ---")
    device_path = f"\\\\.\\{drive_letter}".encode('ascii')
    
    hDevice = kernel32.CreateFileA(
        device_path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        None,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        None
    )

    if hDevice == -1 or hDevice == 0xffffffff:
        print(f"CreateFileA failed for {device_path.decode()}, error: {kernel32.GetLastError()}")
        return
    else:
        print(f"CreateFileA succeeded for {device_path.decode()}")
        
    out_buffer = ctypes.create_string_buffer(200)
    bytes_returned = wintypes.DWORD()
    
    # Test _EX
    success_ex = kernel32.DeviceIoControl(
        hDevice,
        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        None,
        0,
        out_buffer,
        len(out_buffer),
        ctypes.byref(bytes_returned),
        None
    )
    
    if success_ex:
        print("UpdateGeometry (IOCTL_DISK_GET_DRIVE_GEOMETRY_EX) succeeded")
    else:
        print(f"UpdateGeometry (IOCTL_DISK_GET_DRIVE_GEOMETRY_EX) failed, error: {kernel32.GetLastError()}")
        
        # Test fallback
        success_norm = kernel32.DeviceIoControl(
            hDevice,
            IOCTL_DISK_GET_DRIVE_GEOMETRY,
            None,
            0,
            out_buffer,
            len(out_buffer),
            ctypes.byref(bytes_returned),
            None
        )
        if success_norm:
            print("Fallback IOCTL_DISK_GET_DRIVE_GEOMETRY succeeded!")
        else:
            print(f"Fallback IOCTL_DISK_GET_DRIVE_GEOMETRY failed, error: {kernel32.GetLastError()}")
            
    kernel32.CloseHandle(hDevice)
    print("\n")

if __name__ == "__main__":
    is_admin = ctypes.windll.shell32.IsUserAnAdmin()
    if not is_admin:
        print("Please run this script as Administrator to get accurate results.")
    
    test_drive("C:")
    test_drive("D:")
