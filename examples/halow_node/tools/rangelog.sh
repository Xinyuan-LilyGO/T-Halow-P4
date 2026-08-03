#!/bin/bash
# Ground station for a HaLow range walk: run this on a machine on the home
# LAN while somebody carries the camera node away from the gateway.
#
# Pulls the field unit's H.264 stream through the gateway and KEEPS IT, while
# logging once a second: bytes received, RSSI/SNR, ping RTT and loss.
# Reconnects when the link drops, so walking out of range and back produces
# one continuous record; each connection becomes its own video segment so a
# dropout never truncates what was already captured.
#
# Usage: rangelog.sh <gateway-ip> [csv] [videodir]
#
# The gateway forwards :8554 to the field unit's H.264 stream and :8080 to its
# config page, so both the video and the telemetry come from one address.
GW="${1:?usage: rangelog.sh <gateway-ip> [csv] [videodir]}"
OUT="${2:-./rangewalk-$(date +%Y%m%d-%H%M).csv}"
VIDDIR="${3:-$(dirname "$OUT")/video-$(basename "${OUT%.csv}")}"

mkdir -p "$(dirname "$OUT")" "$VIDDIR"
# Append if the CSV already exists, so a restart mid-walk keeps one trace.
# utc_iso first so the trace can be aligned against a GPX track without
# guessing at timezone or date; local time kept for reading at a glance.
[ -s "$OUT" ] || echo "utc_iso,time,elapsed_s,kbit_s,total_MB,rssi,snr,tx_rate,ping_ms,loss_pct,halow_up" > "$OUT"
echo "log:   $OUT"
echo "video: $VIDDIR/segNNN.h264   (Ctrl-C to stop)"

start=$(date +%s)
seg=0
trap 'echo; echo "saved: $OUT and $VIDDIR"; exit 0' INT TERM

while true; do
  seg=$((seg + 1))
  # Timestamped: a logger restart must never overwrite an earlier segment.
  vid=$(printf '%s/%s-seg%03d.h264' "$VIDDIR" "$(date +%H%M%S)" "$seg")
  timeout 7200 nc "$GW" 8554 > "$vid" 2>/dev/null &
  ncpid=$!
  last=0
  stalled=0
  while kill -0 $ncpid 2>/dev/null; do
    sleep 1
    now=$(date +%s)
    cur=$(stat -c %s "$vid" 2>/dev/null || echo 0)
    rate=$(( (cur - last) * 8 / 1000 ))
    last=$cur

    link=$(curl -s -m2 "http://$GW:8080/api/link" 2>/dev/null)
    get() { sed -n "s/.*\"$1\":\([-0-9.]*\).*/\1/p" <<<"$link"; }
    up=$(grep -o '"halow_up":[a-z]*' <<<"$link" | cut -d: -f2)

    printf '%s,%s,%s,%s,%.1f,%s,%s,%s,%s,%s,%s\n' \
      "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(date +%H:%M:%S)" "$(( now - start ))" "$rate" \
      "$(echo "scale=1; $cur/1048576" | bc 2>/dev/null || echo 0)" \
      "$(get rssi)" "$(get snr)" "$(get tx_bitrate)" \
      "$(get ping_ms)" "$(get loss_pct)" "${up:-unknown}" >> "$OUT"

    tail -1 "$OUT"

    # A board reboot leaves this end of the TCP connection half-open: nc sits
    # there forever and records nothing while the link looks perfectly fine.
    # If bytes stop while the link is up, drop it and reconnect.
    if [ "$rate" -eq 0 ] && [ "${up:-unknown}" = "true" ]; then
      stalled=$((stalled + 1))
    else
      stalled=0
    fi
    if [ "$stalled" -ge 20 ]; then
      echo "  (no video for 20s with the link up -- reconnecting)"
      kill $ncpid 2>/dev/null
      break
    fi
  done
  # Drop empty segments from failed reconnect attempts.
  [ -s "$vid" ] || rm -f "$vid"
  sleep 2
done
