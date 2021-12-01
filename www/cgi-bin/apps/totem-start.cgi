#!/bin/sh

/etc/init.d/S30putv start
RESULT={\"result\"=\"$?\"}"
RESULTLEN=$(echo $RESULT | wc -c )
printf "Content-Type: application/json\r\n"
printf "Content-Length: $RESULTLEN\r\n"
printf "\r\n"
echo $RESULT
