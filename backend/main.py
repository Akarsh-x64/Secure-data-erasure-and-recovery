from flask import Flask,jsonify,request
from flask_socketio import SocketIO
from pydantic import BaseModel,ValidationError
from typing import Literal,Optional
import uuid
import threading
import time

import sys
import os

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'modules'))

try:
    import osdevice
    import hdd
    import exfat
    import ext4
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
    else:
        raise NotImplementedError(f"Filesystem {fs_type} is not supported yet.")

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
        
        # Group targets by (drive, filesystem)
        drive_targets = {}
        for target in targets:
            canonical_path = target.get('canonicalPath', '')
            fs_type = target.get('filesystem', '')
            
            if not canonical_path or len(canonical_path) < 3 or canonical_path[1] != ':':
                raise ValueError(f"Invalid path format: {canonical_path}")
            
            drive_letter = canonical_path[:2] # e.g. "C:"
            relative_path = canonical_path[3:].replace('\\', '/') # e.g. "evidence/audit.log"
            
            group_key = (drive_letter, fs_type)
            if group_key not in drive_targets:
                drive_targets[group_key] = []
            drive_targets[group_key].append(relative_path)
            
        completed_targets = 0
        
        for (drive_letter, fs_type), rel_paths in drive_targets.items():
            device_path = f"\\\\.\\{drive_letter}"
            
            if os.name == 'nt':
                device = osdevice.WindowsStorageDevice()
            else:
                device = osdevice.LinuxStorageDevice()
                
            if not device.Open(device_path):
                raise Exception(f"Failed to open device {device_path}")
                
            hdd_controller = hdd.HDDController(device)
            device.LockVolume()
            device.DismountVolume()
            
            fs_driver = get_fs_driver(fs_type, hdd_controller)
            if not fs_driver.Mount():
                device.Close()
                raise Exception(f"Failed to mount {fs_type} on {device_path}")
            
            for rel_path in rel_paths:
                active_operations[operation_id].update({
                    "phase": "erasing",
                    "percent": int((completed_targets / total_targets) * 100),
                    "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
                })
                socketio.emit('progress', active_operations[operation_id])
                
                success = fs_driver.EraseFile(rel_path)
                if not success:
                    raise Exception(f"Failed to erase file: {rel_path} on {device_path}")
                    
                completed_targets += 1
                
            device.Close()

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

    idem_key = request.headers.get('Idempotency-Key')
    
    if not idem_key:
        return jsonify({"error": "Idempotency-Key header is strictly required."}), 400


    if idem_key in file_cache:
        return jsonify(file_cache[idem_key]), 200

    data = request.json
    if data.get("confirmation") != "CONFIRM_ERASE":
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
            
        hdd_controller = hdd.HDDController(device)
        device.LockVolume()
        device.DismountVolume()
        
        fs_driver = get_fs_driver(fs_type, hdd_controller)
        if not fs_driver.Mount():
            device.Close()
            raise Exception(f"Failed to mount {fs_type} on {device_path}")

        active_operations[operation_id].update({
            "phase": "erasing",
            "percent": 50, # intermediate progress
            "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
        })
        socketio.emit('progress', active_operations[operation_id])
        
        success = fs_driver.WipeVolume()
        if not success:
            raise Exception(f"Failed to wipe volume on {device_path}")
            
        device.Close()
            
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
        
@app.route('/api/v1/erase/drives/validate')
def drive_erase_validate() :
    try:
        validated_data = DriveEraseRequest(**request.json)
        
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

if __name__ == '__main__':
    socketio.run(app, port=5000, allow_unsafe_werkzeug=True)
