grep "%CPU" 20 15 10 5 0 _5 _10 _15 _20 | awk '{print $1" " $3}' | sed 's/_/-/' | sed 's/://'
