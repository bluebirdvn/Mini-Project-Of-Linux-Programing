# Signal-Aware Multithreaded Daemon

A simple C daemon that demonstrates how to handle **signals safely** in a **multithreaded** program, combined with a **producer-consumer** pattern using a shared work queue.

## Core Idea

- A **dedicated thread** (`sig_thread`) is responsible for waiting on signals using `sigwaitinfo()`, after the process blocks those signals via `pthread_sigmask`. This avoids the pitfalls of traditional async signal handlers.
- When a signal arrives, this thread converts it into a "job" (`workqueue`) and pushes it onto a shared work queue.
- Multiple **worker threads** (5 by default) continuously pop jobs from the queue and process them.
- The queue is a **bounded queue** (capacity 32), synchronized with a `mutex` and two `condition variables` (`not_empty`, `not_full`).

## Supported Jobs / Signals

| Signal    | Job                    | Meaning                                   |
|-----------|------------------------|--------------------------------------------|
| `SIGUSR1` | `JOB_DO_WORK`          | A worker simulates doing work (sleeps for the configured `delay`) |
| `SIGHUP`  | `JOB_RELOAD_CONFIG`    | Reload the configuration file              |
| `SIGTERM` / `SIGINT` | `JOB_SHUTDOWN` | Gracefully shut down the daemon            |

*(`JOB_PRINT_STATUS` is also defined to print daemon status, though it isn't currently wired to a specific signal.)*

## Configuration

The daemon reads a `key=value` style config file, one parameter per line. Lines starting with `#` are treated as comments. Supported parameters:

```
sample_rate=1000
delay=500
log_level=INFO
```

- `sample_rate`: sampling frequency (default 1000)
- `delay`: simulated worker processing time in milliseconds (default 500)
- `log_level`: log level (default INFO)

The config file is locked with `flock()` while being read to avoid conflicts.

## Directory Structure

```
.
├── config/
│   └── config.txt        # Default configuration file
├── inc/
│   ├── work_queue.c       # (Work queue implementation)
│   └── work_queue.h
├── main.c                 # Main daemon logic
├── Makefile
└── README.md
```

## How to Run

```bash
make
./signal-aware-daemon [path_to_config_file]
```

If no argument is given, the daemon defaults to `./config/config.txt`.

While the daemon is running, you can send signals to trigger different jobs, for example:

```bash
kill -SIGUSR1 <pid>   # Trigger a worker to do work
kill -SIGHUP  <pid>   # Reload configuration
kill -SIGTERM <pid>   # Gracefully shut down the daemon
```

## General Flow

1. `main()` initializes state, reads the config file, and spawns the worker threads plus one signal thread.
2. `sig_thread` waits for signals via `sigwaitinfo()`; when one arrives, it enqueues the corresponding job.
3. Each `worker_thread` continuously dequeues jobs and processes them based on job type.
4. On `SIGTERM`/`SIGINT`, the daemon sets a `shutting_down` flag, wakes up all waiting workers, lets them finish any remaining jobs, then exits.
5. `main()` waits for all threads to finish (`pthread_join`), frees resources, and exits.

## Note

This is a learning/practice project (in the spirit of *The Linux Programming Interface*), focused on demonstrating safe signal handling in a multithreaded environment. It is not intended as a production-ready daemon.