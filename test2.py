import subprocess
import ctypes
import os

print("IsAdmin:", ctypes.windll.shell32.IsUserAnAdmin())
res = subprocess.run('cmd.exe /c chkdsk', capture_output=True, text=True, shell=True)
print("OUT:", res.stdout)
print("ERR:", res.stderr)
