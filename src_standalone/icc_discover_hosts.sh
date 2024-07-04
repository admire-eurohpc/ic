#!/bin/bash

set -x

usage () {
    echo "Usage: $0  [hint_nodes]"
    echo "NOTE: hint can be set as parameter or with env. var. \$HINT_NODES" 
    echo "NOTE2: If \$HINT_FILE set, hits are read from \$HINT_FILE one by one"
    echo "NOTE3: STANDALONE_FIFO must be defined with the standalone fifo file"
    exit 1
}

# check standalone fifo exist
if ! [ -p "$STANDALONE_FIFO" ]; then
    usage
fi

# check too many parameters 
if [ $# -gt 1 ]; then
    usage
fi
	
# check exit env. var parameters
if [ $# -eq 0 ] && [ ! -v HINT_NODES ] && [ ! -v HINT_FILE ]; then
    usage
fi

#get parameter from command line
if [ $# -eq 1 ]; then
    HINT_NODES=$1
fi

#get parameters from file
if [ $# -eq 0 ]; then
    if [ -f "$HINT_FILE" ]; then
	read AUX_HINTS < "$HINT_FILE"
        HINT_NODES="$( cut -d ' ' -f 1 <<< "$AUX_HINTS" | xargs)"
        AUX_HINTS="$( cut -d ' ' -f 2- <<< "$AUX_HINTS" | xargs)"
	echo "$AUX_HINTS" > "$HINT_FILE"
    fi    
fi

# check parameter is a integer
NUM_PATTERN='^-?[0-9]+$' 
if ! [[ $HINT_NODES =~ $NUM_PATTERN ]]; then 
    HINT_NODES=0
fi


# leave malleability region and get hosts list
echo "LEAVE" > $STANDALONE_FIFO
NODELIST=$(cat $STANDALONE_FIFO)

# enter malleability region and set nodes hint
echo "ENTER" > $STANDALONE_FIFO
echo $HINT_NODES > $STANDALONE_FIFO

# print hosts line by line
for x in $(IFS=',';echo $NODELIST); do 
    echo "$x"; 
done
echo "broadwell-000"
