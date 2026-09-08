from flask import Flask,jsonify,request
from pydantic import BaseModel,ValidationError
from typing import Literal,Optional
import uuid
import threading

app = Flask(__name__)

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

class EraseConfig(BaseModel):
    clearMetadata: bool
    wipeSlackSpace: bool
    overwriteMethod: Literal["zero", "random", "dod"]
    passCount: int
    
class FileEraseRequest(BaseModel):
    targets: list[EraseTarget]
    config: EraseConfig

class DriveEraseRequest(BaseModel):
    deviceID: str
    config: EraseConfig

class DriveEraseValidateRequest(BaseModel):
    deviceId: str
    identityToken: str
    standard: str

class DriveEraseRequest(BaseModel):
    deviceId: str
    identityToken: str
    standard: str
    confirmation: Literal["CONFIRM_WIPE"]
    
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
    return jsonify(response_payload), 202
    
@app.route('/api/v1/erase/drives/validate')
def drive_erase_validate() :
    try:
        validated_data = FileEraseRequest(**request.json)
        
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
        return jsonify(file_cache[idem_key]), 200
    
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
    return jsonify(response_payload), 202

if __name__ == '__main__':
    app.run(port=5000)
