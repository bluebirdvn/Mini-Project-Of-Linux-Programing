#!/bin/bash
if [ "$EUID" -ne 0 ]; then
echo "error run by sudo: sudo ./test.sh"
exit 1
fi

echo "client run in node2 (IP: 192.168.1.20)..."

ip netns exec node2 ../client/client_tls 192.168.1.10 5556