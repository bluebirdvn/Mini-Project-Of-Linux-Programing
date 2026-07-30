# Gateway Core (POSIX IPC)

A simple Linux gateway system demonstrating POSIX IPC mechanisms: message queues, shared memory, semaphores, and mutexes, used to simulate a sensor/controller communication pipeline.

## Overview

The project has two main programs sharing a common IPC layer:

- **`controller`** — creates/opens the shared IPC resources and sends test commands to `sensor` through a POSIX message queue.
- **`sensor`** — receives commands from the queue, processes them, and reads/writes sensor data from a shared-memory ring buffer.

Both programs communicate through:

- **POSIX Message Queue** (`/gateway_queue`) — used to send commands (`GET_DATA`, `STORAGE_DATA`, `GET_STATUS`, `LOW_ENERGY`, `SHUTDOWN`).
- **POSIX Shared Memory** (`/gateway_shm`) — a ring buffer of `sensor_data` records, protected by a process-shared mutex and two counting semaphores (`items`, `spaces`) implementing a classic bounded-buffer producer/consumer pattern.

## Files

| File | Purpose |
|---|---|
| `common.h` | Shared data structures, constants, and function declarations |
| `common.c` | IPC helpers (mq, shm, semaphore wrappers) and command handling logic |
| `controller.c` | Creates IPC resources and sends test commands |
| `sensor.c` | Receives and processes commands, handles graceful shutdown |

## Build

```bash
gcc -Wall -o controller controller.c common.c -lrt -lpthread
gcc -Wall -o sensor sensor.c common.c -lrt -lpthread
```

(Adjust to your actual Makefile/build system if you have one.)

## Usage

Run `sensor` first (it can create or attach to existing IPC resources), then run `controller` to send test commands:

```bash
./sensor &
./controller
```

Press `Ctrl+C` on either program to trigger a graceful shutdown:
- The signal handler sets a shutdown flag and stops the worker thread's main loop.
- Any pending messages in the queue are drained before resources are released.
- IPC resources (message queue, shared memory) are only unlinked by the creator process.

## Commands (message types)

| Type | Name | Action |
|---|---|---|
| 1 | `GET_DATA` | Read one entry from the shared-memory ring buffer (non-blocking; reports if empty) |
| 2 | `STORAGE_DATA` | Write a new sensor entry into the ring buffer (non-blocking; drops and counts if full) |
| 3 | `GET_STATUS` | Print current buffer indices and dropped-item count |
| 4 | `LOW_ENERGY` | Placeholder for low-power mode handling |
| 5 | `SHUTDOWN` | Requests a full shutdown of the receiving process |

## Notes / Known Design Points

- Shared-memory access is synchronized with a `PTHREAD_PROCESS_SHARED` mutex plus two semaphores (`items` for available data, `spaces` for free slots).
- Semaphore waits use non-blocking `sem_trywait` for `GET_DATA`/`STORAGE_DATA` to avoid deadlocking the single worker thread on an empty/full buffer.
- Shutdown is coordinated through a single path: OS signal (`SIGINT`/`SIGTERM`) or an in-band `SHUTDOWN` message both funnel into the same signal-handling thread, which is the only place that calls `shutdown_ipc()`, after all worker threads have exited.

## Requirements

- Linux with POSIX real-time extensions (`librt`) and `pthread`.
- GCC or any C11-compatible compiler.