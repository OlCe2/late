WORK_COUNT=`./late -c 40000 | grep Calculated | cut -d' ' -f 3`
echo $WORK_COUNT
./late -p -r360 -w $WORK_COUNT -s 50000
