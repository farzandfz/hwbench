"""
hwbench relay server — FastAPI
Accepts benchmark JSON submissions, saves locally, and uploads to Google Drive.
"""
import os
import json
import datetime
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

# Google Drive integration
try:
    from google.oauth2 import service_account
    from googleapiclient.discovery import build
    from googleapiclient.http import MediaIoBaseUpload
    import io
    GDRIVE_AVAILABLE = True
except ImportError:
    GDRIVE_AVAILABLE = False

VERSION = "1.0.0"
RESULTS_DIR = Path("results")
RESULTS_DIR.mkdir(exist_ok=True)

app = FastAPI(title="hwbench relay", version=VERSION)


def _get_drive_service():
    """Build and return a Google Drive service client, or None if not configured."""
    if not GDRIVE_AVAILABLE:
        return None
    
    # Try env var content first (for Railway / cloud deployments)
    creds_content = os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT")
    if creds_content:
        creds_info = json.loads(creds_content)
        creds = service_account.Credentials.from_service_account_info(
            creds_info,
            scopes=["https://www.googleapis.com/auth/drive.file"],
        )
        return build("drive", "v3", credentials=creds, cache_discovery=False)
    
    # Fall back to file path (for local development)
    key_path = os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON")
    if not key_path or not Path(key_path).exists():
        return None
    creds = service_account.Credentials.from_service_account_file(
        key_path,
        scopes=["https://www.googleapis.com/auth/drive.file"],
    )
    return build("drive", "v3", credentials=creds, cache_discovery=False)


def _upload_to_drive(service, folder_id: str, filename: str, content: bytes) -> str:
    """Upload content to Google Drive folder; returns file_id."""
    file_metadata = {
        "name": filename,
        "parents": [folder_id],
    }
    media = MediaIoBaseUpload(
        io.BytesIO(content),
        mimetype="application/json",
        resumable=False,
    )
    resp = (
        service.files()
        .create(body=file_metadata, media_body=media, fields="id")
        .execute()
    )
    return resp.get("id", "")


@app.get("/health")
async def health():
    folder_id = os.environ.get("GDRIVE_FOLDER_ID", "")
    has_creds = bool(
        os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT")
        or os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON")
    )
    return {
        "status": "ok",
        "version": VERSION,
        "gdrive_available": GDRIVE_AVAILABLE,
        "gdrive_folder_configured": bool(folder_id),
        "gdrive_creds_configured": has_creds,
    }


@app.post("/submit")
async def submit(request: Request):
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid JSON body")

    # Validate required fields
    meta = body.get("meta", {})
    if not meta.get("device_name"):
        raise HTTPException(status_code=422, detail="Missing meta.device_name")
    if not meta.get("timestamp"):
        raise HTTPException(status_code=422, detail="Missing meta.timestamp")
    if "results" not in body:
        raise HTTPException(status_code=422, detail="Missing results field")

    device_name = meta["device_name"]
    timestamp   = meta["timestamp"].replace(":", "-").replace(" ", "T")
    filename    = f"hwbench_{device_name}_{timestamp}.json"
    content     = json.dumps(body, indent=2).encode("utf-8")

    # Save locally
    local_path = RESULTS_DIR / filename
    local_path.write_bytes(content)

    # Upload to Google Drive
    file_id  = ""
    gdrive_ok = False
    folder_id = os.environ.get("GDRIVE_FOLDER_ID", "")

    if folder_id:
        try:
            service = _get_drive_service()
            if service:
                file_id = _upload_to_drive(service, folder_id, filename, content)
                gdrive_ok = True
            else:
                print(f"GDrive upload skipped: service=None (gdrive_available={GDRIVE_AVAILABLE}, "
                      f"has_content_env={bool(os.environ.get('GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT'))}, "
                      f"has_file_env={bool(os.environ.get('GOOGLE_SERVICE_ACCOUNT_JSON'))})")
        except Exception as exc:
            print(f"GDrive upload failed: {exc}")
    else:
        print("GDrive upload skipped: GDRIVE_FOLDER_ID not set")

    return JSONResponse({
        "status": "ok",
        "file_id": file_id,
        "gdrive_uploaded": gdrive_ok,
        "local_path": str(local_path),
        "message": "Results uploaded" if gdrive_ok else "Results saved locally",
    })


@app.get("/results")
async def list_results():
    """List files in the Google Drive folder (falls back to local listing)."""
    folder_id = os.environ.get("GDRIVE_FOLDER_ID", "")
    if folder_id:
        try:
            service = _get_drive_service()
            if service:
                query = f"'{folder_id}' in parents and trashed=false"
                resp = (
                    service.files()
                    .list(
                        q=query,
                        fields="files(id,name,createdTime,description)",
                        orderBy="createdTime desc",
                        pageSize=100,
                    )
                    .execute()
                )
                files = []
                for f in resp.get("files", []):
                    # device name is encoded in the filename: hwbench_<device>_<ts>.json
                    name = f.get("name", "")
                    parts = name.replace(".json", "").split("_", 2)
                    device = parts[1] if len(parts) > 1 else "unknown"
                    files.append({
                        "name": name,
                        "file_id": f.get("id"),
                        "uploaded_at": f.get("createdTime"),
                        "device_name": device,
                    })
                return files
        except Exception as exc:
            print(f"GDrive listing failed: {exc}")

    # Local fallback
    files = []
    for p in sorted(RESULTS_DIR.glob("hwbench_*.json"), reverse=True)[:100]:
        stat = p.stat()
        parts = p.stem.split("_", 2)
        device = parts[1] if len(parts) > 1 else "unknown"
        files.append({
            "name": p.name,
            "file_id": None,
            "uploaded_at": datetime.datetime.fromtimestamp(
                stat.st_mtime, tz=datetime.timezone.utc
            ).isoformat(),
            "device_name": device,
        })
    return files
