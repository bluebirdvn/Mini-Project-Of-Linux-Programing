# Safe Config Toolkit for Embedded Linux

A highly robust, power-loss safe, and thread-safe configuration file processing toolkit written in C. It is highly optimized for Embedded Linux systems such as IoT Gateways, Smart Agriculture, and Smart Home devices.

This project solves a classic problem in embedded systems: **How to guarantee that a configuration file will not be corrupted if the device experiences a sudden power loss during a write operation?**

##  Core Features

1. **Atomic Overwrite:** Never writes directly to the original file. New data is securely written to a temporary (`.tmp`) file, forcefully flushed to physical storage using `fsync()`, and finally swapped using an atomic `rename()` system call. The configuration file is guaranteed to remain **100**% intact.
2. **Exclusive File Locking:** Utilizes `flock(LOCK_EX / LOCK_SH)` to prevent race conditions when multiple processes or threads attempt to read or write to the config file simultaneously.
3. **Zero-copy Backup:** Automatically generates a backup (`.bak`) file before applying any changes. It uses the `copy_file_range()` system call to duplicate data directly within the Kernel Space, minimizing **CPU** and **RAM** overhead.
4. **Real-time Event Watcher:** Includes a standalone daemon utilizing the `inotify` **API** to monitor the configuration directory. It instantly detects file changes (e.g., `MOVE_TO`, `**MODIFY**`) without relying on resource-intensive polling loops.

##  Project Structure

```text . ├── config/ │   └── config.txt      # Configuration directory and sample file ├── main.c              # Source code for the safe read/write tool (safe-config-tool) ├── notify.c            # Source code for the event watcher (inotify-test) ├── Makefile            # Build automation script └── **README**.md           # Project documentation

🛠️ Build Instructions

The project includes a standard Makefile. Open a terminal at the project root and run the following commands: Bash

# Compile the entire project

make

# Clean up executable and object files

make clean

The make command will generate two independent executable files:

    safe-config-tool: The primary utility for reading and writing parameters safely.

    inotify-test: A background application (watcher) to monitor file system events.

 Usage Guide ## Monitor the Configuration Directory (Terminal 1)

Start the watcher to monitor all filesystem events within the config/ directory. Leave this terminal open in the background to observe the logs. Bash

./inotify-test ./config/

## Read a Parameter (Terminal 2)

The tool will acquire a shared lock (LOCK_SH), parse the file to find the specified parameter, output its value, and release the lock. Bash

# Syntax: ./safe-config-tool read <path> <param>

./safe-config-tool read ./config/config.txt baudrate

## Write/Modify a Parameter (Terminal 2)

The tool executes the complete safe-write lifecycle: Acquire LOCK_EX -> Create Backup -> Write to .tmp -> fsync -> Atomic rename -> Unlock. This sequence will trigger real-time notifications on Terminal 1 (inotify). Bash

# Syntax: ./safe-config-tool write <path> <param> <value>

./safe-config-tool write ./config/config.txt frequency **1000**

(Note: If the parameter does not exist in the file, the tool will automatically append it to the end of the configuration file). 

Error Handling & Recovery Mechanisms

    Write Failures: If an error occurs while writing data to the .tmp file (e.g., storage full), the temporary file is immediately deleted (unlink), leaving the original configuration file untouched and safe.

    Atomic Rename Failures: If the rename() operation fails (e.g., cross-device link error or permission denied), the system will automatically call restore_file() to recover the original state using the previously generated .bak file.

Built with C & Linux System Calls.