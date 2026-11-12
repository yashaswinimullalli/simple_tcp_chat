# Technical Comparison: Concurrent TCP Server Architectures

**Assignment Title:** Concurrent TCP Chat Server  
**Task:** Use Berkeley sockets to handle multiple clients. Compare **fork-per-client**, **thread-per-client**, and **select()/poll()** approaches.

---

## 1. Executive Summary

A network server using standard Berkeley sockets faces a fundamental challenge: network system calls like `accept()`, `recv()`, and `send()` are synchronous and blocking by default. If a single-threaded server blocks while waiting for data from Client A, it cannot accept incoming connections or service Client B.

To handle multiple simultaneous clients concurrently, three foundational paradigms exist:
1. **Multi-Process (`fork()`-per-client)**
2. **Multi-Threaded (Thread-per-client)**
3. **I/O Multiplexing (`select()` / `poll()`)**

This report analyzes and contrasts these three approaches based on memory overhead, context-switching latency, inter-client synchronization, reliability, and cross-platform portability.

---

## 2. Approach 1: Multi-Process (`fork()`-per-Client)

### Architecture
In this model, the parent process acts solely as a listener/dispatcher. Upon accepting an incoming client connection, it calls `fork()` to spawn an independent child process dedicated to that client.

```
                  +-------------------------+
                  | Parent Process (PID 100)|
                  |     Listening on 9090   |
                  +------------+------------+
                               | accept()
                        +------+------+
                        |   fork()    |
                        +------+------+
                               |
            +------------------+------------------+
            |                                     |
+-----------v-----------+             +-----------v-----------+
|  Child 1 (PID 101)    |             |  Child 2 (PID 102)    |
| Handles Socket fd 4   |             | Handles Socket fd 5   |
+-----------------------+             +-----------------------+
```

### Key Technical Characteristics
* **Memory Isolation:** Each child process receives its own virtual address space and duplicate file descriptors. On modern Linux systems, this uses **Copy-on-Write (CoW)** to defer physical memory duplication.
* **Broadcasting & IPC Complexity:** Because each child process has an isolated memory space, sharing state (such as broadcasting a chat message to all other connected clients) requires Inter-Process Communication (IPC)—such as POSIX shared memory (`mmap(MAP_SHARED)` / `shmget`), UNIX domain sockets, or named pipes.
* **Process Lifecycle & Zombies:** When a child terminates, the OS retains its exit status until the parent collects it. A `SIGCHLD` signal handler must call `waitpid(-1, NULL, WNOHANG)` to reap terminated children and prevent zombie processes.
* **Platform Limitation:** `fork()` is native to POSIX systems (Linux, UNIX, WSL). Windows lacks a native `fork()` system call, making this architecture non-portable to Win32 environments without emulation layers (like Cygwin).

### Advantages
* **Strong Fault Isolation:** If one client sends malformed data that causes a segmentation fault or memory corruption in its handler, only that child crashes. The parent server and all other clients remain fully functional.
* **Security Sandboxing:** Children can drop privileges (`setuid()`) or run in chroot/sandboxes independently.

### Disadvantages
* **High Resource Overhead:** Spawning processes incurs high kernel overhead (copying page tables, allocating Process Control Blocks).
* **Heavy Context Switches:** Switching between processes requires invalidating or flushing the CPU Translation Lookaside Buffer (TLB), significantly increasing CPU cache misses.
* **Poor Scalability:** Operating systems typically limit processes to a few thousand, making it unsuitable for high-concurrency systems.

---

## 3. Approach 2: Multi-Threaded (Thread-per-Client)

### Architecture
In this architecture, the server runs within a single operating system process. The main thread executes the `accept()` loop, and spawns a lightweight thread (POSIX `pthread_create()` or Win32 `CreateThread()`) for each accepted client.

```
+-------------------------------------------------------------+
|                      Server Process                         |
|                                                             |
|  [Main Thread]  ──accept()──> pthread_create()              |
|                                     │                       |
|         ┌───────────────────────────┼──────────────────┐    |
|         ▼                           ▼                  ▼    |
|  [Thread 1: Client A]      [Thread 2: Client B]   [Shared]  |
|   recv() / send()           recv() / send()       Clients[] |
|                                                    Mutex 🔒 |
+-------------------------------------------------------------+
```

### Key Technical Characteristics
* **Shared Virtual Address Space:** All threads share the same heap, global variables, and open file descriptors, while maintaining their own private thread stack (typically 1–8 MB) and registers.
* **Trivial State Sharing:** Broadcasting messages is straightforward. Any thread can iterate through a global `clients[]` array and invoke `send()` directly.
* **Synchronization Requirement:** Because multiple threads access and mutate shared data simultaneously, access must be synchronized using mutexes (`pthread_mutex_t` on Linux, `CRITICAL_SECTION` on Windows) to prevent race conditions and memory corruption.

### Advantages
* **Lower Overhead than Processes:** Thread creation is much faster than `fork()`, and memory consumption is significantly lower.
* **Fast Context Switching:** Switching between threads of the same process preserves the page table and TLB cache, resulting in lower CPU overhead.
* **Broad Platform Support:** Natively supported across POSIX and Windows operating systems.

### Disadvantages
* **Zero Fault Isolation:** A crash (e.g., NULL pointer dereference or stack overflow) in any single client thread terminates the entire server process.
* **Concurrency Hazards:** Prone to deadlocks, race conditions, thread starvation, and priority inversions if locking mechanisms are poorly designed.
* **Stack Memory Limits:** 10,000 threads with 2 MB stack allocations consume ~20 GB of virtual memory just for thread stacks, causing high overhead when connections are mostly idle.

---

## 4. Approach 3: I/O Multiplexing (`select()` / `poll()`)

### Architecture
I/O Multiplexing operates on an **event-driven, single-threaded model**. Instead of dedicating a thread or process to block on each socket, the server registers all active sockets with the operating system kernel via `select()` or `poll()`. The kernel wakes up the server only when one or more sockets are ready for non-blocking I/O.

```
[All Active Sockets] ──> select(max_fd + 1, &read_fds, ...)
                                     │
                        (Kernel checks readiness)
                                     │
                             Returns Ready Count
                                     │
                ┌────────────────────┴────────────────────┐
                ▼                                         ▼
         FD_ISSET(server_fd)                      FD_ISSET(client_fd)
       Accept new connection                     Read and broadcast data
```

### Key Technical Characteristics
* **Single Thread of Execution:** A single loop handles all client connections, incoming data, and message distribution sequentially.
* **Bitmask Mechanism (`select()`):** Sockets are passed to the kernel via bitmasks (`fd_set`). The maximum descriptor value is constrained by `FD_SETSIZE` (typically 1024 on POSIX, 64 on Windows).
* **`poll()` Improvement:** `poll()` replaces the fixed-size bitmask with a dynamically sized array of `struct pollfd`, eliminating the `FD_SETSIZE` hard limit.
* **Non-Blocking Operation:** Sockets are marked non-blocking; the loop processes ready descriptors immediately without waiting.

### Advantages
* **Minimal Resource Footprint:** Consumes minimal memory since there are no per-client thread stacks or process tables.
* **No Synchronization Required:** Because execution is single-threaded, race conditions, deadlocks, and mutex locks are completely eliminated.
* **Ideal for Many Idle Connections:** Highly efficient when hundreds of clients remain connected but transmit messages infrequently.

### Disadvantages
* **$O(N)$ Scanning Overhead:** Both `select()` and `poll()` require the kernel to iterate over all $N$ registered descriptors to check readiness. As $N$ grows, CPU time spent scanning linear arrays increases significantly.
* **Buffer Copy Overhead:** On every call to `select()`, the descriptor sets must be copied from user space to kernel space and back.
* **Head-of-Line Blocking:** If processing a message takes a long time (e.g., intensive compute, database query, disk I/O), the entire server blocks and cannot service other clients until that task finishes.
* **Windows Limitation:** On Windows Winsock, `select()` only accepts network socket handles; it cannot monitor standard input (`STDIN_FILENO`), preventing a unified keyboard/socket loop without secondary threads.

---

## 5. Comprehensive Comparison Matrix

| Evaluation Criteria | `fork()`-per-Client | Thread-per-Client | `select()` / `poll()` |
| :--- | :--- | :--- | :--- |
| **Execution Model** | Multi-Process | Multi-Threaded | Event-Driven (Single Thread) |
| **Creation Cost** | High (full process copy) | Moderate (thread stack) | None (zero runtime allocation) |
| **Memory per Client** | High (several MBs + CoW) | Moderate (stack: 512KB–8MB) | Minimal (few bytes for state) |
| **Context Switch Overhead**| Heavy (flushes TLB/caches) | Lightweight | None (in-process event loop) |
| **Inter-Client Sharing** | Complex (Requires IPC/shm) | Simple (Shared heap + Mutex) | Trivial (Same memory, no locks) |
| **Crash Resilience** | **Excellent** (process isolated)| **Poor** (crashes whole server) | **Poor** (crashes whole server) |
| **Concurrency Ceiling** | Low (~100 - 500) | Moderate (~1,000 - 5,000) | High (~1,000 - 10,000) |
| **CPU Saturation Vulnerability**| Immune (OS preempts processes)| Protected by OS scheduling | High (a slow handler blocks all) |
| **Cross-Platform Portability**| POSIX only (Linux/WSL) | Cross-platform | Cross-platform (with Win32 caveats) |

---

## 6. Modern Evolution: Solving the C10K Problem

While `select()` and `poll()` were foundational for early network servers, they hit a performance wall when scaling to 10,000+ concurrent connections (**The C10K Problem**) due to their $O(N)$ scanning cost.

Modern production-grade servers (e.g., NGINX, Redis, Node.js) utilize $O(1)$ event notification mechanisms provided by operating system kernels:

1. **Linux — `epoll` (`epoll_create`, `epoll_ctl`, `epoll_wait`):**  
   The kernel maintains an internal red-black tree and ready-list of monitored descriptors. Instead of scanning all sockets, `epoll_wait` returns only the sockets that had active events in $O(1)$ time.
2. **BSD / macOS — `kqueue`:**  
   Kernel event notification mechanism providing scalable, filter-based $O(1)$ monitoring.
3. **Windows — I/O Completion Ports (IOCP):**  
   Asynchronous overlapped I/O combined with kernel-managed thread pools, representing the gold standard for high-performance Windows network servers.

---

## 7. Conclusion

* **Choose `fork()`** when absolute fault isolation and security sandboxing are critical and connection counts remain small (e.g., legacy UNIX daemons, SSH sessions).
* **Choose Thread-per-Client** for moderate concurrency where tasks involve blocking computations or when building cross-platform applications with shared in-memory state.
* **Choose `select()` / `poll()`** for high-volume, I/O-bound connections where clients are mostly idle, memory efficiency is paramount, and lock-free simplicity is desired.

