#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 30 ];
do
        curl http://172.17.0.2/ &> /dev/null
        sleep 0.3
        ((i++))
done

# killing the worker
killall -9 httpd
