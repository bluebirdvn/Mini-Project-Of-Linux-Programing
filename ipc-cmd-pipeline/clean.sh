#!/bin/bash
# clean.sh - Clean everything

./stop.sh 2>/dev/null

echo "cleaning..."
make clean 2>/dev/null
cd controller && make clean && cd ..
cd worker && make clean && cd ..
cd logger && make clean && cd ..

rm -f main controller/controller worker/worker logger/logger
rm -f ipc.pid main.log
rm -rf build logs
rm -f /tmp/ipc_*.fifo


echo "clean successfully"
