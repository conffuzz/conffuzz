#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 20 ];
do
        curl http://localhost/ &> /dev/null
        sleep 0.3
        ((i++))
done

killall -9 lighttpd
