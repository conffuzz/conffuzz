#!/bin/bash

# wait for the server to start
sleep 1

i=0
while [ $i -lt 1000 ];
do
	# RediSearch
        /root/redis-6.2.6/src/redis-cli hset doc:1 title "hello world" body "lorem ipsum" url "http://redis.io"
        /root/redis-6.2.6/src/redis-cli FT.SEARCH myIdx "hello world" LIMIT 0 10
        /root/redis-6.2.6/src/redis-cli FT.SUGADD autocomplete "hello world" 100
        /root/redis-6.2.6/src/redis-cli FT.SUGGET autocomplete "he"
        /root/redis-6.2.6/src/redis-cli FT.DICTADD dict foo bar "hello world"
        /root/redis-6.2.6/src/redis-cli FT.SPELLCHECK myIdx held DISTANCE 2
        /root/redis-6.2.6/src/redis-cli FT.AGGREGATE myIdx "*"
        /root/redis-6.2.6/src/redis-cli FT.DROPINDEX myIdx
        /root/redis-6.2.6/src/redis-cli FT.CREATE users SCHEMA first_name TEXT SORTABLE last_name TEXT age NUMERIC SORTABLE
        /root/redis-6.2.6/src/redis-cli FT.ADD users user1 1.0 FIELDS first_name "alice" last_name "jones" age 35
        /root/redis-6.2.6/src/redis-cli FT.ADD users user2 1.0 FIELDS first_name "bob" last_name "jones" age 36
        /root/redis-6.2.6/src/redis-cli FT.SEARCH users "@last_name:jones" SORTBY first_name DESC
        /root/redis-6.2.6/src/redis-cli FT.SEARCH users "alice jones" SORTBY age ASC
        /root/redis-6.2.6/src/redis-cli FT.CREATE idx schema t text
        /root/redis-6.2.6/src/redis-cli FT.SYNUPDATE idx group1 hello world
        /root/redis-6.2.6/src/redis-cli FT.SEARCH idx hello

        sleep 0.3
        ((i++))
done

# killing the worker
killall -9 redis-server
