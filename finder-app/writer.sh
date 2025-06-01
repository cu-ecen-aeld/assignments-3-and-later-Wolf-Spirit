#!/bin/bash
writefile=$1
writestr=$2

first() {
if ! [[ -n "$writefile" ]] || ! [[ -n "$writestr" ]]; then
  echo "Error! I missed something!"
  return 1
fi
}

newfile() {
  echo "$writefile"
  mkdir -p $(dirname "$writefile")
}

echo "$writestr" > "$(newfile "$writefile")"

first
