from flask import Flask,jsonify,request
from flask_cors import CORS
from flask_socketio import SocketIO
from pydantic import BaseModel,ValidationError
from typing import Literal,Optional
import uuid
import threading
import time
import subprocess

import sys
import os

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'modules'))

try:
    import osdevice
    import hdd
    import exfat
    import ext4
    import fat32
    import ntfs
    import verification
except ImportError as e:
    print(f"Warning: Failed to import erasure modules: {e}")

def ensure_admin():
    if os.name == 'nt':
        try:
            import ctypes
            is_admin = ctypes.windll.shell32.IsUserAnAdmin()
        except:
            is_admin = False
        
        if not is_admin:
            print("[INFO] Requesting Windows Administrator privileges...")
            import ctypes, sys
            ctypes.windll.shell32.ShellExecuteW(
                None, "runas", sys.executable, " ".join(sys.argv), None, 1
            )
            sys.exit(0)
    else:
        if os.geteuid() != 0:
            print("[ERROR] Please run this script with sudo.")
            sys.exit(1)

ensure_admin()

app = Flask(__name__)
CORS(app)

socketio = SocketIO(app, cors_allowed_origins="*")

active_operations = {}

#Structures
OperationState = Literal['queued',
	'running',
	'verifying',
	'completed',
	'failed',
	'cancelled']

class OperationRef(BaseModel):
    operationId: str
    state : OperationState
    createdAt : str

class ProgressEvent(BaseModel):
    operationId: str
    state: OperationState
    phase: str
    percent: int
    currentBytes: Optional[int]=None
    totalBytes: Optional[int]=None
    currentSector: Optional[int]=None
    totalSectors: Optional[int]=None
    passIndex: Optional[int]=None
    passTotal: Optional[int]=None
    throughputMBps: Optional[int]=None
    etaSeconds: Optional[int]=None
    message: Optional[str]=None
    timestamp: str
    
class ErrorResponse(BaseModel):
    code: str
    message: str
    details: Optional[str]=None
    retryable: bool
    
class DeviceCapabilities(BaseModel):
    fileErase:bool
    driveErase: bool
    secureErase: bool
    trim: bool
    cryptoErase:bool
    
class StorageDevice(BaseModel):
    id: str
    path: str
    model: str
    serial: Optional[str]=None
    busType: Literal['SATA','NVMe','USB','unknown']
    capacityBytes: int
    sectorSizeBytes: int
    health: Literal['healthy','warning','critical','unknown']
    mounted: bool
    readOnly: bool
    writeProtected: bool
    identityToken: str
    
class FileNode(BaseModel):
    id: str
    parentId: Optional[str]=None
    name: str
    canonicalPath: str
    kind: Literal['file','directory','drive']
    sizeBytes: Optional[int]=None
    modifiedAt: Optional[str]=None
    filesystem: Optional[Literal['NTFS','ext4','exFAT', 'FAT32','unknown']]=None
    clusterSizeBytes: Optional[int]=None
    inode: Optional[int]=None
    startSector: Optional[int]=None
    sectorCount: Optional[int]=None
    deleted: Optional[bool]=None
    corrupted: Optional[bool]=None
    confidence: Optional[int]=None
    children: Optional['FileNode']=None
    
class EraseTarget(BaseModel):
    nodeId: str
    canonicalPath: str
    kind: Optional[str] = None
    filesystem: str  # Required for selecting the correct driver

class EraseConfig(BaseModel):
    clearMetadata: bool
    wipeSlackSpace: bool
    overwriteMethod: Literal["zero", "random", "dod"]
    passCount: int
    
class FileEraseRequest(BaseModel):
    targets: list[EraseTarget]
    config: EraseConfig

class DriveEraseValidateRequest(BaseModel):
    deviceId: str
    identityToken: str
    standard: str
    filesystem: str

class DriveEraseRequest(BaseModel):
    deviceId: str
    identityToken: str
    standard: str
    filesystem: str
    confirmation: Literal["CONFIRM_WIPE"]
    
def background_get_storage_worker() :
    while True:
        #DLL call
        break
    return
        
@app.route('/api/v1/devices',methods=['GET'])
def get_storage_device() :
    
    mock_caps = DeviceCapabilities(
        fileErase=True,
        driveErase=True,
        secureErase=False,
        trim=True,
        cryptoErase=False
    )
    
    device1 = StorageDevice(
        id="dev-123",
        path="\\\\.\\PhysicalDrive0", 
        model="Samsung SSD 970 EVO Plus",
        serial="S4EVNF0M812345A",
        busType="NVMe",
        capacityBytes=500107862016, 
        sectorSizeBytes=512,
        health="healthy",
        mounted=True,
        readOnly=False,
        writeProtected=False,
        identityToken="token-abc-123", 
        capabilities=mock_caps
    )
    
    devices_list = [device1]
    socketio.start_background_task(
        background_get_storage_worker
    )
    
    return jsonify([device.model_dump() for device in devices_list])


@app.route('/api/v1/file-node',methods=['GET'])
def get_file_node() :
    
    audit_log = FileNode(
        id="node-f001",
        parentId="node-d001",
        name="audit.log",
        canonicalPath="C:\\evidence\\audit.log",
        kind="file",
        sizeBytes=1048576,  
        modifiedAt="2026-09-08T10:00:00Z",
        inode=15032,
        startSector=2048,
        sectorCount=2048,
        deleted=False,
        corrupted=False,
        confidence=1.0
    )

    deleted_image = FileNode(
        id="node-f002",
        parentId="node-d001",
        name="recovered_image.jpg",
        canonicalPath="C:\\evidence\\recovered_image.jpg",
        kind="file",
        sizeBytes=345000,
        modifiedAt="2025-12-01T14:30:00Z",
        inode=15033,
        startSector=4096,
        sectorCount=674,
        deleted=True,       
        corrupted=True,     
        confidence=0.85     
    )

    evidence_folder = FileNode(
        id="node-d001",
        parentId="node-r001",
        name="evidence",
        canonicalPath="C:\\evidence",
        kind="directory",
        modifiedAt="2026-09-08T09:00:00Z",
        filesystem="NTFS",
        children=[audit_log, deleted_image] 
    )
    root_drive = FileNode(
        id="node-r001",
        name="C:",
        canonicalPath="C:\\",
        kind="drive",
        sizeBytes=500107862016,
        filesystem="NTFS",
        clusterSizeBytes=4096,
        children=[evidence_folder] 
    )
    return jsonify(root_drive.model_dump())

MOCK_DEVICES = {
    "dev-123": {
        "path": "\\\\.\\X:",  # Safe dummy drive path instead of PhysicalDrive0
        "filesystem": "exfat"
    },
    "dev-linux-test": {                                                                                                                                
        "path": "/dev/sdb1",                                                                                                                           
        "filesystem": "ext4"                                                                                                                           
    }     
}

def get_fs_driver(fs_type: str, hdd_controller):
    if not fs_type:
        raise ValueError("Filesystem type must be specified")
    
    fs_type = fs_type.lower()
    if fs_type == 'exfat':
        return exfat.ExFatDriver(hdd_controller)
    elif fs_type == 'ext4':
        return ext4.Ext4Driver(hdd_controller)
    elif fs_type == 'fat32' or fs_type == 'fat':
        return fat32.Fat32Driver(hdd_controller)
    elif fs_type == 'ntfs':
        return ntfs.NtfsDriver(hdd_controller)
    else:
        raise NotImplementedError(f"Filesystem {fs_type} is not supported yet.")

def repair_filesystem(device_path: str, fs_type: str, operation_id: str = None):
    logs = ""
    def add_log(msg):
        nonlocal logs
        print(msg)
        logs += msg + "\n"
        if operation_id and operation_id in active_operations:
            active_operations[operation_id]["repair_logs"] = logs
            socketio.emit('progress', active_operations[operation_id])

    add_log(f"[INFO] Initiating filesystem repair for {device_path} ({fs_type})")
    fs_type = fs_type.lower()
    
    if os.name == 'nt':
        # On Windows, use built-in chkdsk for supported filesystems
        if fs_type in ['ntfs', 'exfat', 'fat32']:
            # device_path is usually \\.\C:
            volume_name = device_path.replace('\\\\.\\', '')
            cmd = ['chkdsk', volume_name, '/f', '/x']
            add_log(f"[INFO] Waiting 2 seconds for OS to release volume locks...")
            import time
            time.sleep(2)
            add_log(f"[INFO] Running Windows repair command: {' '.join(cmd)}")
            try:
                # Use shell=True to ensure proper execution environment for system tools
                result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
                add_log(f"[INFO] Repair output:\n{result.stdout}")
                if result.stderr:
                    add_log(f"[ERROR] Repair errors:\n{result.stderr}")
            except Exception as e:
                add_log(f"[ERROR] Failed to run chkdsk: {e}")
        else:
            add_log(f"[INFO] No Windows repair tool configured for filesystem: {fs_type}")
        return

    # On Linux, use _externals
    externals_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '_externals')
    if not os.path.exists(externals_dir):
        add_log(f"[WARNING] _externals directory not found at {externals_dir}")
        return

    cmd = []
    if fs_type == 'ext4':
        exe = os.path.join(externals_dir, 'e2fsck')
        cmd = [exe, '-y', '-f', device_path]
    elif fs_type == 'exfat':
        exe = os.path.join(externals_dir, 'fsck.exfat')
        cmd = [exe, '-a', device_path]
    elif fs_type == 'fat32' or fs_type == 'fat':
        exe = os.path.join(externals_dir, 'fsck.fat')
        cmd = [exe, '-a', device_path]
    elif fs_type == 'ntfs':
        exe = os.path.join(externals_dir, 'ntfsfix')
        cmd = [exe, '-d', device_path] # -d clears dirty flag
    elif fs_type == 'xfs':
        exe = os.path.join(externals_dir, 'xfs_repair')
        cmd = [exe, device_path]
    else:
        add_log(f"[INFO] No Linux repair tool configured for filesystem: {fs_type}")
        return

    if not os.path.exists(exe):
        add_log(f"[WARNING] Executable not found: {exe}")
        return

    try:
        os.chmod(exe, 0o755)
    except Exception:
        pass

    add_log(f"[INFO] Running repair command: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        add_log(f"[INFO] Repair output:\n{result.stdout}")
        if result.stderr:
            add_log(f"[ERROR] Repair errors:\n{result.stderr}")
    except Exception as e:
        add_log(f"[ERROR] Failed to run repair script: {e}")

def background_file_erase_worker(operation_id: str, targets: list, config: dict):
    active_operations[operation_id] = {
        "operationId": operation_id,
        "state": "running",
        "phase": "preparing",
        "percent": 0,
        "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    }
    
    socketio.emit('progress', active_operations[operation_id])
    
    try:
        total_targets = len(targets)
        completed_targets = 0

        for target in targets:
            canonical_path = target.get('canonicalPath') or target.get('path') or ''
            if not canonical_path:
                continue

            active_operations[operation_id].update({
                "phase": "erasing",
                "percent": int((completed_targets / max(total_targets, 1)) * 100),
                "message": f"Sanitizing and erasing {canonical_path}...",
                "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
            })
            socketio.emit('progress', active_operations[operation_id])

            if os.path.exists(canonical_path):
                if os.path.isfile(canonical_path):
                    try:
                        # Overwrite file content with zeros to sanitize physical sectors
                        file_size = os.path.getsize(canonical_path)
                        with open(canonical_path, "ba+", buffering=0) as f:
                            f.seek(0)
                            remaining = file_size
                            chunk_size = 64 * 1024
                            while remaining > 0:
                                write_size = min(remaining, chunk_size)
                                f.write(b'\x00' * write_size)
                                remaining -= write_size
                            f.flush()
                            os.fsync(f.fileno())
                    except Exception as err:
                        print(f"[WARNING] Overwrite pass error on {canonical_path}: {err}")

                    try:
                        os.remove(canonical_path)
                        print(f"[SUCCESS] Permanently erased file: {canonical_path}")
                    except Exception as err:
                        print(f"[ERROR] Failed to unlink file {canonical_path}: {err}")
                        raise err
                elif os.path.isdir(canonical_path):
                    import shutil
                    # Overwrite contained files before directory deletion
                    for root, dirs, files in os.walk(canonical_path, topdown=False):
                        for name in files:
                            fpath = os.path.join(root, name)
                            try:
                                fsize = os.path.getsize(fpath)
                                with open(fpath, "ba+", buffering=0) as f:
                                    f.seek(0)
                                    remaining = fsize
                                    chunk_size = 64 * 1024
                                    while remaining > 0:
                                        wsize = min(remaining, chunk_size)
                                        f.write(b'\x00' * wsize)
                                        remaining -= wsize
                                    f.flush()
                                    os.fsync(f.fileno())
                                os.remove(fpath)
                            except Exception as fe:
                                print(f"[WARNING] Could not overwrite {fpath}: {fe}")
                                try:
                                    os.remove(fpath)
                                except Exception:
                                    pass
                        for name in dirs:
                            try:
                                os.rmdir(os.path.join(root, name))
                            except Exception:
                                pass
                    try:
                        shutil.rmtree(canonical_path, ignore_errors=True)
                        print(f"[SUCCESS] Permanently erased directory: {canonical_path}")
                    except Exception as err:
                        print(f"[ERROR] Failed to remove directory {canonical_path}: {err}")
                        raise err
            else:
                print(f"[WARNING] Target path does not exist on disk: {canonical_path}")

            completed_targets += 1

        active_operations[operation_id].update({
            "state": "completed",
            "phase": "completed",
            "percent": 100,
            "message": f"Successfully erased {completed_targets} target(s).",
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        })
        socketio.emit('progress', active_operations[operation_id])

        
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"[DEBUG] background_file_erase_worker failed: {e}")
        active_operations[operation_id].update({
            "state": "failed",
            "message": str(e),
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        })
        socketio.emit('progress', active_operations[operation_id])

@app.route('/api/v1/erase/files/validate',methods=['POST'])
def file_erase_validate() :
    try:
        validated_data = FileEraseRequest(**request.json)
        
        return jsonify({"message": "Valid request"}), 200
        
    except ValidationError as e:
        return jsonify(e.errors()), 400

file_cache = {}

@app.route('/api/v1/erase/files', methods=['POST'])
def execute_file_erase():
    print("[DEBUG] /api/v1/erase/files endpoint hit")

    idem_key = request.headers.get('Idempotency-Key')
    print(f"[DEBUG] Idempotency-Key: {idem_key}")
    
    if not idem_key:
        print("[DEBUG] Error: Idempotency-Key header missing")
        return jsonify({"error": "Idempotency-Key header is strictly required."}), 400


    if idem_key in file_cache:
        print(f"[DEBUG] Cache hit for Idempotency-Key: {idem_key}")
        return jsonify(file_cache[idem_key]), 200

    data = request.json
    print(f"[DEBUG] Request Payload: {data}")

    if data.get("confirmation") != "CONFIRM_ERASE":
        print("[DEBUG] Error: Explicit CONFIRM_ERASE string is missing")
        return jsonify({"error": "Explicit CONFIRM_ERASE string is required."}), 400

    operation_id = f"op-file-{uuid.uuid4().hex[:8]}"
    audit_id = f"audit-{uuid.uuid4().hex[:8]}"
    
    response_payload = {
        "operationId": operation_id,
        "state": "queued",
        "auditId": audit_id
    }

    file_cache[idem_key] = response_payload
    socketio.start_background_task(
        background_file_erase_worker,
        operation_id,
        data.get("targets", []),
        data.get("config", {})
    )
    return jsonify(response_payload), 202
    

def background_drive_erase_worker(operation_id: str, device_id: str, standard: str, fs_type: str):
    active_operations[operation_id] = {
        "operationId": operation_id,
        "state": "running",
        "phase": "preparing",
        "percent": 0,
        "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    }
    socketio.emit('progress', active_operations[operation_id])
    
    try:
        # Resolve device path from deviceId 
        device_info = MOCK_DEVICES.get(device_id)
        if not device_info:
            raise Exception(f"Device not found: {device_id}")
            
        device_path = device_info["path"]
        
        if os.name == 'nt':
            device = osdevice.WindowsStorageDevice()
        else:
            device = osdevice.LinuxStorageDevice()
            
        if not device.Open(device_path):
            raise Exception(f"Failed to open device {device_path}")
            
        try:
            hdd_controller = hdd.HDDController(device)
            device.LockVolume()
            device.DismountVolume()
            
            fs_driver = get_fs_driver(fs_type, hdd_controller)
            if not fs_driver.Mount():
                raise Exception(f"Failed to mount {fs_type} on {device_path}")

            v_engine = verification.VerificationEngine(hdd_controller, device)
            print(f"[INFO] Verification BEFORE wipe for {device_path}:")
            try:
                pre_report = v_engine.AuditVolumeWipe(fs_type, 0, 1000000, 8)
                pre_report.PrintTerminalReport()
            except Exception as e:
                print(f"[WARNING] Pre-wipe verification failed: {e}")

            active_operations[operation_id].update({
                "phase": "erasing",
                "percent": 50, # intermediate progress
                "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
            })
            socketio.emit('progress', active_operations[operation_id])
            
            success = fs_driver.WipeVolume()
            if not success:
                raise Exception(f"Failed to wipe volume on {device_path}")
            
            print(f"[INFO] Verification AFTER wipe for {device_path}:")
            try:
                post_report = v_engine.AuditVolumeWipe(fs_type, 0, 1000000, 8)
                post_report.PrintTerminalReport()
            except Exception as e:
                print(f"[WARNING] Post-wipe verification failed: {e}")
        finally:    
            device.Close()
            repair_filesystem(device_path, fs_type, operation_id)
            
        active_operations[operation_id].update({
            "state": "completed",
            "phase": "verifying",
            "percent": 100,
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        })
        socketio.emit('progress', active_operations[operation_id])
        
    except Exception as e:
        active_operations[operation_id].update({
            "state": "failed",
            "message": str(e),
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        })
        socketio.emit('progress', active_operations[operation_id])
        
@app.route('/api/v1/erase/drives/validate', methods=['POST'])
def drive_erase_validate() :
    try:
        validated_data = DriveEraseValidateRequest(**request.json)
        
        return jsonify({"message": "Valid request"}), 200
        
    except ValidationError as e:
        return jsonify(e.errors()), 400

drive_cache = {}
@app.route('/api/v1/erase/drives', methods=['POST'])
def execute_drive_erase():

    idem_key = request.headers.get('Idempotency-Key')
    if not idem_key:
        return jsonify({"error": "Idempotency-Key header is required"}), 400
        
    if idem_key in drive_cache:
        return jsonify(drive_cache[idem_key]), 200
    
    try:
        data = DriveEraseRequest(**request.json)
    except ValidationError as e:
        return jsonify(e.errors()), 400

    operation_id = f"op-drive-{uuid.uuid4().hex[:8]}"
    audit_id = f"audit-{uuid.uuid4().hex[:8]}"
    
    response_payload = {
        "operationId": operation_id,
        "state": "queued",
        "auditId": audit_id
    }
    
    drive_cache[idem_key] = response_payload
    socketio.start_background_task(
        background_drive_erase_worker,
        operation_id,
        data.deviceId,
        data.standard,
        data.filesystem
    )
    return jsonify(response_payload), 202

# --- Recovery API Boilerplate ---

@app.route('/api/v1/recovery/sources', methods=['POST'])
def register_recovery_source():
    data = request.json or {}
    return jsonify({
        "sourceId": f"source-{uuid.uuid4().hex[:8]}",
        "kind": data.get("kind"),
        "path": data.get("path")
    }), 201

@app.route('/api/v1/recovery/scans', methods=['POST'])
def start_recovery_scan():
    data = request.json or {}
    operation_id = f"op-scan-{uuid.uuid4().hex[:8]}"
    return jsonify({
        "operationId": operation_id,
        "state": "queued"
    }), 202

@app.route('/api/v1/recovery/scans/<operation_id>/artifacts', methods=['GET'])
def get_recovery_artifacts(operation_id):
    return jsonify([
        {
            "id": f"art-{uuid.uuid4().hex[:8]}",
            "name": "recovered_file.jpg",
            "type": "image/jpeg",
            "sizeBytes": 1024500,
            "fragments": 1,
            "confidence": 0.95,
            "confidenceNote": "Header and footer match exactly",
            "sectorOffset": 2048,
            "previewAvailable": True
        }
    ]), 200

@app.route('/api/v1/recovery/artifacts/<artifact_id>/export', methods=['POST'])
def export_recovery_artifact(artifact_id):
    return jsonify({"status": "success", "message": f"Artifact {artifact_id} exported successfully"}), 200

@app.route('/api/v1/sources/<source_id>/preview', methods=['GET'])
def preview_source(source_id):
    offset = request.args.get('offset', 0, type=int)
    length = request.args.get('length', 256, type=int)
    return jsonify({
        "sourceId": source_id,
        "offset": offset,
        "length": length,
        "bytesBase64": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
        "sha256": "dummy-hash-value"
    }), 200

@app.route('/api/v1/audit-logs', methods=['GET'])
def query_audit_logs():
    return jsonify([
        {
            "id": f"audit-{uuid.uuid4().hex[:8]}",
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            "operatorId": "sysadmin",
            "action": request.args.get('action', 'unknown'),
            "level": request.args.get('level', 'info'),
            "verified": True,
            "sha256": "dummy-hash-value",
            "signature": "dummy-signature",
            "payload": {"status": "Mocked audit log entry"}
        }
    ]), 200

@app.route('/api/v1/reports', methods=['POST'])
def generate_report():
    return jsonify({"reportId": f"report-{uuid.uuid4().hex[:8]}"}), 201

@app.route('/api/v1/reports/<report_id>/download', methods=['GET'])
def download_report(report_id):
    return jsonify({"status": "success", "message": f"Downloading report {report_id}"}), 200

if __name__ == '__main__':
    socketio.run(app, port=5000, allow_unsafe_werkzeug=True)
