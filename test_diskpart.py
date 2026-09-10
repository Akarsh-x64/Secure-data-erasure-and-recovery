import subprocess
def check():
    script = "select volume D\ndetail volume\n"
    with open("dp_script.txt", "w") as f:
        f.write(script)
    res = subprocess.run(['diskpart', '/s', 'dp_script.txt'], capture_output=True, text=True)
    print("STDOUT:")
    print(res.stdout)
    print("STDERR:")
    print(res.stderr)
if __name__ == "__main__":
    check()
