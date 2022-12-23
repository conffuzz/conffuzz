#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 80 ];
do
        python3 /root/examples/memcached-client-sasl.py &> /dev/null
        sleep 0.3
        ((i++))
done

killall -9 memcached
