#!/bin/sh

if [ $# -lt 3 ]; then
	echo "Usage: " $0 "<output dir> <concurrent> <run sec>"
	exit 1
fi

DIRECTORY=$1
CONCURRENT=$2
RUN_SEC=$3

WORK_US=8000
SLEEP_US=92000

WORK_COUNT=`./late -c $WORK_US | grep Calculated | cut -d' ' -f 3`

./batch.sh $DIRECTORY $CONCURRENT $WORK_COUNT $SLEEP_US $RUN_SEC
