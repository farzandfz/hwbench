# hwbench — Cross-Platform Hardware Benchmark Suite

A self-contained benchmark that runs on Linux x86_64, Linux aarch64, macOS (Intel & Apple Silicon), WSL2, and Android (Termux). Single command to clone, build, and run.

## Quick Start

```bash
git clone https://github.com/yourname/hwbench
cd hwbench
bash run.sh
```

That's it. `run.sh` auto-installs dependencies (cmake, gcc/clang, python3) and builds the benchmark before running it.

---

## Benchmark Modules

| # | Module | Description | Key Metrics |
|---|--------|-------------|-------------|
| 1 | **CPU Single-Core** | Register-only integer workload (XOR, multiply, shift). No memory access. | iterations/sec |
| 2 | **CPU Multi-Core** | Same workload on all logical cores via pthreads. | total iters/sec, scaling factor, efficiency % |
| 3 | **Memory** | Sequential read/write, memcpy throughput, pointer-chasing latency. | GB/s, latency ns |
| 4 | **Storage** | Sequential read/write (512 MB), random 4K I/O, file creation rate. | MB/s, IOPS, files/sec |
| 5 | **Compression** | Inline LZ77 compress + decompress on 16 MB deterministic block. | MB/s, compression ratio |
| 6 | **SHA-256 Hashing** | Self-contained SHA-256 (FIPS 180-4) on 1 MB block. | hashes/sec, MB/s |
| 7 | **Floating Point** | Register-only double FP workload (mul/add/div/sqrt). | GFLOPs, iterations/sec |
| 8 | **Thread Primitives** | Thread creation latency, mutex contention, condition variable throughput. | µs, ops/sec |
| 9 | **FS Metadata** | create / stat / unlink 10,000 files. | files/sec |
| 10 | **Python Benchmark** | Same integer loop in Python 3 (single + multiprocessing). | iterations/sec, scaling |

---

## CLI Flags

```
hwbench <device_name> [options]

  --duration N        Seconds per module (default: 10)
  --storage-path PATH Override path for storage benchmark (default: /tmp)
  --csv               Append a CSV row to results/benchmark_results.csv
  --skip MODULE       Skip a module by name:
                        cpu_single | cpu_multi | memory | storage |
                        compression | hashing | float | threads | fsmeta | python
  --no-color          Disable ANSI color output
  --help              Show usage
```

---

## Output

### JSON

Each run produces `results/benchmark_results_YYYY-MM-DDTHH-MM-SS.json`:

```json
{
  "meta": {
    "hwbench_version": "1.0.0",
    "device_name": "RaspberryPi4-Home",
    "hostname": "raspberrypi",
    "timestamp": "2026-06-26T18:35:12Z",
    "duration_per_module_sec": 10
  },
  "system": {
    "os_name": "Linux",
    "cpu_model": "Cortex-A72",
    "cpu_physical_cores": 4,
    "cpu_logical_cores": 4,
    "ram_total_mb": 3842,
    ...
  },
  "results": {
    "cpu_single":    { "iterations_per_sec": 847234100, ... },
    "cpu_multi":     { "scaling_factor": 3.91, "efficiency_percent": 97.8, ... },
    "memory":        { "sequential_write_gbps": 7.4, "latency_ns": 94.3, ... },
    "storage":       { "sequential_write_mbps": 410, "random_4k_write_iops": 8200, ... },
    "compression":   { "compress_mbps": 42.1, "decompress_mbps": 280.5, ... },
    "hashing":       { "hashes_per_sec": 241, "throughput_mbps": 241, ... },
    "floating_point":{ "flop_estimate_per_sec": 1.02e9, ... },
    "thread_bench":  { "thread_create_latency_us": 17.9, "mutex_ops_per_sec": 5.1e7, ... },
    "fs_metadata":   { "create_per_sec": 16087, "stat_per_sec": 482928, ... },
    "python":        { "single_core_iterations_per_sec": 12500000, ... }
  }
}
```

### CSV (with `--csv`)

Appended to `results/benchmark_results.csv`. Headers written automatically on first run if the file doesn't exist.

---

## Relay Server

The `server/` directory contains a FastAPI server that accepts `POST /submit` with the JSON result body and uploads to Google Drive.

See [server/README_DEPLOY.md](server/README_DEPLOY.md) for full deployment instructions on Railway and Render (both have free tiers).

After deploying, update the `RELAY_URL` variable at the top of `run.sh`.

### Server endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET`  | `/health` | Health check |
| `POST` | `/submit` | Upload a benchmark result JSON |
| `GET`  | `/results` | List submitted results |

### Local server test

```bash
cd server/
pip install -r requirements.txt
uvicorn main:app --host 127.0.0.1 --port 8000 --reload

# In another terminal:
curl http://localhost:8000/health
curl -X POST http://localhost:8000/submit \
     -H "Content-Type: application/json" \
     -d @results/benchmark_results_*.json
```

---

## Platform Notes

| Platform | Compiler | Notes |
|----------|----------|-------|
| Linux x86_64 / aarch64 | gcc or clang | Full support |
| macOS Intel / Apple Silicon | AppleClang (Xcode CLT) | Full support |
| WSL2 | gcc (Linux) | Full support |
| Android Termux | clang | Full support; storage bench uses internal storage |
| Windows (native) | Not supported | Use WSL2 |

Storage benchmark warns automatically if `/tmp` is tmpfs (RAM-backed). Use `--storage-path /your/real/disk` to measure actual disk speed.

---

## Contributing Results

1. Run `bash run.sh`
2. When prompted, answer `y` to upload.
3. Your result appears at `GET /results` on the relay server.

To contribute to the project, open a PR — all platforms welcome.
