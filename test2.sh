mkdir test2
./vi.sh test2/vi 4 33 &
./mozilla.sh test2/mozilla 2 33 &
./nice.sh test2/nice 100000 0 30 &

wait
