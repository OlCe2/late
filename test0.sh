mkdir test0
./vi.sh test0/vi 4 33 &
./mozilla.sh test0/mozilla 2 33 &
./nice.sh test0/nice 100000 100000 30 &

wait
