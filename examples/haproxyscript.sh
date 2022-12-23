#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 20 ];
do
        curl -o /dev/null -v -D - 127.0.0.1:8090 -H "Accept-Encoding: gzip" &> /dev/null
        sleep 0.3
        ((i++))
done

killall -9 haproxy
