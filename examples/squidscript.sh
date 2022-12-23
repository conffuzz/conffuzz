#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 200 ];
do
        curl localhost:100 &> /dev/null
        sleep 0.3
        ((i++))
done

# killing the worker
killall -9 squid
