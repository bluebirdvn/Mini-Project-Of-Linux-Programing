# Unix Domain Socket Control Plane

A sample control-plane server/client, communicating over **Unix Domain Socket** (`AF_UNIX`), written in C/C++ with a multi-threaded architecture, priority queues, and a custom framing protocol with checksum.

This is a **learning / systems-practice** project (multi-model I/O with select/poll/epoll, pthread, synchronization, binary protocol design), suitable for exploring how to build a device control-plane server, similar to an IoT gateway: receiving commands from clients, classifying them by priority, processing them asynchronously, and returning results (ACK/NACK).

---

## 1. Purpose

Simulates a **gateway server** that accepts connections from multiple clients (devices/controllers) over a local socket (no network involved, same-machine only). Each client sends binary control packets (get/set status, configuration, sensor data...). The server:

- Reads data from multiple clients simultaneously using non-blocking I/O.
- Wraps each received command into a `task`, classified by priority level (HIGH/MEDIUM/LOW).
- Pushes the task into the corresponding queue; groups of **worker threads** per priority level pick it up and process it.
- Sends the result (ACK/NACK) back to the client through a non-blocking send queue.
- Manages client **sessions** via tokens, allowing re-identification (resume) when a client reconnects.

The `client` side (`gatewaycli`) acts as a simulated client: connects to the server, sends an identification token, then randomly generates control commands to test the server's entire processing flow.

---

## 2. Overall Architecture

```
                 AF_UNIX socket (/tmp/myapp.sock)
 ┌────────────┐  ───────────────────────────────►  ┌──────────────┐
 │  gatewaycli │                                    │   gatewayd    │
 │  (client)   │  ◄───────────────────────────────  │   (server)    │
 └────────────┘                                     └──────────────┘
```

### 2.1. Server (`gatewayd`)

The server is organized into multiple independent threads sharing a common state struct `struct server`:

| Thread | Role |
|---|---|
| `sig_thread` | Catches `SIGINT`/`SIGTERM`/`SIGHUP`/`SIGUSR1` via `sigwaitinfo`, triggers a full system shutdown |
| `IO_thread` | Main I/O loop: accepts new connections, performs non-blocking read/write with clients, pushes valid packets into the priority queues, and reaps clients that have disconnected |
| `high_pri_worker_thread` × N | Prioritizes pulling tasks from the HIGH queue; if empty, "borrows" from MEDIUM, then LOW |
| `mid_pri_worker_thread` × N | Pulls tasks from the MEDIUM queue; if empty, from LOW |
| `low_pri_worker_thread` × N | Only pulls tasks from the LOW queue |

Multi-client I/O is abstracted through 3 mutually exclusive macros, one selected at compile time:

```c
#define USING_EPOLL  0
#define USING_POLL   0
#define USING_SELECT 1
```

`select()` is used by default. You can switch to `poll()` or `epoll()` by editing these 3 macros in `gatewayd/server.h`.

### 2.2. Client (`gatewaycli`)

The client is also multi-threaded:

| Thread | Role |
|---|---|
| `sig_thread` | Catches `SIGINT`/`SIGTERM` for a safe shutdown |
| `recv_thread` | Uses `epoll` to wait for data from the server, parses packets via a state machine (`READ_HEADER → READ_PAYLOAD → READ_CHECKSUM`), automatically generates ACK/NACK responses |
| `send_thread` | Pulls messages from the send queue (`sent_queue`) and sends them over the socket |
| `task_create_thread` | Periodically (every 1s) randomly generates a control command (GET_STATUS, SET_CONFIG, SEND_CONTROL...) and pushes it into the send queue |
| `worker_thread` | Pulls received messages from the receive queue (`recv_queue`) to "process" them (currently just logs them) |

On startup, the client immediately sends an `IDENTIFY` packet containing a fixed token so the server can create/restore the session.

---

## 3. Wire Protocol (framing protocol)

Each packet has a fixed structure with 3 parts, sent sequentially over the socket (length-prefixed framing):

```
┌─────────────────────┐
│ header_message       │  cmd, pri, number_packet, length
├─────────────────────┤
│ payload (length byte)│  up to MAX_PAYLOAD_SIZE = 2048 bytes
├─────────────────────┤
│ checksum (4 bytes)    │  16-bit Internet-checksum style over header + payload
└─────────────────────┘
```

```c
struct header_message {
    enum command cmd;
    enum priority pri;
    uint64_t number_packet;
    int length;
};

struct message_control {
    struct header_message header;
    char message[MAX_PAYLOAD_SIZE];
    uint32_t checksum;
};
```

Packet reading is implemented as a **3-stage state machine** (`READ_HEADER → READ_PAYLOAD → READ_CHECKSUM`) to support non-blocking I/O — each `recv()` call may only read part of the data, and the partially-read state is preserved in `client->stage` and `client->bytes_read` to continue on the next pass.

### Command Set (`enum command`)

| Command | Meaning |
|---|---|
| `GET_STATUS` / `SET_STATUS` | Read/write device status |
| `GET_DATA` | Read sensor data |
| `GET_CONFIG` / `SET_CONFIG` | Read/write configuration |
| `SEND_CONTROL` | Send an actuator control command |
| `ACK` / `NACK` | Acknowledge / reject a packet |
| `IDENTIFY` | Identify session via token |

### Priority Levels (`enum priority`): `LOW`, `MEDIUM`, `HIGH`

---

## 4. Session Management

The server maintains a session table (`struct session_table`) with up to `MAX_SESSIONS` slots. When an `IDENTIFY` packet is received:

- If the token already exists → treated as a **reconnect**, increments `reconnect_count`, updates `last_seen_ms`.
- If not → assigns a new `client_id`, creates a session (**NEW**).
- If the table is full → returns a NACK "session table full".

The session is bound (`client->session`) to the current TCP/socket connection, allowing tracking of sent/received packet counts (`packets_in_total`, `packets_out_total`) across the device's lifetime, even if the client disconnects and reconnects with the same token.

---

## 5. Priority Queues & Task Processing Model

- Every valid packet read from a client is wrapped into a `struct task` and pushed into one of 3 queues (`high_priority_queue`, `medium_priority_queue`, `low_priority_queue`) based on `header.pri`.
- Worker threads (`NUM_HIGH_WORKERS`, `NUM_MID_WORKERS`, `NUM_LOW_WORKERS` — configured in `server.h`) wait on `pthread_cond_t queue_wakeup`, which wakes them when a new task arrives.
- If a queue is full (`CAPACITY = 1024`) or the server is shutting down, the packet is rejected and the server returns a NACK "SERVER BUSY" to the client.
- `process_task()` dispatches the task to the appropriate handler function (`get_status_task`, `set_config_task`, `identify_task`, ...), then returns the result to the client via `queue_client()` (pushed into the client's non-blocking send queue; `send_thread`/`IO_thread` performs the actual send).

---

## 6. Directory Structure

```
.
├── bin/            # Built binaries: client, server; app.log
├── common/         # Shared code for both client & server
│   ├── common.h    # Protocol definitions, checksum, framing, connect_to_server()
│   ├── logger.c    # Logging system implementation (LOG_INFO/LOG_WARN/LOG_ERROR)
│   └── logger.h
├── gatewaycli/     # Client source code
│   ├── client.c
│   └── client.h
├── gatewayd/       # Server source code
│   ├── server.c
│   └── server.h
├── log/            # Runtime log directory
├── obj/            # Intermediate .o files from build
├── Makefile
└── README.md
```

---

## 7. Configuration (compile-time)

Some important parameters can be adjusted in `gatewayd/server.h` and `common/common.h`:

| Macro | Meaning | Default |
|---|---|---|
| `PATH_SOCKET` | Unix socket path | `/tmp/myapp.sock` |
| `MAX_PAYLOAD_SIZE` | Maximum payload size | 2048 bytes |
| `SESSION_TOKEN_LEN` | Session token length | 64 |
| `CAPACITY` | Capacity of each priority queue | 1024 tasks |
| `MAX_SESSIONS` | Maximum number of sessions | 256 |
| `NUM_HIGH_WORKERS` / `NUM_MID_WORKERS` / `NUM_LOW_WORKERS` | Number of worker threads per priority level | 2 / 1 / 1 |
| `MAX_FRAMES_PER_CLIENT_PER_LOOP` | Max frames read consecutively per client per I/O loop | 32 |
| `NUM_PACKET_TO_SEND` | Send queue capacity per client | 16 |
| `USING_SELECT/POLL/EPOLL` | Selects the multi-client I/O mechanism | `USING_SELECT=1` |

---

## 8. Build & Run

### Requirements

- Linux (uses `AF_UNIX`, `epoll`, `pthread`, `sigwaitinfo` — Linux-specific APIs)
- `g++` supporting C++17 or later
- `make`

### Build

```bash
make            # builds both server and client into the bin/ directory
make server     # build server only
make client     # build client only
```

### Run

Open 2 separate terminals, run the server first:

```bash
./bin/server
```

Then run one or more clients in (other) terminals:

```bash
./bin/client
```

Convenience targets are also available:

```bash
make run-server
make run-client
```

Stop with `Ctrl+C` (both server and client catch `SIGINT` for a safe shutdown, closing sockets and exiting threads).

### Cleanup

```bash
make clean       # removes obj/, bin/, log/*.log
```

---

## 9. Logging

The logging system (`common/logger.h`) provides `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` macros used throughout both server and client, writing to `bin/app.log` / the `log/` directory (depending on configuration in `logger.c`) to track connection lifecycle, sessions, frame errors, etc.

---

## 10. Development Status / Known Limitations

This project is still under development. A few things to note when reading/extending the source code:

- The outgoing data packaging mechanism (`send_message_chunk` in `server.h`) currently assumes the payload always fills the entire `MAX_PAYLOAD_SIZE`; this needs to be carefully cross-checked against the actual layout of `struct message_control` before trusting the checksum on the client side.
- Some mutex-locking branches (`client->lock` vs `client->sent_lock`) in `gatewaycli/client.c` need review to avoid deadlocks/race conditions when adding more threads.
- `dequeue_client()` in `server.h` is currently unused (dead code); the send-queue advancement logic (`advance_send_queue`) needs to be completed before it is called.

The items above are suitable as a checklist for the next review/refactor pass on the project.

---

## 11. Future Directions

- Add a stronger token authentication mechanism (currently uses a fixed string for testing).
- Support TLS/encryption if expanding to TCP sockets instead of just local Unix domain sockets.
- Add unit tests for the packet-reading state machine and the checksum mechanism.
- Add a heartbeat/keepalive mechanism to detect "dead" clients that don't close the socket properly.