#!/usr/bin/env bash
# hwbench universal entrypoint — Linux / macOS / WSL2 / Termux
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
RELAY_URL="https://hwbench-relay.example.com/submit"
HWBENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$HWBENCH_DIR/benchmark/build"
RESULTS_DIR="$HWBENCH_DIR/results"
MIN_CMAKE_MAJOR=3
MIN_CMAKE_MINOR=16
# ──────────────────────────────────────────────────────────────────────────────

# ---- color helpers -----------------------------------------------------------
if [ -t 1 ]; then
    RED='\033[0;31m'; YEL='\033[0;33m'; GRN='\033[0;32m'
    CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'
else
    RED=''; YEL=''; GRN=''; CYN=''; BLD=''; RST=''
fi
info()  { printf "${CYN}[hwbench]${RST} %s\n" "$*"; }
ok()    { printf "${GRN}[  OK  ]${RST} %s\n" "$*"; }
warn()  { printf "${YEL}[ WARN ]${RST} %s\n" "$*"; }
die()   { printf "${RED}[ERROR ]${RST} %s\n" "$*" >&2; exit 1; }

# ---- platform detection ------------------------------------------------------
detect_platform() {
    if [ -d "/data/data/com.termux" ] || [ -n "${PREFIX:-}" ]; then
        echo "termux"
    elif [ "$(uname -s)" = "Darwin" ]; then
        echo "macos"
    elif [ "$(uname -s)" = "Linux" ]; then
        echo "linux"
    else
        echo "unknown"
    fi
}

PLATFORM=$(detect_platform)
info "Detected platform: $PLATFORM"

# ---- package manager helpers -------------------------------------------------
install_pkg_linux() {
    local pkg="$1"
    if command -v apt-get &>/dev/null; then
        info "Installing $pkg via apt-get..."
        sudo apt-get install -y "$pkg"
    elif command -v dnf &>/dev/null; then
        info "Installing $pkg via dnf..."
        sudo dnf install -y "$pkg"
    elif command -v yum &>/dev/null; then
        info "Installing $pkg via yum..."
        sudo yum install -y "$pkg"
    elif command -v pacman &>/dev/null; then
        info "Installing $pkg via pacman..."
        sudo pacman -S --noconfirm "$pkg"
    else
        die "No supported package manager found. Please install $pkg manually."
    fi
}

install_build_tools() {
    case "$PLATFORM" in
        linux)
            if ! command -v gcc &>/dev/null && ! command -v clang &>/dev/null; then
                if command -v apt-get &>/dev/null; then
                    sudo apt-get install -y build-essential
                elif command -v dnf &>/dev/null; then
                    sudo dnf install -y gcc make
                else
                    install_pkg_linux gcc
                fi
            fi
            ;;
        macos)
            if ! command -v gcc &>/dev/null && ! command -v clang &>/dev/null; then
                warn "Xcode Command Line Tools not found."
                info "Running: xcode-select --install"
                xcode-select --install 2>/dev/null || true
                info "Please complete the Xcode CLT installation and re-run this script."
                exit 1
            fi
            ;;
        termux)
            if ! command -v clang &>/dev/null; then
                pkg install -y clang
            fi
            ;;
    esac
}

install_cmake() {
    case "$PLATFORM" in
        linux)
            install_pkg_linux cmake
            ;;
        macos)
            if command -v brew &>/dev/null; then
                brew install cmake
            else
                warn "Homebrew not found. Attempting to download cmake binary..."
                local cmake_url="https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-macos-universal.tar.gz"
                local tmp_dir
                tmp_dir=$(mktemp -d)
                curl -L "$cmake_url" -o "$tmp_dir/cmake.tar.gz"
                tar -xzf "$tmp_dir/cmake.tar.gz" -C "$tmp_dir"
                sudo cp -r "$tmp_dir"/cmake-*/CMake.app/Contents/bin/cmake /usr/local/bin/cmake
                rm -rf "$tmp_dir"
            fi
            ;;
        termux)
            pkg install -y cmake
            ;;
    esac
}

install_python() {
    case "$PLATFORM" in
        linux)
            if command -v apt-get &>/dev/null; then
                sudo apt-get install -y python3
            elif command -v dnf &>/dev/null; then
                sudo dnf install -y python3
            else
                install_pkg_linux python3
            fi
            ;;
        macos)
            if command -v brew &>/dev/null; then
                brew install python3
            else
                warn "Python 3 not found. Please install from https://python.org"
            fi
            ;;
        termux)
            pkg install -y python
            ;;
    esac
}

# ---- check cmake version -----------------------------------------------------
check_cmake_version() {
    if ! command -v cmake &>/dev/null; then
        return 1
    fi
    local ver
    ver=$(cmake --version | head -1 | awk '{print $3}')
    local major minor
    major=$(echo "$ver" | cut -d. -f1)
    minor=$(echo "$ver" | cut -d. -f2)
    if [ "$major" -lt "$MIN_CMAKE_MAJOR" ] || \
       { [ "$major" -eq "$MIN_CMAKE_MAJOR" ] && [ "$minor" -lt "$MIN_CMAKE_MINOR" ]; }; then
        warn "cmake $ver is too old (need >= ${MIN_CMAKE_MAJOR}.${MIN_CMAKE_MINOR})"
        info "Please update cmake:"
        case "$PLATFORM" in
            linux)
                info "  sudo apt-get install --only-upgrade cmake"
                info "  Or: pip install cmake"
                ;;
            macos)
                info "  brew upgrade cmake"
                ;;
            termux)
                info "  pkg upgrade cmake"
                ;;
        esac
        return 1
    fi
    return 0
}

# ---- ensure tools exist ------------------------------------------------------
info "Checking build tools..."

install_build_tools

if ! check_cmake_version; then
    info "Installing/updating cmake..."
    install_cmake
    if ! check_cmake_version; then
        die "cmake >= ${MIN_CMAKE_MAJOR}.${MIN_CMAKE_MINOR} is required but could not be installed."
    fi
fi
ok "cmake $(cmake --version | head -1 | awk '{print $3}') found"

if ! command -v python3 &>/dev/null; then
    warn "python3 not found — Python benchmark module will be skipped."
    info "Installing python3..."
    install_python
fi

# ---- device name prompt ------------------------------------------------------
printf "\n${BLD}Enter a name for this device${RST} (e.g. 'RaspberryPi4-Home', 'MacBookPro-M3'):\n"
printf "Device name: "
read -r DEVICE_NAME

# Replace spaces with hyphens, strip special chars
DEVICE_NAME=$(echo "$DEVICE_NAME" | tr ' ' '-' | tr -cd '[:alnum:]-_')
if [ -z "$DEVICE_NAME" ]; then
    DEVICE_NAME="unnamed-device"
fi
ok "Device name: $DEVICE_NAME"

# ---- extra flags from user ---------------------------------------------------
EXTRA_FLAGS=""
printf "\nRun with defaults (CPU: 30s, other modules: 10s)? [Y/n]: "
read -r ans_dur
if [[ "$ans_dur" =~ ^[Nn]$ ]]; then
    printf "Seconds for CPU modules (default 30): "
    read -r CPU_DUR
    CPU_DUR=$(echo "$CPU_DUR" | tr -cd '0-9')
    [ -n "$CPU_DUR" ] && EXTRA_FLAGS="$EXTRA_FLAGS --cpu-duration $CPU_DUR"

    printf "Seconds for other modules (default 10): "
    read -r DUR
    DUR=$(echo "$DUR" | tr -cd '0-9')
    [ -n "$DUR" ] && EXTRA_FLAGS="$EXTRA_FLAGS --duration $DUR"
fi

printf "Override storage benchmark path? (leave blank for /tmp): "
read -r STOR_PATH
if [ -n "$STOR_PATH" ]; then
    EXTRA_FLAGS="$EXTRA_FLAGS --storage-path $STOR_PATH"
fi

printf "Write CSV row in addition to JSON? [y/N]: "
read -r ans_csv
[[ "$ans_csv" =~ ^[Yy]$ ]] && EXTRA_FLAGS="$EXTRA_FLAGS --csv"

# ---- build -------------------------------------------------------------------
info "Building hwbench..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_OPTS="-DCMAKE_BUILD_TYPE=Release"

cmake "$HWBENCH_DIR/benchmark" $CMAKE_OPTS 2>&1 | tail -5

NPROC=1
if command -v nproc &>/dev/null; then
    NPROC=$(nproc)
elif command -v sysctl &>/dev/null; then
    NPROC=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 1)
fi

make -j"$NPROC" 2>&1 | tail -3
ok "Build complete"

# ---- create results dir inside project root ----------------------------------
mkdir -p "$RESULTS_DIR"

# ---- run benchmark -----------------------------------------------------------
cd "$HWBENCH_DIR"
info "Starting benchmark for device: $DEVICE_NAME"
printf "\n"

# Run the binary from project root so relative 'results/' path works
"$BUILD_DIR/hwbench" "$DEVICE_NAME" $EXTRA_FLAGS

# Find the most recently written JSON result
RESULT_JSON=$(ls -t "$RESULTS_DIR"/benchmark_results_*.json 2>/dev/null | head -1)

if [ -z "$RESULT_JSON" ]; then
    # Also check build directory results (if run from there previously)
    RESULT_JSON=$(ls -t "$BUILD_DIR/results"/benchmark_results_*.json 2>/dev/null | head -1 || true)
fi

# ---- upload offer ------------------------------------------------------------
if [ -n "$RESULT_JSON" ]; then
    printf "\n${BLD}Upload results to hwbench leaderboard?${RST} [y/N]: "
    read -r ans_upload
    if [[ "$ans_upload" =~ ^[Yy]$ ]]; then
        if command -v curl &>/dev/null; then
            info "Uploading to $RELAY_URL ..."
            HTTP_CODE=$(curl -s -o /tmp/hwbench_upload_resp.json -w "%{http_code}" \
                -X POST "$RELAY_URL" \
                -H "Content-Type: application/json" \
                -d "@$RESULT_JSON")
            if [ "$HTTP_CODE" = "200" ]; then
                ok "Upload successful"
                cat /tmp/hwbench_upload_resp.json 2>/dev/null && printf "\n"
            else
                warn "Upload failed (HTTP $HTTP_CODE). Results are still saved locally."
            fi
        elif command -v python3 &>/dev/null; then
            info "Uploading via python3..."
            python3 -c "
import urllib.request, json, sys
with open('$RESULT_JSON') as f:
    data = f.read().encode()
req = urllib.request.Request('$RELAY_URL',
    data=data, method='POST',
    headers={'Content-Type': 'application/json'})
try:
    resp = urllib.request.urlopen(req, timeout=30)
    print(resp.read().decode())
except Exception as e:
    print(f'Upload failed: {e}', file=sys.stderr)
    sys.exit(1)
"
        else
            warn "Neither curl nor python3 available for upload."
        fi
    fi

    printf "\n${GRN}${BLD}Results saved:${RST} $RESULT_JSON\n\n"
fi
