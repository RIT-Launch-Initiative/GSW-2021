#!/bin/bash

# Start Grafana server remotely, first argument is hostname 
# Seconds argument is username, or defaults to 'launch'

if ! [ $1 ]
then
    echo "Usage ./remote_start.sh [hostname] <username>"
    exit 
fi

if ! [ $2 ]
then
    USER="launch"
else
    USER=$2
fi

ssh $USER@$2 "gsw; app/db/grafana/start.sh"
