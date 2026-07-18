# 🛠️ C Linux Process Supervisor (Mini-Systemd)

A robust, lightweight process manager written in POSIX-compliant C. Acting as a mini-`systemd`, this project is specifically designed for embedded systems and IoT Gateways (e.g., BeagleBone, Raspberry Pi) to ensure high availability of worker processes.

## ✨ Core Features

*   🔄 **Respawn Loop (`--restart`):** Automatically restarts child processes if they crash. Integrates a throttling mechanism (sleep) to prevent CPU-burning "crash-loops."
*   ⏱️ **Timeout Enforcer (`--timeout`):** Immediately terminates the child process if it exceeds the specified execution time, preventing I/O deadlocks or infinite hangs.
*   💣 **Process Group Kill (`-g`):** Prevents orphan and zombie processes. Spawns the child in a new Process Group and terminates the entire group (including grandchildren) with a single signal.
*   📝 **Safe Log Capture (`-c`):** Automatically redirects the child's `stdout` and `stderr` into an anonymous pipe. The parent process safely reads and manages the logs without cluttering the terminal output.
*   🧩 **Dynamic CLI Parsing:** Highly flexible command-line argument parsing. Flags can be placed in any order before the target command without confusing the supervisor with the child's own arguments.

---

## 🧠 System Programming Architecture (Under the Hood)

This project orchestrates several low-level Linux Kernel APIs:

1.  **Process & Lifecycle Management:**
    *   Utilizes `fork()` and `execvp()` to execute new programs.
    *   Uses `waitpid()` along with the `WIFEXITED` and `WIFSIGNALED` macros to reap zombie processes and analyze the exact cause of the child's termination.
2.  **Process Group Management (`setpgid` & `kill`):** 
    *   Solves critical race conditions: Both the parent and the child call `setpgid` to guarantee the Process Group is fully established before a `kill(-pid, SIGKILL)` command is ever invoked.
3.  **Inter-Process Communication (IPC - Unnamed Pipes):**
    *   Creates an anonymous pipe using `pipe()`.
    *   Employs `dup2()` to redirect the child's STDOUT (FD 1) and STDERR (FD 2) into the write-end of the pipe, while the parent monitors the read-end.
4.  **Advanced I/O Multiplexing (`pselect`):**
    *   The parent supervisor uses a **SINGLE** `pselect` call to concurrently handle three blocking events:
        *   Waiting for incoming log data from the Pipe.
        *   Counting down the Timeout (with nanosecond precision).
        *   Safely receiving interrupt signals using a `sigset_t` mask.
5.  **Safe Signal Handling (`sigaction` & `sigprocmask`):**
    *   Blocks `SIGCHLD`, `SIGINT`, and `SIGTERM` using `sigprocmask` before entering critical sections.
    *   Passes `&orig_set` into `pselect` to atomically unblock signals. This prevents the classic race condition where a signal arrives a microsecond before the process goes to sleep.

---

## 🚀 Usage Guide

### 1. Build Compilation
```bash
gcc -O2 -Wall supervisor.c -o supervisor

2. Syntax
Bash

./supervisor run [--timeout N] [--restart N] [-g] [-c] <command> [args...]

Note: You can place the flags (-c, -g, --timeout...) in any order before the target child command.
3. Practical Examples

Example 1: Standard Execution
Monitor the ping command and print output directly to the terminal:
Bash

./supervisor run /bin/ping 8.8.8.8

Example 2: Enforce a Timeout
Force the child process to sleep for 10 seconds, but the supervisor will terminate it after 3 seconds:
Bash

./supervisor run --timeout 3 /bin/sleep 10

Example 3: Auto-Restart (Respawn)
Run an unstable worker script and allow the supervisor to restart it up to 5 times if it crashes:
Bash

./supervisor run --restart 5 ./my_unstable_worker

Example 4: Ultimate Mode (Combined Features)
Limit execution to 5 seconds, restart up to 3 times, terminate the entire process group (-g), and capture all logs (-c) instead of printing them directly:
Bash

./supervisor run --timeout 5 --restart 3 -g -c ./worker_script.sh arg1 arg2

🛠️ Troubleshooting Common Errors

    [Worker] EXEC failed: Permission denied: Ensure that your target child executable (e.g., ./worker) has execution privileges granted (chmod +x worker).

    [Worker] EXEC failed: No such file or directory: The supervisor passes its Current Working Directory (CWD) to the child process. It is highly recommended to always use Absolute Paths for the command and its arguments to avoid path resolution errors.