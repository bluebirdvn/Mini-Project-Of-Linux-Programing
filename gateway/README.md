# IoT Gateway

A multithreaded IoT gateway daemon written in C for Linux. It exposes a **TLS-secured** binary TCP protocol (JSON payload) for controlling sensors/actuators, plus a UDP discovery service for zero-configuration client connections.

## Features

- **TLS Encryption**: Secure TCP communication using OpenSSL.
- **UDP Discovery**: Clients broadcast a discovery request first, parse the gateway's response, and only then open the TLS/TCP session — no hardcoded IP needed.
- **Custom Binary Protocol**: Highly optimized frame format `Header (16 bytes) + JSON payload + CRC32 (4 bytes)`.
- **Thread Pool Architecture**: Non-blocking I/O with `epoll` and `eventfd` for high-performance concurrency.
- **Graceful Shutdown**: Dedicated signal-handling thread (`sigwait` + mutex/cond) lets both gateway and client shut down cleanly on `SIGTERM`.
- **Isolated Network Testing**: Built-in CI/CD scripts using Linux Network Namespaces (`node1`, `node2`) to simulate a real physical network.

## Architecture

![alt text](image/image.png)

- **main_thread**: Single-threaded `epoll` loop, only does I/O (accept, read, write). Never runs business logic.
- **worker pool** (4 threads): Pulls tasks from a bounded queue, talks to sensors/actuators, pushes results to a response queue.
- **eventfd**: Wakes `main_thread` up when a worker has a response ready.
- **Protocol Frame**: See `docs/protocol_spec.json` for the full message spec.

### Client connection flow

The TLS client (`client/client_tls.c`) follows this sequence on startup and on every reconnect:

1. Send a UDP discovery broadcast (`MSG_DISCOVERY`) to the given broadcast address/port.
2. Wait for the gateway's discovery response, verify its CRC, and parse out the real `host`/`port`.
3. Only then open the TCP connection and perform the TLS handshake against that discovered address.
4. Poll sensors and drive actuators as usual, with a dedicated signal thread watching for `SIGINT`/`SIGTERM` to trigger a graceful shutdown.

## Project Layout

```text
include/     Header files
src/         Gateway implementation
client/      Client implementation (client.c, client_tls.c)
scripts/     CI/CD and automation bash scripts (setup, run, stop, netns)
certs/       Generated TLS certificates (server.crt, server.key)
```

## Quick Start (Automated Scripts)

The project includes automated bash scripts to set up virtual network namespaces (`node1` for Server, `node2` for Client) and manage the Systemd service.

### 1. Build and Setup

Navigate to the `scripts` directory and run the setup script. It will generate self-signed TLS certificates, compile the source code, and install the `gateway` as a Systemd service.

```bash
cd scripts
sudo ./setup.sh
```

### 2. Start the Gateway Environment

This script initializes the virtual network (`veth` pair across `node1` and `node2`) and starts the Gateway Systemd service in `node1` (IP: 192.168.1.10).

```bash
sudo ./run.sh
```

Check the service logs in real-time:

```bash
tail -f ../log/gateway.log
```

### 3. Run the Smart Client

Run the TLS client in `node2`. It will broadcast a UDP discovery packet to find the gateway, parse the response, establish a TLS handshake, and start polling data.

```bash
sudo ip netns exec node2 ../client/client_tls 192.168.1.255 5556
```

(Alternatively, if you have a `test.sh` script: `sudo ./test.sh`)

### 4. Stop and Cleanup

Gracefully shutdown the gateway service, kill rogue clients, and destroy the virtual network namespaces.

```bash
sudo ./stop.sh
```

## Debugging

This project was built with robustness in mind. The following tools were used to find and fix bugs:

- **Sanitizers & Valgrind**: ASan/UBSan for memory leak and undefined behavior detection.
- **tcpdump / Wireshark**: Packet sniffing across network namespaces.

```bash
# Capture packets on the virtual interface
sudo ip netns exec node1 tcpdump -i veth1 -w gateway_traffic.pcap
# Analyze with Wireshark
wireshark gateway_traffic.pcap
```

- **strace & ltrace**: System call tracing.

## Known Limitations

- No Client Certificate Authentication (mTLS) yet, only Server-side TLS.
- No OTA (Over-the-Air) update mechanism for end-devices.
- No MQTT / Cloud backend integration.
- Single gateway instance, no high-availability clustering.