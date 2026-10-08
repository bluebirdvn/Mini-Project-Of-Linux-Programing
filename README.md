# Linux Systems Programming

This repository is a collection of 7 distinct mini-projects demonstrating deep, low-level **Linux Systems Programming** and **Inter-Process Communication (IPC)** in C. It serves as a practical implementation of concepts from *The Linux Programming Interface (TLPI)* and modern POSIX standards.

## Technology & APIs
*   **Concurrency:** `pthreads`, Mutexes, Condition Variables, Producer-Consumer thread pools.
*   **Networking:** TCP/UDP Sockets, `epoll`/`poll`/`select` (I/O Multiplexing), OpenSSL (TLS), UDP Broadcast Discovery.
*   **POSIX IPC:** `shm_open`, `mq_open`, `sem_open` (Named Semaphores).
*   **System V IPC:** `shmget`, `msgget`, `semget`.
*   **Local IPC:** `AF_UNIX` (Unix Domain Sockets), Named Pipes (FIFO).
*   **OS Internals:** `fork`, `execvp`, Process Groups, Signals (`sigwaitinfo`, `sigprocmask`), `eventfd`, `inotify`, Atomic file operations (`flock`, `rename`).

---

## Project Modules

### 1. `gateway/` (High-Performance Secure Gateway)
A multithreaded IoT gateway daemon featuring a **TLS-secured** TCP socket server.
*   Uses `epoll` for non-blocking I/O and `eventfd` to wake up the main event loop.
*   Implements a custom binary framing protocol (Header + JSON Payload + CRC32).
*   Features a UDP Broadcast Discovery service for zero-config client connections.
*   Includes Token Bucket rate-limiting and robust Thread Pool task dispatching.

### 2. `unix-domain-socket-control-plane/` (Local Control Plane)
A client-server architecture communicating entirely over **Unix Domain Sockets (`AF_UNIX`)**.
*   Implements a 3-stage non-blocking state machine (`READ_HEADER` → `READ_PAYLOAD` → `READ_CHECKSUM`) to prevent partial-read deadlocks.
*   Features Priority Queuing (High/Medium/Low) for task scheduling.

### 3. `gateway-core-posix/` (POSIX IPC Simulation)
Simulates a high-frequency sensor hub using modern **POSIX IPC**.
*   Utilizes POSIX Shared Memory as a zero-copy ring buffer.
*   Synchronized via Process-Shared Mutexes and Counting Semaphores (`items` and `spaces`).
*   Command routing handled by POSIX Message Queues.

### 4. `system-V-ipc-sensorhub/` (Legacy IPC Hub)
A classic implementation of the sensor hub utilizing **System V IPC**.
*   Demonstrates mastery of `ftok`, `shmget`, `msgget`, and strict `semop` locking arrays.

### 5. `ipc-cmd-pipeline/` (FIFO Pipeline)
A multiprocess pipeline orchestrated via **Named Pipes (FIFO)**.
*   The parent process uses `fork` and `execv` to spawn Controller, Worker, and Logger processes.
*   Implements an ARQ (Stop-and-Wait) protocol directly over raw FIFOs.

### 6. `process-supervisor/` (Mini-Systemd)
A lightweight daemon supervisor for managing unstable worker processes.
*   Prevents zombie/orphan processes using Process Group isolation (`setpgid`).
*   Implements a nanosecond-precision timeout enforcer using `pselect`.
*   Redirects child `STDOUT`/`STDERR` safely via anonymous `pipe()`.

### 7. `safe-config-tool/` & `signal-aware-multithreaded-daemon/`
Tools for robust, power-loss safe configuration management.
*   Guarantees atomic file updates using `flock()`, `fsync()`, and `rename()`.
*   Uses `inotify` for real-time file tracking without polling.
*   Handles asynchronous OS signals safely in a multithreaded context by blocking them globally and catching them via a dedicated `sigwaitinfo` thread.

---

## How to Build & Run
Each module is completely self-contained with its own `Makefile`. Navigate to any directory and run:

```bash
cd <module_name>
make
