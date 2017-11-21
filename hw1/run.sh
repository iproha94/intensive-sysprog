#!/bin/bash

echo "0" > file.txt

for i in `seq 1 50`;
do
	./a.out &
done 

sleep 5
cat file.txt
	
