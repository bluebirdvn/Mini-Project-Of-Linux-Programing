#!/bin/bash

echo "Build ipc-cmd-pipeline"
echo ""

echo "Build Main"
echo "Create fifo for control and log"

make clean > /dev/null 2>&1
make 
if [ $? -eq 0 ]; then
    echo "build successfully"
else 
    echo "build failed"
    exit 1
fi


echo "Build Controller"
echo "Create controller"

cd controller

make clean > /dev/null 2>&1
make 
if [ $? -eq 0 ]; then
    echo "build successfully"
else 
    echo "build failed"
    exit 1
fi


echo "Build Logger"
echo "Create logger"

cd logger

make clean > /dev/null 2>&1
make 
if [ $? -eq 0 ]; then
    echo "build successfully"
else 
    echo "build failed"
    exit 1
fi


echo "Build Worker"
echo "Create worker"

cd worker

make clean > /dev/null 2>&1
make 
if [ $? -eq 0 ]; then
    echo "build successfully"
else 
    echo "build failed"
    exit 1
fi


echo "build successfully"
echo "binary: "
ls -la main controller/controller logger/logger worker/worker  2>/dev/null