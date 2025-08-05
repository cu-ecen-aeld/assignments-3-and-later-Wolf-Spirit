#!/bin/sh
filesdir=$1
searchstr=$2

first() {
if ! [[ -n "$filesdir" ]] || ! [[ -n "$searchstr" ]]; then
  echo "Error! I missed something!"
  return 1
fi
}

second() {
if ! [[ -d "$filesdir" ]]; then
  echo "Error! "$filesdir" is not a directory!"
  return 1
fi
}

first
second

numberOfFiles=$(find "$filesdir" -type f | wc -l)
numOfMatch=$(grep -r "$searchstr" "$filesdir" | wc -l)
echo "The number of files are "$numberOfFiles" and the number of matching lines are "$numOfMatch""