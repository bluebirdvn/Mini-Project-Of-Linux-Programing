#!/bin/bash

echo "Run pipeline"

CONTROLLER=./controller/controller
LOGGER=./logger/logger
WORKER=./worker/worker

if [ ! -f "ipc-cmd-pipeline" ]; then
    echo "main is not created."
    echo "running ./build.sh"
    ./build.sh

fi

if [ -f "ipc.pid" ]; then
    OLD_PID=$(cat ipc.pid)
    if kill -0 $OLD_PID 2> /dev/null; then
        echo "kill old sys: PID : $OLD_PID"
        kill $OLD_PID
        sleep 1
    fi
    rm -rf ipc.pid
fi

echo "run main"
nohup ./ipc-cmd-pipeline $CONTROLLER $WORKER $LOGGER > main.log 2>&1 &
MAIN_PID=$!

echo $MAIN_PID > ipc.pid

sleep 2

if kill -0 $MAIN_PID 2>/dev/null; then
    echo "sys run successfully"
    echo "pid: $MAIN_PID"
    echo "log: main.log"
else
    echo "run failed"
    rm -f ipc.pid
    exit 1
fi