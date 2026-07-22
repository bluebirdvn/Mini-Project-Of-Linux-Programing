#!/bin/bash
# stop.sh 

stop_from_pidfile() {
    local pidfile=$1
    local name=$2
    
    if [ -f "$pidfile" ]; then
        local pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            echo "  Stopping $name (PID: $pid)..."
            kill "$pid" 2>/dev/null || true
            sleep 1
            if kill -0 "$pid" 2>/dev/null; then
                echo "  Force killing $name..."
                kill -9 "$pid" 2>/dev/null || true
            fi
            echo "  $name stopped"
        fi
        rm -f "$pidfile"
    fi
}

stop_from_pidfile "ipc.pid" "main"
stop_from_pidfile "controller.pid" "controller"
stop_from_pidfile "worker.pid" "worker"
stop_from_pidfile "logger.pid" "logger"

echo "Cleaning up FIFOs..."
rm -f /tmp/ipc_*.fifo 2>/dev/null || true

echo "system stop"