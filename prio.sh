WORK_COUNT=`./late -c 400000 | grep Calculated | cut -d' ' -f 3`
echo $WORK_COUNT
./late -p -r10 -w $WORK_COUNT -s 100000
