#!/bin/sh

if [ $# -lt 4 ]; then
	echo "Usage: " $0 "<output dir> <work us> <sleep us> <run sec>"
	exit 1
fi

DIRECTORY=$1
WORK_US=$2
SLEEP_US=$3
RUN_SEC=$4

WORK_COUNT=`./late -c $WORK_US | grep Calculated | cut -d' ' -f 3`

LATE_CMD="./late -u -b -r$RUN_SEC -w $WORK_COUNT -s $SLEEP_US"

mkdir $DIRECTORY

$LATE_CMD  -n-20 > $DIRECTORY/_20 &
pids=`echo $pids  $!`

$LATE_CMD -n-15 > $DIRECTORY/_15 &
pids=`echo $pids  $!`

$LATE_CMD -n-10 > $DIRECTORY/_10 &
pids=`echo $pids  $!`

$LATE_CMD -n-5 > $DIRECTORY/_5 &
pids=`echo $pids  $!`

$LATE_CMD -n0 > $DIRECTORY/0 &
pids=`echo $pids  $!`

$LATE_CMD -n5 > $DIRECTORY/5 &
pids=`echo $pids  $!`

$LATE_CMD -n10 > $DIRECTORY/10 &
pids=`echo $pids  $!`

$LATE_CMD -n15 > $DIRECTORY/15 &
pids=`echo $pids  $!`

$LATE_CMD -n20 > $DIRECTORY/20 &
pids=`echo $pids  $!`

sleep 1
kill -USR1 $pids

wait
