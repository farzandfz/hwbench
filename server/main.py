"""
hwbench relay server — FastAPI
Accepts benchmark JSON submissions and appends rows to a Google Sheet leaderboard.
"""
import os
import json
import datetime
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

try:
    from google.oauth2 import service_account
    from googleapiclient.discovery import build
    GAPI_AVAILABLE = True
except ImportError:
    GAPI_AVAILABLE = False

VERSION = "1.0.0"
RESULTS_DIR = Path("results")
RESULTS_DIR.mkdir(exist_ok=True)

app = FastAPI(title="hwbench relay", version=VERSION)

SHEET_HEADERS = [
    "timestamp", "device_name", "hostname", "os", "arch", "cpu_model",
    "cpu_cores_physical", "cpu_cores_logical", "ram_total_mb",
    # cpu single (millions iter/s)
    "cpu_dep_ips_M", "cpu_thr_ips_M", "cpu_mem_ips_M",
    # cpu multi
    "cpu_dep_ips_multi_M", "cpu_thr_ips_multi_M", "cpu_mem_ips_multi_M",
    "cpu_thr_scaling", "cpu_thr_efficiency_pct",
    # memory
    "mem_write_gbps", "mem_read_gbps", "mem_memcpy_gbps", "mem_latency_ns",
    # storage
    "stor_seq_write_mbps", "stor_seq_read_mbps",
    "stor_rand4k_write_iops", "stor_rand4k_read_iops", "stor_file_create_ps",
    # compression
    "comp_compress_mbps", "comp_decompress_mbps", "comp_ratio",
    # hashing
    "hash_hashes_ps", "hash_mbps",
    # floating point
    "fp_gflops", "fp_ips_M",
    # thread bench
    "thr_create_us", "thr_mutex_ops_ps", "thr_condvar_ps",
    # fs metadata
    "fsmeta_create_ps", "fsmeta_stat_ps", "fsmeta_delete_ps",
    # python
    "py_version", "py_startup_ms", "py_sc_ips", "py_mc_ips", "py_mc_scaling",
]


def _get_sheets_service():
    if not GAPI_AVAILABLE:
        print("Sheets: google-api-python-client not installed")
        return None
    creds_content = os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT")
    if not creds_content:
        print("Sheets: GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT not set")
        return None
    creds_info = json.loads(creds_content)
    creds = service_account.Credentials.from_service_account_info(
        creds_info,
        scopes=["https://www.googleapis.com/auth/spreadsheets"],
    )
    return build("sheets", "v4", credentials=creds, cache_discovery=False)


def _fmt(v, scale=1.0, decimals=3):
    """Scale and round a numeric value; return empty string if absent."""
    if v is None or v == "":
        return ""
    try:
        return round(float(v) * scale, decimals)
    except (ValueError, TypeError):
        return ""


def _build_row(body: dict) -> list:
    meta = body.get("meta", {})
    sys  = body.get("system", {})
    res  = body.get("results", {})

    cs   = res.get("cpu_single", {})
    cm   = res.get("cpu_multi", {})
    mem  = res.get("memory", {})
    stor = res.get("storage", {})
    comp = res.get("compression", {})
    hsh  = res.get("hashing", {})
    fp   = res.get("floating_point", {})
    thr  = res.get("thread_bench", {})
    fsm  = res.get("fs_metadata", {})
    py   = res.get("python", {})

    os_str = f"{sys.get('os_name', '')} {sys.get('os_version', '')}".strip()

    stor_skipped = stor.get("skipped", False)
    comp_skipped = comp.get("skipped", False)
    py_skipped   = py.get("skipped", False)
    stor_tmpfs   = stor.get("is_tmpfs", False)

    return [
        meta.get("timestamp", ""),
        meta.get("device_name", ""),
        sys.get("hostname", ""),
        os_str,
        sys.get("architecture", ""),
        sys.get("cpu_model", ""),
        sys.get("cpu_physical_cores", ""),
        sys.get("cpu_logical_cores", ""),
        sys.get("ram_total_mb", ""),
        # cpu single
        _fmt(cs.get("dependent_chain_ips"),      scale=1e-6),
        _fmt(cs.get("independent_throughput_ips"), scale=1e-6),
        _fmt(cs.get("memory_bound_ips"),          scale=1e-6),
        # cpu multi
        _fmt(cm.get("dependent_chain_ips"),       scale=1e-6),
        _fmt(cm.get("independent_throughput_ips"), scale=1e-6),
        _fmt(cm.get("memory_bound_ips"),          scale=1e-6),
        _fmt(cm.get("thr_scaling"),               decimals=2),
        _fmt(cm.get("thr_efficiency_percent"),    decimals=1),
        # memory
        _fmt(mem.get("sequential_write_gbps"), decimals=3),
        _fmt(mem.get("sequential_read_gbps"),  decimals=3),
        _fmt(mem.get("memcpy_gbps"),           decimals=3),
        _fmt(mem.get("latency_ns"),            decimals=1),
        # storage
        "" if stor_skipped else _fmt(stor.get("sequential_write_mbps"), decimals=1),
        "" if stor_skipped else _fmt(stor.get("sequential_read_mbps"),  decimals=1),
        "" if stor_skipped or stor_tmpfs else _fmt(stor.get("random_4k_write_iops"), decimals=0),
        "" if stor_skipped or stor_tmpfs else _fmt(stor.get("random_4k_read_iops"),  decimals=0),
        "" if stor_skipped else _fmt(stor.get("file_create_per_sec"), decimals=0),
        # compression
        "" if comp_skipped else _fmt(comp.get("compress_mbps"),    decimals=1),
        "" if comp_skipped else _fmt(comp.get("decompress_mbps"),  decimals=1),
        "" if comp_skipped else _fmt(comp.get("compression_ratio"), decimals=4),
        # hashing
        _fmt(hsh.get("hashes_per_sec"),  decimals=1),
        _fmt(hsh.get("throughput_mbps"), decimals=1),
        # floating point
        _fmt(fp.get("flop_estimate_per_sec"), scale=1e-9, decimals=3),
        _fmt(fp.get("iterations_per_sec"),    scale=1e-6, decimals=3),
        # thread bench
        _fmt(thr.get("thread_create_latency_us"), decimals=1),
        _fmt(thr.get("mutex_ops_per_sec"),         decimals=0),
        _fmt(thr.get("condvar_msgs_per_sec"),       decimals=0),
        # fs metadata
        _fmt(fsm.get("create_per_sec"), decimals=0),
        _fmt(fsm.get("stat_per_sec"),   decimals=0),
        _fmt(fsm.get("delete_per_sec"), decimals=0),
        # python
        "" if py_skipped else py.get("python_version", ""),
        "" if py_skipped else _fmt(py.get("startup_time_ms"),                  decimals=1),
        "" if py_skipped else _fmt(py.get("single_core_iterations_per_sec"),   decimals=0),
        "" if py_skipped else _fmt(py.get("multi_core_iterations_per_sec"),    decimals=0),
        "" if py_skipped else _fmt(py.get("multi_core_scaling_factor"),        decimals=2),
    ]


def _append_to_sheet(service, sheet_id: str, tab: str, row: list):
    sheets = service.spreadsheets()

    # Check if sheet already has rows
    existing = sheets.values().get(
        spreadsheetId=sheet_id,
        range=f"'{tab}'!A1:A2",
    ).execute().get("values", [])

    if len(existing) == 0:
        # Empty — write headers + first data row together
        sheets.values().update(
            spreadsheetId=sheet_id,
            range=f"'{tab}'!A1",
            valueInputOption="RAW",
            body={"values": [SHEET_HEADERS, row]},
        ).execute()
    else:
        # Headers present — append a new row
        sheets.values().append(
            spreadsheetId=sheet_id,
            range=f"'{tab}'!A1",
            valueInputOption="RAW",
            insertDataOption="INSERT_ROWS",
            body={"values": [row]},
        ).execute()


@app.get("/health")
async def health():
    sheet_id = os.environ.get("GSHEET_ID", "")
    tab      = os.environ.get("GSHEET_TAB", "")
    has_creds = bool(os.environ.get("GOOGLE_SERVICE_ACCOUNT_JSON_CONTENT"))
    return {
        "status": "ok",
        "version": VERSION,
        "gapi_available": GAPI_AVAILABLE,
        "sheet_configured": bool(sheet_id),
        "tab_configured": bool(tab),
        "creds_configured": has_creds,
    }


@app.post("/submit")
async def submit(request: Request):
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid JSON body")

    meta = body.get("meta", {})
    if not meta.get("device_name"):
        raise HTTPException(status_code=422, detail="Missing meta.device_name")
    if not meta.get("timestamp"):
        raise HTTPException(status_code=422, detail="Missing meta.timestamp")
    if "results" not in body:
        raise HTTPException(status_code=422, detail="Missing results field")

    sheet_ok = False
    sheet_id = os.environ.get("GSHEET_ID", "")
    tab      = os.environ.get("GSHEET_TAB", "Sheet1")

    if sheet_id:
        try:
            service = _get_sheets_service()
            if service:
                row = _build_row(body)
                _append_to_sheet(service, sheet_id, tab, row)
                sheet_ok = True
            else:
                print("Sheets upload skipped: could not build service client")
        except Exception as exc:
            print(f"Sheets upload failed: {exc}")
    else:
        print("Sheets upload skipped: GSHEET_ID not set")

    return JSONResponse({
        "status": "ok",
        "sheet_uploaded": sheet_ok,
        "message": "Results added to leaderboard" if sheet_ok else "Sheet upload failed — check server logs",
    })


@app.get("/results")
async def list_results():
    """List rows from the Google Sheet leaderboard."""
    sheet_id = os.environ.get("GSHEET_ID", "")
    tab      = os.environ.get("GSHEET_TAB", "Sheet1")

    if not sheet_id:
        return {"error": "GSHEET_ID not configured"}

    try:
        service = _get_sheets_service()
        if not service:
            return {"error": "Could not connect to Sheets API"}

        result = service.spreadsheets().values().get(
            spreadsheetId=sheet_id,
            range=f"'{tab}'!A1:AZ",
        ).execute()

        values = result.get("values", [])
        if len(values) < 2:
            return []

        headers = values[0]
        rows = []
        for row in values[1:]:
            # Pad short rows so zip works cleanly
            padded = row + [""] * (len(headers) - len(row))
            rows.append(dict(zip(headers, padded)))
        return rows

    except Exception as exc:
        print(f"Sheets listing failed: {exc}")
        return {"error": str(exc)}
