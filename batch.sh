#!/bin/sh

if [ $# -lt 5 ]; then
	echo "Usage: " $0 "<output dir> <late iterations> <work count> <sleep us> <run sec> <work iterations>"
	exit 1
fi

DIRECTORY=$1
ITERATIONS=$2
WORK_COUNT=$3
SLEEP_US=$4
RUN_SEC=$5
WORK_ITER=$6

# The -u option is for synchronization
# LATE_CMD="./late -u -r$RUN_SEC -w $WORK_COUNT -s $SLEEP_US"

if [ $RUN_SEC -eq 0 ]; then
	LATE_CMD="./late -i $WORK_ITER -w $WORK_COUNT -s $SLEEP_US"
else
	LATE_CMD="./late -x -r$RUN_SEC -w $WORK_COUNT -s $SLEEP_US"
fi

mkdir $DIRECTORY

i=0
while [ $i -lt $ITERATIONS ]; do
	$LATE_CMD > $DIRECTORY/$i &
	i=`expr $i + 1`
	pids=`echo $pids  $!`
done
# kill -USR1 $pids
echo $pids
wait
