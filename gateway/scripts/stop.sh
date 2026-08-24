#!/bin/bash

if [ "$EUID" -ne 0 ]; then
  echo "run with sudo: sudo ./stop.sh"
  exit 1
fi

systemctl stop gateway
echo "shutdown gateway"

killall client_tls 2>/dev/null
killall client 2>/dev/null

cd "$(dirname "$0")/.."
WORK_DIR=$(pwd)

make clean



./netns_setup.sh stop
