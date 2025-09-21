#!/bin/bash

if [ $# -ne 2 ]
then
    echo "Args missing. Please provide directory and search string."
    exit 1
fi

if [ ! -d $1 ]
then
    echo "Invalid directory: $1"
    exit 1
fi

DIRECTORY=$1
SEARCH_STRING=$2

number_of_files=$(ls -r $DIRECTORY | wc -l)
lines=$(grep -r $SEARCH_STRING $DIRECTORY | wc -l)

echo "The number of files are $number_of_files and the number of matching lines are $lines"
exit 0