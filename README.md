# OS/161 Kernel (CPEN 331 – UBC)

This repo contains my OS/161 kernel work from CPEN 331 (Operating Systems) at UBC.  
The goal of the project was to understand how an operating system works by actually building core pieces of one: synchronization, system calls, process/file management, and virtual memory.

---

## Overview

Over the term we started with the OS/161 base code and incrementally added functionality until the system could run real user programs reliably. The work is split across three major areas:

1) **Synchronization primitives** used throughout the kernel  
2) **System calls** (file + process) and the user↔kernel transfer path  
3) **Virtual memory** (TLB faults, page-level memory management, and heap growth)

---

## Synchronization primitives

Early on, we implemented our own synchronization layer so later parts of the kernel could be written safely.

What I built:
- **Locks** implemented using a **binary semaphore** for mutual exclusion
- **Condition variables** implemented using **wait channels**, so threads can sleep and wake correctly without busy waiting

These primitives are used later in the file subsystem, process management, and VM code to avoid races and keep shared kernel state consistent.

**Where to look**
- `kern/thread/synch.c`

---

## System calls + user↔kernel transfer

System calls are where user programs cross into the kernel. A big part of getting this right is not just writing the syscall logic, but also handling the “transfer code” properly.

What this includes:
- Fetching syscall arguments from the correct places (registers / user memory)
- Returning results in the correct registers following the OS/161 MIPS calling convention  
- Returning proper error codes instead of crashing on invalid inputs
- Handling special cases like syscalls with 64-bit values (e.g., `lseek`)

**Where to look**
- Syscall transfer/dispatch path: `kern/arch/mips/syscall/`
- Syscall implementations (general): `kern/syscall/`
- File syscall implementations (organized separately): `kern/file_syscalls/`

---

## File descriptors + open file handling

File-related syscalls are mostly about keeping correct per-process state. The kernel needs to map file descriptors to open file objects and track things like offsets, access modes, and shared references (especially with `dup2`, and later when processes are forked).

What I implemented:
- Per-process file descriptor tracking
- Open file structures that maintain file state (including offsets)
- Synchronization around shared file state so concurrent access remains safe

**Where to look**
- Data structures / interfaces:
  - `kern/include/filehandle.h`
  - `kern/include/open_file_handler.h`
- Syscalls using these structures:
  - `kern/file_syscalls/`

---

## Virtual memory

Later in the project, DUMBVM is replaced with a real VM subsystem. The focus here is handling address translation and memory growth correctly on a MIPS-style system.

What this part covers:
- Handling **TLB faults** and installing translations into the TLB
- Page-level memory management (allocating, tracking, and reclaiming pages)
- Supporting dynamic heap growth via **`sbrk`**, so user programs can use `malloc()` properly
- Keeping behavior stable under multi-process and memory-pressure tests

**Where to look**
- `kern/vm/`

---

## Build & run (typical workflow)

```bash
# Configure kernel
cd kern/conf
./config GENERIC   # earlier stages may use DUMBVM

# Build kernel
cd ../compile/GENERIC
bmake depend
bmake
bmake install

# Build userland (when needed)
cd ../../..
bmake
bmake install
