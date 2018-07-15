#!/bin/bash

cd ..
max=$1

for i in $(seq 0 $max)
do
	if [ -f ./logs/log$i.txt ]; then
		echo Logs already exists, stopping run.
		exit
	fi
	( ./HTM.out $i >> ./logs/log$i.txt & )
	echo started region $i
done

echo all processes started
