import ctypes
import subprocess
import sys

def main():
    is_admin = False
    try:
        is_admin = ctypes.windll.shell32.IsUserAnAdmin()
    except:
        pass
    
    print(f"Is Admin: {is_admin}")
    
    cmd = ['chkdsk', 'D:', '/f', '/x']
    try:
        res = subprocess.run(cmd, capture_output=True, text=True)
        print("STDOUT:", res.stdout)
        print("STDERR:", res.stderr)
    except Exception as e:
        print("Exception:", e)

if __name__ == '__main__':
    main()
