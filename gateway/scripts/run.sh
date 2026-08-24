#!/bin/bash
# run.sh

if [ "$EUID" -ne 0 ]; then
  echo "error: run by sudo: sudo ./run.sh"
  exit 1
fi

./netns_setup.sh start


systemctl start gateway

if systemctl is-active --quiet gateway; then
    echo "gateway is running"
else 
    echo "error"
fi
