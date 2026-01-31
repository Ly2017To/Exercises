#!/bin/sh

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <process_name> <output_file>"
    exit 1
fi

PROCESS_NAME="$1"
OUTPUT_FILE="$2"

echo "Attempting to find process: $PROCESS_NAME"

PID=$(pgrep -x -o "$PROCESS_NAME")

echo "Found PID: $PID"

if [ -z "$PID" ]; then
    echo "Error: Process '$PROCESS_NAME' not found."
    exit 1
fi

echo "=== Thread Monitor Started at $(date +"%Y-%m-%d %H:%M:%S") ===" > "$OUTPUT_FILE"
echo "Monitoring threads for PID $PID (Process: $PROCESS_NAME)..." >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

while true; do
    if [ ! -d "/proc/$PID" ]; then
        echo "Process $PID ($PROCESS_NAME) has exited. Stopping monitor." >> "$OUTPUT_FILE"
        echo "Monitor stopped at $(date +"%Y-%m-%d %H:%M:%S")" >> "$OUTPUT_FILE"
        exit 0
    fi

    TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

    THREADS=$(ls /proc/$PID/task/ 2>/dev/null)

    echo "[$TIMESTAMP] Active threads (TID):" >> "$OUTPUT_FILE"
    
    if [ -z "$THREADS" ]; then
        echo "  No active threads found (process may be exiting)..." >> "$OUTPUT_FILE"
    else
        for TID in $THREADS; do
            echo "  Thread $TID is active." >> "$OUTPUT_FILE"
        done
        echo "  All TIDs: $THREADS" >> "$OUTPUT_FILE"
    fi

    echo "" >> "$OUTPUT_FILE"

    sleep 60
done
