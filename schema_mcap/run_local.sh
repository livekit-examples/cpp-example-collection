#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
INVOCATION_DIR="$(pwd)"

build_dir="build"
output_dir="mcap"
url="${LIVEKIT_URL:-ws://localhost:7880}"
api_key="${LIVEKIT_API_KEY:-devkey}"
api_secret="${LIVEKIT_API_SECRET:-secret}"
room="schema-mcap"
publisher_identity="schema-publisher"
recorder_identity="schema-recorder"
frames="80"
frames_explicit="false"
pointcloud_sequence_path="${SCRIPT_DIR}/data/pandaset_sample"
skip_build="false"

usage() {
  cat <<EOF
Usage:
  $0 [options]

Options:
  --url <url>                    LiveKit URL (default: ${url})
  --api-key <key>                LiveKit API key (default: ${api_key})
  --api-secret <secret>          LiveKit API secret (default: ${api_secret})
  --room <name>                  Room name (default: ${room})
  --publisher-identity <id>      Publisher identity (default: ${publisher_identity})
  --recorder-identity <id>       Recorder identity (default: ${recorder_identity})
  --frames <count>               Number of frames (default: full sequence)
  --output-dir <dir>             MCAP output directory (default: ${output_dir})
  --pointcloud-sequence <dir>    Replay another compatible sequence directory
  --build-dir <dir>              CMake build directory (default: ${build_dir})
  --no-build                     Skip building the publisher and recorder targets
  -h, --help                     Show this help

Examples:
  $0
  $0 --pointcloud-sequence schema_mcap/data/pandaset_sample
EOF
}

absolute_path() {
  local path="$1"
  local dir
  local base
  dir="$(dirname "${path}")"
  base="$(basename "${path}")"
  if [[ -d "${dir}" ]]; then
    echo "$(cd "${dir}" && pwd)/${base}"
  else
    echo "${path}"
  fi
}

resolve_input_path() {
  local path="$1"
  if [[ "${path}" = /* ]]; then
    absolute_path "${path}"
  elif [[ -e "${INVOCATION_DIR}/${path}" ]]; then
    absolute_path "${INVOCATION_DIR}/${path}"
  elif [[ -e "${REPO_ROOT}/${path}" ]]; then
    absolute_path "${REPO_ROOT}/${path}"
  elif [[ -e "${SCRIPT_DIR}/${path}" ]]; then
    absolute_path "${SCRIPT_DIR}/${path}"
  else
    absolute_path "${INVOCATION_DIR}/${path}"
  fi
}

run_supervised() {
  local status_file="$1"
  shift
  local child_pid=""

  terminate_child() {
    if [[ -n "${child_pid}" ]] && kill -0 "${child_pid}" >/dev/null 2>&1; then
      kill "${child_pid}" >/dev/null 2>&1 || true
      wait "${child_pid}" >/dev/null 2>&1 || true
    fi
  }

  trap 'terminate_child; exit 143' INT TERM

  "$@" &
  child_pid="$!"
  set +e
  wait "${child_pid}"
  local status="$?"
  set -e
  echo "${status}" > "${status_file}"
  exit "${status}"
}

read_status_file() {
  local status_file="$1"
  if [[ -f "${status_file}" ]]; then
    local status
    read -r status < "${status_file}"
    echo "${status}"
    return 0
  fi
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --url)
      url="$2"
      shift 2
      ;;
    --api-key)
      api_key="$2"
      shift 2
      ;;
    --api-secret)
      api_secret="$2"
      shift 2
      ;;
    --room)
      room="$2"
      shift 2
      ;;
    --publisher-identity)
      publisher_identity="$2"
      shift 2
      ;;
    --recorder-identity)
      recorder_identity="$2"
      shift 2
      ;;
    --frames)
      frames="$2"
      frames_explicit="true"
      shift 2
      ;;
    --output-dir)
      output_dir="$2"
      shift 2
      ;;
    --pointcloud-sequence)
      pointcloud_sequence_path="$2"
      shift 2
      ;;
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --no-build)
      skip_build="true"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! command -v lk >/dev/null 2>&1; then
  echo "lk CLI not found. Install it or add it to PATH before running this script." >&2
  exit 1
fi

pointcloud_sequence_path="$(resolve_input_path "${pointcloud_sequence_path}")"
if [[ ! "${frames}" =~ ^[1-9][0-9]*$ ]]; then
  echo "--frames must be a positive integer" >&2
  exit 1
fi
sequence_manifest="${pointcloud_sequence_path}/frames.txt"
if [[ ! -f "${sequence_manifest}" ]]; then
  echo "Point-cloud sequence manifest not found: ${sequence_manifest}" >&2
  exit 1
fi
sequence_frame_count="$(
  awk 'NF && $1 !~ /^#/ { count++ } END { print count + 0 }' "${sequence_manifest}"
)"
if [[ "${sequence_frame_count}" -eq 0 ]]; then
  echo "Point-cloud sequence contains no frames: ${sequence_manifest}" >&2
  exit 1
fi
if [[ "${frames_explicit}" != "true" ]]; then
  frames="${sequence_frame_count}"
elif [[ "${frames}" -gt "${sequence_frame_count}" ]]; then
  echo "[schema_mcap] sequence contains ${sequence_frame_count} frames; recording one complete pass"
  frames="${sequence_frame_count}"
fi

cd "${REPO_ROOT}"

if [[ "${skip_build}" != "true" ]]; then
  cmake --build "${build_dir}" --target schema_mcap_publisher schema_mcap_recorder
fi

publisher_bin="${build_dir}/schema_mcap/publisher/schema_mcap_publisher"
recorder_bin="${build_dir}/schema_mcap/recorder/schema_mcap_recorder"

if [[ ! -x "${publisher_bin}" || ! -x "${recorder_bin}" ]]; then
  echo "Missing schema_mcap binaries under ${build_dir}. Run CMake configure/build first." >&2
  exit 1
fi

mkdir -p "${output_dir}"
output_dir_abs="$(cd "${output_dir}" && pwd)"

export LIVEKIT_URL="${url}"
LIVEKIT_PUBLISHER_TOKEN="$(
  lk token create \
    --api-key "${api_key}" \
    --api-secret "${api_secret}" \
    --token-only \
    --join \
    --room "${room}" \
    --identity "${publisher_identity}"
)"
export LIVEKIT_PUBLISHER_TOKEN
LIVEKIT_RECORDER_TOKEN="$(
  lk token create \
    --api-key "${api_key}" \
    --api-secret "${api_secret}" \
    --token-only \
    --join \
    --room "${room}" \
    --identity "${recorder_identity}"
)"
export LIVEKIT_RECORDER_TOKEN

status_dir="$(mktemp -d "${TMPDIR:-/tmp}/schema_mcap.XXXXXX")"
recorder_status_file="${status_dir}/recorder.status"
publisher_status_file="${status_dir}/publisher.status"

cleanup() {
  local pid
  for pid in "${publisher_pid}" "${recorder_pid}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
      wait "${pid}" >/dev/null 2>&1 || true
    fi
  done
  rm -rf "${status_dir}"
}
trap cleanup EXIT INT TERM

recorder_pid=""
publisher_pid=""

recorder_cmd=("${recorder_bin}" --output-dir "${output_dir}" --frames "${frames}")
publisher_cmd=(
  "${publisher_bin}"
  --frames "${frames}"
  --pointcloud-sequence "${pointcloud_sequence_path}"
)

echo "[schema_mcap] starting recorder: ${recorder_cmd[*]}"
run_supervised "${recorder_status_file}" "${recorder_cmd[@]}" &
recorder_pid="$!"

sleep 1

recorder_status=0
if recorder_status="$(read_status_file "${recorder_status_file}")"; then
  if [[ "${recorder_status}" -ne 0 ]]; then
    echo "[schema_mcap] recorder failed before publisher start" >&2
    cleanup
    trap - EXIT INT TERM
    exit "${recorder_status}"
  fi
  echo "[schema_mcap] recorder exited before publisher start; not starting publisher" >&2
  cleanup
  trap - EXIT INT TERM
  exit 0
fi

echo "[schema_mcap] starting publisher: ${publisher_cmd[*]}"
run_supervised "${publisher_status_file}" "${publisher_cmd[@]}" &
publisher_pid="$!"

publisher_status=0
recorder_status=0
publisher_done="false"
recorder_done="false"
publisher_finish_logged="false"

while true; do
  if [[ "${publisher_done}" != "true" ]] && publisher_status="$(read_status_file "${publisher_status_file}")"; then
    publisher_done="true"
    if [[ "${publisher_status}" -ne 0 ]]; then
      echo "[schema_mcap] publisher failed; stopping recorder"
      cleanup
      trap - EXIT INT TERM
      exit "${publisher_status}"
    fi
    if [[ "${publisher_finish_logged}" != "true" && "${recorder_done}" != "true" ]]; then
      echo "[schema_mcap] publisher finished; waiting for recorder to close the MCAP file"
      publisher_finish_logged="true"
    fi
  fi

  if [[ "${recorder_done}" != "true" ]] && recorder_status="$(read_status_file "${recorder_status_file}")"; then
    recorder_done="true"
    if [[ "${recorder_status}" -ne 0 ]]; then
      echo "[schema_mcap] recorder failed; stopping publisher"
      cleanup
      trap - EXIT INT TERM
      exit "${recorder_status}"
    fi
    if [[ "${publisher_done}" != "true" ]]; then
      echo "[schema_mcap] recorder exited; stopping publisher"
      cleanup
      trap - EXIT INT TERM
      break
    fi
  fi

  if [[ "${publisher_done}" == "true" && "${recorder_done}" == "true" ]]; then
    break
  fi

  sleep 0.1
done

trap - EXIT INT TERM
rm -rf "${status_dir}"

if [[ "${publisher_status}" -ne 0 ]]; then
  echo "[schema_mcap] publisher exited with ${publisher_status}" >&2
  exit "${publisher_status}"
fi
if [[ "${recorder_status}" -ne 0 ]]; then
  echo "[schema_mcap] recorder exited with ${recorder_status}" >&2
  exit "${recorder_status}"
fi

latest_mcap="$(ls -t "${output_dir_abs}"/livekit_pointcloud_*.mcap 2>/dev/null | head -n 1 || true)"
if [[ -n "${latest_mcap}" ]]; then
  echo "[schema_mcap] complete. MCAP file written to: ${latest_mcap}"
else
  echo "[schema_mcap] complete. MCAP output directory: ${output_dir_abs}/"
fi
