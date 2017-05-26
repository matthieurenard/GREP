#!/bin/bash

Prog="../../game_enf_offline"
GraphFile="../../safety_two_clocks.grph"
TimeFilePrefix="times"
LogFilePrefix="log"
OutFilePrefix="out"
ErrFilePrefix="err"
InputFile="../../inputs_safety_two_clocks"
RepeatTimes=100

NLines=$(cat $InputFile | wc -l)

for j in $(seq 1 $RepeatTimes); do
	cat /dev/null > "$TimeFilePrefix$j"
	cat /dev/null > "$LogFilePrefix$j"
done

for j in $(seq 1 $RepeatTimes); do
	TimeFile="$TimeFilePrefix$j"
	for i in $(seq 1 $NLines); do
		#echo $(tail -n +$i $InputFile | head -n 1)
		tail -n +$i $InputFile | head -n 1 | $Prog -g $GraphFile -t $TimeFile -l \
			$LogFilePrefix$j >$OutFilePrefix$j 2>$ErrFilePrefix$j
	done
done

exit 0

