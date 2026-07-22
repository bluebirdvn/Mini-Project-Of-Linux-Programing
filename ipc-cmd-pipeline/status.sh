
if [ -f "ipc.pid" ]; then
    MAIN_PID=$(cat ipc.pid)
    if kill -0 $MAIN_PID 2>/dev/null; then
        echo "main running (PID: $MAIN_PID)"
    else
        echo "main not running (stale PID: $MAIN_PID)"
    fi
else
    echo "main not running (no PID file)"
fi

echo ""
echo "Child processes:"
for name in controller worker logger; do
    if pgrep -f $name > /dev/null; then
        echo "  $name running"
    else
        echo "  $name not running"
    fi
done

echo ""
echo "FIFOs:"
for fifo in /tmp/ipc_*.fifo; do
    if [ -e "$fifo" ]; then
        echo "  ✓ $(basename $fifo) exists"
    fi
done

echo ""
echo "Log file:"
if [ -f "main.log" ]; then
    echo "  main.log ($(du -h main.log | cut -f1))"
    echo ""
    echo "Last 5 lines:"
    tail -5 main.log
else
    echo "  No log file"
fi