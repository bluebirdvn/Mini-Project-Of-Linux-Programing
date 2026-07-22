# ipc-cmd-pipeline

An inter-process communication (IPC) simulation system on Linux, using **FIFO (named pipes)** and **pthreads** to simulate the command-sending / processing / logging flow between 3 independent processes.

## Architecture

```
                    main (creates fifos, fork 3 child processes)
                     |
            ----------------------------------
        controller          worker         logger
```




- **controller**: generates random commands (PING, GET_DATA, SEND_CMD, CHECK_SEC, CHECK_STATUS), sends them through `ipc_cmd_pipeline.fifo`, and waits for an ACK through `ipc_ack.fifo`. Includes a resend mechanism on timeout.
- **worker**: receives commands from the controller, sends an ACK immediately, then processes the command (each command type has its own task) and writes the result to `ipc_log_pipeline.fifo`.
- **logger**: reads data from `ipc_log_pipeline.fifo` and prints it to the screen.
- **main**: the parent program, creates 3 FIFOs, `fork()`s + `execv()`s the 3 child processes, and waits (`wait()`) for them to finish.

### FIFOs used

| FIFO | Path | Role |
|---|---|---|
| Command | `/tmp/ipc_cmd_pipeline.fifo` | controller → worker (sends command packet) |
| Ack | `/tmp/ipc_ack.fifo` | worker → controller (acknowledges receipt) |
| Log | `/tmp/ipc_log_pipeline.fifo` | worker → logger (writes processing result log) |

## Directory structure

```
.
├── main.c              # Parent process: creates FIFOs, forks/execs controller-logger-worker
├── Makefile
├── controller/
│   ├── controller.c    # Generates commands, sends via FIFO, waits for ACK, resends on timeout
│   └── Makefile
├── worker/
│   ├── worker.c        # Receives commands, sends ACK, processes task, writes log
│   └── Makefile
├── logger/
│   ├── logger.c        # Reads and prints logs from FIFO
│   └── Makefile
├── build.sh            # Builds all 4 binaries
├── run.sh              # Runs the system (in background), saves PID, checks status
├── stop.sh             # Stops the entire system based on PID
├── clean.sh            # Runs make clean everywhere + removes binaries, logs, FIFOs
├── status.sh           # Checks the status of the currently running system
└── test/
    └── build.sh
```

## Environment requirements

- Linux (uses `mkfifo`, `fork`, `execv` — POSIX APIs)
- `gcc`, `make`
- `pthread` (compiled with `-lpthread`)

## Usage

### 1. Build
```bash
./build.sh
```
Compiles `main`, `controller`, `worker`, and `logger` in sequence. Stops immediately if any build step fails.

### 2. Run the system
```bash
./run.sh
```
- Automatically builds if the `ipc-cmd-pipeline` binary doesn't exist yet.
- Kills any leftover old process (based on `ipc.pid`).
- Runs `main` in the background (`nohup ... &`), logging to `main.log`.
- Prints the status of main, the child processes, the FIFOs, and the last few log lines.

### 3. Check status
```bash
./status.sh
```
Checks: whether main is running (via `ipc.pid`), whether the child processes `controller`/`worker`/`logger` are running (via `pgrep`), whether the FIFOs exist, and the most recent log.

### 4. Stop the system
```bash
./stop.sh
```
Sends `SIGTERM` to the processes according to the PID file, waits 1 second, sends `SIGKILL` if they haven't stopped, then removes all FIFOs.

### 5. Full cleanup
```bash
./clean.sh
```
Stops the system, runs `make clean` in every directory, removes binaries/logs/PID/FIFOs.

### 6. View logs
```bash
tail -f main.log
```

## Operation flow

1. `main` creates 3 FIFOs, forks and `execv`s the 3 child processes.
2. `controller` generates 1 random command, sends it through the command FIFO, waits up to `TIMEOUT` seconds for an ACK, resends up to `RESEND_NUM` times if the correct ACK isn't received.
3. `worker` receives the command, sends an ACK immediately, then processes the command according to its type (`ping_task`, `get_data_task`, `send_cmd_task`, `check_sec_task`, `check_status_task`) and writes the result to the log FIFO.
4. `logger` reads the log FIFO and prints: `[logger]: <content>`.
5. Repeats for the next command.

## Known Issues / TODO

- [ ] `build.sh`: uses `cd controller` then `cd logger`/`cd worker` — needs to be fixed to use correct relative paths from the root directory (`cd ../logger`, `cd ../worker`).
- [ ] `run.sh`: the current order of arguments passed to the main binary is `controller worker logger`, needs to match the order expected in `main.c` (`controller logger worker`).
- [ ] The synchronization mechanism using `pthread_cond_wait`/`pthread_cond_timedwait` between threads in `controller` and `worker` needs a predicate flag added to avoid lost wakeups, and the mutex must be locked before calling `cond_wait`/`cond_timedwait`.
- [ ] Clean up dead code (mutex locked twice in a row without unlocking) in `controller.c`.

## License

Personal learning / practice project (based on TLPI - The Linux Programming Interface).