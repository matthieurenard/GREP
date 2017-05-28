#!/bin/bash

Prog="../../game_enf_offline"
AutomatonFile="../../cosafety.tmtn"
GraphFile="cosafety.grph"
TimeFilePrefix="times"
LogFilePrefix="log"
OutFilePrefix="out"
ErrFilePrefix="err"
InputFile="inputs_game"
RepeatTimes=100

if [ ! -e "$GraphFile" ]; then
	echo -n "Generating $GraphFile... "
	$Prog -a "$AutomatonFile" -s "$GraphFile" < /dev/null >/dev/null 2>/dev/null
	echo "Done."
fi

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
	echo -n "."
done

echo

exit 0

