import requests
import time

def test():
    # Start scan
    res = requests.post("http://127.0.0.1:5000/api/v1/recovery/scans", json={"diskImage": "C:\\", "mode": "both"})
    print("Scan start:", res.json())
    op_id = res.json()["operationId"]

    # Wait for completion
    while True:
        status = requests.get(f"http://127.0.0.1:5000/api/v1/recovery/scans/{op_id}").json()
        print("Status:", status["percent"])
        if status["state"] in ["completed", "failed"]:
            break
        time.sleep(1)

    # Get artifacts
    arts = requests.get(f"http://127.0.0.1:5000/api/v1/recovery/scans/{op_id}/artifacts").json()
    print("Artifacts:", len(arts))
    if len(arts) > 0:
        art_id = arts[0]["id"]
        # Export
        exp = requests.post(f"http://127.0.0.1:5000/api/v1/recovery/artifacts/{art_id}/export", json={"targetDirectory": "C:\\Users\\devan\\Downloads"})
        print("Export:", exp.status_code, exp.text)

if __name__ == "__main__":
    test()
