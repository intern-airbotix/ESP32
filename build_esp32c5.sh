#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd -- "${PROJECT_DIR}/.." && pwd)"

IDF_DIR="${DRONEBRIDGE_IDF_PATH:-${WORKSPACE_DIR}/.toolchains/esp-idf-v5.5.5}"
IDF_TOOLS_DIR="${DRONEBRIDGE_IDF_TOOLS_PATH:-${WORKSPACE_DIR}/.toolchains/espressif-v5.5.5}"
NODE_DIR="${DRONEBRIDGE_NODE_PATH:-${WORKSPACE_DIR}/.toolchains/node-v24}"
CACHE_DIR="${WORKSPACE_DIR}/.cache"
BUILD_DIR="${PROJECT_DIR}/build-codex/esp32c5"
SDKCONFIG_FILE="${PROJECT_DIR}/build-codex/sdkconfig.esp32c5"

if [[ ! -f "${IDF_DIR}/export.sh" ]]; then
    echo "ESP-IDF was not found at ${IDF_DIR}" >&2
    exit 1
fi

if [[ ! -x "${NODE_DIR}/bin/node" ]]; then
    echo "The workspace-local Node.js toolchain was not found at ${NODE_DIR}" >&2
    exit 1
fi

mkdir -p "${CACHE_DIR}/ccache/tmp" "${CACHE_DIR}/npm" "${CACHE_DIR}/pip" "${PROJECT_DIR}/build-codex"

export PATH="${NODE_DIR}/bin:${PATH}"
export IDF_TOOLS_PATH="${IDF_TOOLS_DIR}"
export XDG_CACHE_HOME="${CACHE_DIR}"
export CCACHE_DIR="${CACHE_DIR}/ccache"
export CCACHE_TEMPDIR="${CACHE_DIR}/ccache/tmp"
export IDF_CCACHE_ENABLE=1
export PIP_CACHE_DIR="${CACHE_DIR}/pip"
export PIP_INDEX_URL="https://pypi.org/simple"
export PIP_EXTRA_INDEX_URL="https://dl.espressif.com/pypi"
export npm_config_cache="${CACHE_DIR}/npm"
export PYTHONNOUSERSITE=1

# export.sh only changes this process; it does not edit shell startup files.
source "${IDF_DIR}/export.sh"

if [[ $# -eq 0 ]]; then
    set -- build
fi

exec idf.py \
    -B "${BUILD_DIR}" \
    -DIDF_TARGET=esp32c5 \
    -DSDKCONFIG="${SDKCONFIG_FILE}" \
    -DSDKCONFIG_DEFAULTS="${PROJECT_DIR}/sdkconfig.defaults" \
    "$@"
