#!/usr/bin/env bash
#
# collect-log.sh — Download the filament dryer CSV log, then clear it on the ESP.
#
# Usage:
#   ./scripts/collect-log.sh                     # defaults: ESP at 10.13.13.93, logs in ~/dryer-logs/
#   ./scripts/collect-log.sh 192.168.1.50        # custom ESP IP
#   DRYER_LOG_DIR=/mnt/nas/logs ./scripts/collect-log.sh   # custom output dir
#   SKIP_ANALYSIS=true ./scripts/collect-log.sh  # skip analysis step
#

set -euo pipefail

ESP_IP="${1:-10.13.13.93}"
LOG_DIR="${DRYER_LOG_DIR:-${HOME}/dryer-logs}"
TIMEOUT=30

# Timestamped filename: dryer_2026-04-13_1830.csv
TIMESTAMP="$(date +%Y-%m-%d_%H%M)"
OUTFILE="${LOG_DIR}/dryer_${TIMESTAMP}.csv"

mkdir --parents "${LOG_DIR}"

# 1. Check if ESP is reachable
if ! curl --silent --fail --max-time 5 "http://${ESP_IP}/api/log/stats" > /dev/null 2>&1; then
    echo "ERROR: ESP at ${ESP_IP} is not reachable." >&2
    exit 1
fi

# 2. Get log stats before download
STATS="$(curl --silent --fail --max-time "${TIMEOUT}" "http://${ESP_IP}/api/log/stats")"
RECORD_COUNT="$(echo "${STATS}" | grep --only-matching '"record_count":[0-9]*' | grep --only-matching '[0-9]*')"
FILE_SIZE="$(echo "${STATS}" | grep --only-matching '"file_size":[0-9]*' | grep --only-matching '[0-9]*')"

if [ "${RECORD_COUNT}" -eq 0 ] 2>/dev/null; then
    echo "Log is empty (0 records). Nothing to collect."
    exit 0
fi

echo "Found ${RECORD_COUNT} records (${FILE_SIZE} bytes) on ESP."

# 3. Download the CSV log
HTTP_CODE="$(curl --silent --output "${OUTFILE}" --write-out '%{http_code}' --max-time "${TIMEOUT}" "http://${ESP_IP}/api/log")"

if [ "${HTTP_CODE}" -ne 200 ]; then
    echo "ERROR: Download failed with HTTP ${HTTP_CODE}." >&2
    rm --force "${OUTFILE}"
    exit 1
fi

DOWNLOADED_SIZE="$(stat --format='%s' "${OUTFILE}")"
echo "Downloaded ${DOWNLOADED_SIZE} bytes -> ${OUTFILE}"

# 4. Verify download looks sane (at least has header + some data)
DOWNLOADED_LINES="$(wc --lines < "${OUTFILE}")"
if [ "${DOWNLOADED_LINES}" -lt 2 ]; then
    echo "ERROR: Downloaded file has only ${DOWNLOADED_LINES} line(s), expected at least header + data. Keeping file, NOT clearing ESP log." >&2
    exit 1
fi

# 5. Clear the log on the ESP
CLEAR_RESP="$(curl --silent --fail --max-time "${TIMEOUT}" --request DELETE "http://${ESP_IP}/api/log")"
if echo "${CLEAR_RESP}" | grep --quiet '"cleared":true'; then
    echo "ESP log cleared successfully."
else
    echo "WARNING: Clear request returned unexpected response: ${CLEAR_RESP}" >&2
fi

# 6. Run analysis (non-fatal — download+clear is the critical path)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANALYZE_SCRIPT="${SCRIPT_DIR}/analyze-log.py"

if [ "${SKIP_ANALYSIS:-false}" = "true" ]; then
    echo "Analysis skipped (SKIP_ANALYSIS=true)."
elif [ ! -f "${ANALYZE_SCRIPT}" ]; then
    echo "Analysis script not found at ${ANALYZE_SCRIPT}, skipping."
elif ! command -v python3 > /dev/null 2>&1; then
    echo "python3 not found, skipping analysis."
else
    echo ""
    echo "Running analysis..."
    if python3 "${ANALYZE_SCRIPT}" "${OUTFILE}"; then
        PNG_FILE="${OUTFILE%.csv}.png"
        if [ -f "${PNG_FILE}" ]; then
            echo "Analysis plot: ${PNG_FILE}"
        fi
    else
        echo "WARNING: Analysis failed (non-fatal). CSV was saved successfully." >&2
    fi
fi

echo "Done. Log saved to: ${OUTFILE}"
