mkdir test1
./vi.sh test1/vi 4 63 &
./mozilla.sh test1/mozilla 2 63 &
./compiler.sh test1/comp 2 30 &

wait
