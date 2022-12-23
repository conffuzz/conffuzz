#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 100000 ];
do
        curl --compressed https://www.google.com &> /dev/null
        sleep 0.3
        ((i++))
done

killall -9 wireshark
