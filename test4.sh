mkdir test4
./vi.sh test4/vi 2 123 &
./nice2.sh test4/nice 5 25000 75000 120 -5 &
wait
