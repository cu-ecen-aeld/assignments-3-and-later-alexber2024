#!/bin/bash

if [ $# -ne 2 ]
then
    echo "Args missing. Please provide directory and string."
    exit 1
fi

# create or overwrite the file with the provided string. Create the directory if it doesn't exist.
DIRECTORY=$(dirname "$1")

if [ ! -d "$DIRECTORY" ]
then
    mkdir -p "$DIRECTORY"
fi

echo $2 > $1
exit 0