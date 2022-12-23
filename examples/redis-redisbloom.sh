#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 1000 ];
do
	# RedisBloom
	/root/redis-6.2.6/src/redis-cli BF.ADD newFilter foo
	/root/redis-6.2.6/src/redis-cli BF.EXISTS newFilter foo
	/root/redis-6.2.6/src/redis-cli BF.EXISTS newFilter bar
	/root/redis-6.2.6/src/redis-cli BF.MADD myFilter foo bar baz
	/root/redis-6.2.6/src/redis-cli BF.MEXISTS myFilter foo nonexist bar
	/root/redis-6.2.6/src/redis-cli BF.RESERVE customFilter 0.0001 600000
	/root/redis-6.2.6/src/redis-cli BF.MADD customFilter foo bar baz
	/root/redis-6.2.6/src/redis-cli CF.RESERVE newCuckooFilter 1000
	/root/redis-6.2.6/src/redis-cli CF.ADD newCuckooFilter foo
	/root/redis-6.2.6/src/redis-cli CF.EXISTS newCuckooFilter foo
	/root/redis-6.2.6/src/redis-cli CF.EXISTS newCuckooFilter notpresent
	/root/redis-6.2.6/src/redis-cli CF.DEL newCuckooFilter foo

        sleep 0.3
        ((i++))
done

# killing the worker
killall -9 redis-server
