#!/bin/bash

NS1="node1"
NS2="node2"
VETH1="veth1"
VETH2="veth2"
IP1="192.168.1.10"
IP2="192.168.1.20"

if [ "$EUID" -ne 0 ]; then
  exit 1
fi

create_namespace() {
    echo "create network namespaces: $NS1, $NS2..."
    ip netns add $NS1 2>/dev/null || echo "Namespace $NS1 existed."
    ip netns add $NS2 2>/dev/null || echo "Namespace $NS2 existed."

    echo "create virtual net"
    ip link add $VETH1 type veth peer name $VETH2 2>/dev/null

    ip link set $VETH1 netns $NS1
    ip link set $VETH2 netns $NS2

    ip netns exec $NS1 ip addr add $IP1/24 dev $VETH1
    ip netns exec $NS1 ip link set $VETH1 up
    ip netns exec $NS1 ip link set lo up

    ip netns exec $NS2 ip addr add $IP2/24 dev $VETH2
    ip netns exec $NS2 ip link set $VETH2 up
    ip netns exec $NS2 ip link set lo up

    echo "success"
}

clean_namespace() {
    ip netns delete $NS1 2>/dev/null || echo "not found $NS1."
    ip netns delete $NS2 2>/dev/null || echo "not found $NS2."

}

case "$1" in
    start)
        create_namespace
        ;;
    stop)
        clean_namespace
        ;;
    *)
        echo "instruction: sudo $0 {start|stop}"
        echo "  start : config Network Namespaces"
        echo "  stop  : delete all Network Namespaces"
        exit 1
esac