#!/bin/bash
aspell check /root/examples/aspelltest.txt
cat aspell.dockerfile | aspell -a
