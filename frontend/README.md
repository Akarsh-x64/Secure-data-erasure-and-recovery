# Secure Data Erasure and Recovery Frontend

Electron + React + TypeScript frontend for the NTRO secure erasure and forensic recovery application.

## Backend Integration Contract

The renderer must not perform privileged filesystem or storage operations. The intended boundary is:

```text
React renderer -> typed preload API -> Electron main process -> native backend/C++ engine
```

The Electron main process owns native file dialogs, canonical paths, device identity validation, privileged operations, progress events, and audit persistence. The renderer only owns presentation state and user input.

### Transport and lifecycle

Long-running operations return an `operationId` immediately. Progress is delivered through Electron IPC events or a local WebSocket. Every operation must support these states:

```ts
type OperationState =
	| 'queued'
	| 'running'
	| 'verifying'
	| 'completed'
	| 'failed'
	| 'cancelled'
```

Shared contracts should live in a typed module consumed by preload and the renderer.

```ts
interface OperationRef {
	operationId: string
	state: OperationState
	createdAt: string
}

interface ProgressEvent {
	operationId: string
	state: OperationState
	phase: 'preparing' | 'erasing' | 'verifying' | 'scanning' | 'carving' | 'exporting'
	percent: number
	currentBytes?: number
	totalBytes?: number
	currentSector?: number
	totalSectors?: number
	passIndex?: number
	passTotal?: number
	throughputMBps?: number
	etaSeconds?: number
	message?: string
	timestamp: string
}

interface ErrorResponse {
	code: string
	message: string
	details?: Record<string, unknown>
	retryable: boolean
}
```

Use numeric byte and sector values in the API. Formatting values such as `512 GB`, `4 KB`, and hexadecimal offsets belongs in the renderer.

## Device API

```http
GET /api/v1/devices
```

```ts
interface StorageDevice {
	id: string
	path: string
	model: string
	serial?: string
	busType: 'SATA' | 'NVMe' | 'USB' | 'unknown'
	capacityBytes: number
	sectorSizeBytes: number
	health: 'healthy' | 'warning' | 'critical' | 'unknown'
	mounted: boolean
	readOnly: boolean
	writeProtected: boolean
	identityToken: string
	capabilities: {
		fileErase: boolean
		driveErase: boolean
		secureErase: boolean
		trim: boolean
		cryptoErase: boolean
	}
}
```

`identityToken` must be revalidated immediately before destructive operations. A device path alone is not a stable identity.

## Filesystem and Explorer API

The current browser `FileList` and display-only `ForensicNode` are temporary UI models. Native directory selection should return stable backend node IDs and canonical paths.

```ts
interface FileNode {
	id: string
	parentId?: string
	name: string
	canonicalPath: string
	kind: 'file' | 'directory' | 'drive'
	sizeBytes?: number
	modifiedAt?: string
	filesystem?: 'NTFS' | 'ext4' | 'exFAT' | 'FAT32' | 'unknown'
	clusterSizeBytes?: number
	inode?: number
	startSector?: number
	sectorCount?: number
	deleted?: boolean
	corrupted?: boolean
	confidence?: number
	children?: FileNode[]
}
```

The backend must never receive only a display name such as `node.name` as an erase target.

## File Erasure API

Validate before showing the final confirmation dialog:

```http
POST /api/v1/erase/files/validate
```

```json
{
	"targets": [
		{ "nodeId": "node-123", "canonicalPath": "C:\\evidence\\audit.log", "kind": "file" }
	],
	"config": {
		"clearMetadata": true,
		"wipeSlackSpace": true,
		"overwriteMethod": "zero",
		"passCount": 1
	}
}
```

Start after the user enters `CONFIRM_ERASE`:

```http
POST /api/v1/erase/files
Idempotency-Key: <client-generated-key>
```

```json
{
	"targets": [{ "nodeId": "node-123", "canonicalPath": "C:\\evidence\\audit.log" }],
	"config": {
		"clearMetadata": true,
		"wipeSlackSpace": true,
		"overwriteMethod": "zero",
		"passCount": 1
	},
	"confirmation": "CONFIRM_ERASE"
}
```

Response:

```json
{ "operationId": "op-file-123", "state": "queued", "auditId": "audit-123" }
```

## Drive Erasure API

Validate the device, mount state, privileges, write protection, and hardware capabilities immediately before execution:

```http
POST /api/v1/erase/drives/validate
```

```json
{
	"deviceId": "device-123",
	"identityToken": "identity-token",
	"standard": "nist-purge"
}
```

Start after the user enters `CONFIRM_WIPE`:

```http
POST /api/v1/erase/drives
Idempotency-Key: <client-generated-key>
```

```json
{
	"deviceId": "device-123",
	"identityToken": "identity-token",
	"standard": "nist-purge",
	"confirmation": "CONFIRM_WIPE"
}
```

The drive progress view consumes `ProgressEvent`. The backend must report verification, failure, cancellation, and actual throughput instead of relying on renderer timers.

## Recovery API

Register a disk image through a native file picker:

```http
POST /api/v1/recovery/sources
```

```json
{ "kind": "disk-image", "path": "D:\\evidence\\image.dd" }
```

Scan with the enabled signatures from the recovery UI:

```http
POST /api/v1/recovery/scans
```

```json
{
	"sourceId": "source-123",
	"signatures": [
		{
			"id": "jpeg",
			"enabled": true,
			"headerHex": "FF D8 FF",
			"footerHex": "FF D9"
		}
	]
}
```

Query results:

```http
GET /api/v1/recovery/scans/{operationId}/artifacts
```

```ts
interface RecoveredArtifact {
	id: string
	name: string
	type: string
	sizeBytes: number
	fragments: number
	confidence: number
	confidenceNote: string
	sectorOffset: number
	previewAvailable: boolean
}
```

Export an artifact:

```http
POST /api/v1/recovery/artifacts/{artifactId}/export
```

The current recovery timers and synthetic artifacts must be replaced by operation events and backend results.

## Preview API

Hex/text preview must use read-only backend data:

```http
GET /api/v1/sources/{sourceId}/preview?offset=0&length=256&format=hex
```

```json
{
	"sourceId": "source-123",
	"offset": 0,
	"length": 256,
	"bytesBase64": "AAECAwQF...",
	"sha256": "..."
}
```

Binary content should be Base64 encoded. The renderer must not generate preview bytes from node IDs.

## Audit and Reports API

Query audit records:

```http
GET /api/v1/audit-logs?action=drive-erase&level=error&verified=false&page=1&pageSize=50
```

Each record should contain `id`, `timestamp`, `operatorId`, `action`, `level`, `verified`, `sha256`, `signature`, and a structured `payload` containing the actual target/device and execution result.

Generate a report:

```http
POST /api/v1/reports
```

```json
{
	"format": "pdf",
	"auditLogIds": ["audit-123"],
	"sections": ["metadata", "hashes", "signatures", "log-trail"]
}
```

Download it with:

```http
GET /api/v1/reports/{reportId}/download
```

Audit records must be generated by the backend, not supplied by the renderer.

## Event Contract

The preload API should expose subscriptions instead of making React poll:

```ts
type BackendEvent =
	| { type: 'operation.progress'; data: ProgressEvent }
	| { type: 'operation.completed'; data: OperationRef }
	| { type: 'operation.failed'; data: ErrorResponse & { operationId: string } }
	| { type: 'device.changed'; data: StorageDevice }

interface NativeApi {
	devices: {
		list(): Promise<StorageDevice[]>
	}
	erase: {
		validateFile(request: unknown): Promise<unknown>
		startFile(request: unknown): Promise<OperationRef>
		validateDrive(request: unknown): Promise<unknown>
		startDrive(request: unknown): Promise<OperationRef>
		cancel(operationId: string): Promise<void>
	}
	events: {
		subscribe(listener: (event: BackendEvent) => void): () => void
	}
}
```

The existing `window.api.eraseFiles(request: unknown)` should be replaced with typed domain methods. Backend errors should use stable error codes such as `DEVICE_NOT_FOUND`, `DEVICE_MOUNTED`, `ACCESS_DENIED`, `WRITE_PROTECTED`, `UNSUPPORTED_HARDWARE`, and `OPERATION_CONFLICT`.

## Recommended IDE Setup

- [VSCode](https://code.visualstudio.com/) + [ESLint](https://marketplace.visualstudio.com/items?itemName=dbaeumer.vscode-eslint) + [Prettier](https://marketplace.visualstudio.com/items?itemName=esbenp.prettier-vscode)

## Project Setup

### Install

```bash
$ npm install
```

### Development

```bash
$ npm run dev
```

### Build

```bash
# For windows
$ npm run build:win

# For macOS
$ npm run build:mac

# For Linux
$ npm run build:linux
```
