#!/bin/bash

STATE_FILE="/opt/reboot-cycle/count"
LOG_FILE="/opt/reboot-cycle/reboot.log"
MAX_CYCLES=0   # 0 = infinite cycles

# Initialize counter if missing
if [ ! -f "$STATE_FILE" ]; then
    echo 0 > "$STATE_FILE"
fi

COUNT=$(cat "$STATE_FILE")
COUNT=$((COUNT + 1))

echo "$COUNT" > "$STATE_FILE"
echo "$(date '+%F %T') : reboot cycle $COUNT" >> "$LOG_FILE"

# Stop after MAX_CYCLES if set
if [ "$MAX_CYCLES" -ne 0 ] && [ "$COUNT" -ge "$MAX_CYCLES" ]; then
    echo "$(date '+%F %T') : reached max cycles, stopping" >> "$LOG_FILE"
    exit 0
fi

# Small delay to ensure filesystem sync
sleep 5

# Cold reboot
/sbin/reboot -f