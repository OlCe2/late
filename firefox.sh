#!/bin/sh

. utils.sh

[ $# -eq 4 ] ||
    errx 1 "Usage: $0 <output dir> <calibration's work duration (ms)> <calibration's work iterations> <run (s)>"

DIRECTORY=$1
CALIB_MS=$2
CALIB_ITER=$3
RUN_SEC=$4

LATE_CMD="./late -u -r ${RUN_SEC} -S"

# All durations are in ms

# They were chosen after a rough and mostly manual analysis of a sample browsing
# session with one tab used to browse a newspaper with picture, another running
# a Slack session, and other transiently being opened on mostly-text sites.

# 10% CPU during 100ms, then sleeping for the rest of a 4s period.  This is
# repeated for about ~1:30 where a spike of %CPU to 40% occurs for 100ms.
MAIN_IPC_IO_PARENT=$(prepare_series ${CALIB_MS} ${CALIB_ITER} \
                     22,10,90,0,3900 1,40,60)
# 80% CPU during 2 100ms slices, then sporadic spikes of 70% CPU every 10
# seconds for a minute, and then 100% CPU for 1 slice spaced by 2 seconds.  In
# between all these, we have 4 slices at 10% CPU per second.
MAIN_THREAD=$(prepare_series ${CALIB_MS} ${CALIB_ITER} \
              2,80,20 10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 1,70,30 \
              10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 1,70,30 \
              10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 1,70,30 \
              10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 1,70,30 \
              10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 1,70,30 \
              10,10,90,0,200,10,90,0,100,10,90,10,90,0,400 \
              2,100,1900)
# 40% CPU during 400ms per 100ms slices, then 10% CPU during 100ms and sleep during the rest of a period of 4s, repeated 9 times (so the whole cycle is ~40s).
WEB_CONTENT_1=$(prepare_series ${CALIB_MS} ${CALIB_ITER} \
                4,40,60 10,10,90,0,3900)
# 100% CPU during 200ms twice, with 10s of 10% CPU one out of three periods of
# 100ms, and then a period of 1 minute with similar 10% CPU small periods.
WEB_CONTENT_2=$(prepare_series ${CALIB_MS} ${CALIB_ITER} \
                1,200,0 10,10,90,0,100,10,90,0,300,10,90,0,300 \
                60,10,90,0,150,10,90,0,250,10,90,0,300)
# 70 %CPU during 300ms, with then 4 slices of 100ms with 20% CPU per second for
# 10s.
WEB_CONTENT_3=$(prepare_series ${CALIB_MS} ${CALIB_ITER} \
             3,70,30 10,20,80,20,80,0,200,20,80,20,80,0,400)


# Meat
mkdir "$DIRECTORY" || exit 1
PIDS=
${LATE_CMD} "${MAIN_IPC_IO_PARENT}" > "$DIRECTORY/main_ipc_io_parent.txt" &
PIDS="$PIDS $!"
${LATE_CMD} "${MAIN_THREAD}" > "$DIRECTORY/main_thread.txt" &
PIDS="$PIDS $!"
${LATE_CMD} "${WEB_CONTENT_1}" > "$DIRECTORY/web_content_1.txt" &
PIDS="$PIDS $!"
${LATE_CMD} "${WEB_CONTENT_2}" > "$DIRECTORY/web_content_2.txt" &
PIDS="$PIDS $!"
${LATE_CMD} "${WEB_CONTENT_3}" > "$DIRECTORY/web_content_3.txt" &
PIDS="$PIDS $!"

sleep 1
kill -USR1 $PIDS
wait
