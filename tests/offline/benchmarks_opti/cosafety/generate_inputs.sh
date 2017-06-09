#!/bin/bash

TraceGenProg="../generate_traces"
NTraces=100
Length=1000
DelayMax=3
TipexFile="inputs_tipex"
GameFile="inputs_game"
ActionsT='G, R'
ActionsG="$ActionsT"

if [ -n "$1" ]; then
	NTraces=$1
fi

if [ -n "$2" ]; then
	Length=$2
fi

$TraceGenProg $NTraces $Length $DelayMax $TipexFile $GameFile "$ActionsT" \
	"$ActionsG"

