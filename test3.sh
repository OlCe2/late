mkdir test3
./vi.sh test3/vi 2 33 &
./nice2.sh test3/nice 50 30000 70000 30 -20 &
wait
