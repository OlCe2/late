#!/bin/sh

if [ $# -lt 3 ]; then
	echo "Usage: " $0 "<output dir> <iterations> <parallelization>"
	exit 1
fi

DIRECTORY=$1
ITERATIONS=$2
PARALLEL=$3

WORK_US=90000
SLEEP_US=10000

WORK_COUNT=`./late -c $WORK_US | grep Calculated | cut -d' ' -f 3`

mkdir $DIRECTORY

i=0 
while [ $i -lt $ITERATIONS ]; do
	./batch.sh $DIRECTORY/$i $PARALLEL $WORK_COUNT $SLEEP_US 0 10
	i=`expr $i + 1`
done
