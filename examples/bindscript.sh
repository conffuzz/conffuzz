#!/bin/bash

# wait for the server to start
sleep 2

echo "nameserver 127.0.0.1  # ns2 private IP address " > /etc/resolv.conf
i=0
while [ $i -lt 20 ];
do
	/root/bind-9.18.8/bin/dig/nslookup example.com
        curl 127.0.0.1:8080 &> /dev/null
        sleep 0.3
        ((i++))
done

killall -9 bind
