#!/bin/sh

msg() {
    echo "$@" >&2
}

warn() {
    msg "WARNING: $@"
}

err() {
    msg "ERROR: $@"
}

errx() {
    local rc
    rc=$1
    shift
    err "$@"
    exit $rc
}

extract_calibration_iterations() {
    awk "/Calculated count:/ { print $NF; }"
}

prepare_group() {
    local CALIB_MS CALIB_ITER G I R V

    CALIB_MS=$1
    CALIB_ITER=$2
    G=$3

    I=0
    R=
    for V in $(echo "$G" | tr ',' ' '); do
        if [ $((I%2)) == 1 ]; then
            R="$R,$((V*CALIB_ITER/CALIB_MS))";
        elif [ $I == 0 ]; then
            R="$V"
        else
            R="$R,$((V*1000))" # Conversion to us
        fi
        I=$((I+1))
    done
    echo $R
}
# Series is a repetition of (occurences, work_time, sleep_time, work_time,
# sleep_time, ...) separated by spaces.  All times in the series must be in ms.
prepare_series() {
    local CALIB_MS CALIB_ITER R

    CALIB_MS=$1
    CALIB_ITER=$2
    shift 2

    R=
    for G; do
        R="$R $(prepare_group $CALIB_MS $CALIB_ITER $G)"
    done
    echo $R
}
