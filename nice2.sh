#!/bin/sh

if [ $# -lt 5 ]; then
	echo "Usage: " $0 "<output dir> <late iterations> <work count> <sleep us> <run sec> <nice>"
	exit 1
fi

DIRECTORY=$1
ITERATIONS=$2
WORK_COUNT=$3
SLEEP_US=$4
RUN_SEC=$5
NICE=$6

LATE_CMD="./late -u -b -n $NICE -r$RUN_SEC -w $WORK_COUNT -s $SLEEP_US"

mkdir $DIRECTORY

i=0
while [ $i -lt $ITERATIONS ]; do
	$LATE_CMD > $DIRECTORY/$i &
	i=`expr $i + 1`
	pids=`echo $pids  $!`
done
echo $pids
sleep 1
kill -USR1 $pids
wait
