import subprocess

def test():
    cmd = ['chkdsk', 'D:', '/f', '/x']
    try:
        res = subprocess.run(cmd, capture_output=True, text=True)
        print("STDOUT:")
        print(res.stdout)
        print("STDERR:")
        print(res.stderr)
    except Exception as e:
        print("Exception:", e)

if __name__ == '__main__':
    test()
