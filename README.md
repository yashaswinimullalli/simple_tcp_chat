# Concurrent TCP Chat Server

A complete implementation and comparative study of concurrent TCP chat servers using **Berkeley Sockets**, covering:
1. **I/O Multiplexing (`select()`)** — `server_select.c`
2. **Thread-per-Client (`pthread` / Win32)** — `server_thread.c`
3. **Fork-per-Client (Multi-Process / POSIX)** — `server_fork.c`

Includes a cross-platform client (`client.c`) and an in-depth technical analysis report ([`COMPARISON.md`](COMPARISON.md)).

---

## System Architecture

```mermaid
flowchart TD
    User(["Chat User"]):::userClass -->|"enters messages"| Client["Chat Client\n[client.c]"]:::clientClass

    subgraph DefaultServer["Default Server"]
        DefListener["Default Listener\n[server.c]"] -->|"spawns per client"| DefWorker["Client Worker\n[server.c]"]
        DefConsole["Console Input\n[server.c]"] -->|"broadcasts console input"| DefBroadcast["Message Broadcast\n[server.c]"]
        DefWorker -->|"broadcasts messages"| DefBroadcast
    end

    subgraph ServerVariants["Server Architecture Variants"]
        subgraph ForkGroup["Fork-per-Client"]
            ForkServ["Fork Server\n[server_fork.c]"] -->|"forks handler"| ForkHandler["Fork Client Handler\n[server_fork.c]"]
            ForkServ -->|"registers sockets"| SharedTable[("Shared Client Table\n[server_fork.c]")]
            ForkHandler -->|"reads and removes sockets"| SharedTable
        end

        subgraph SelectGroup["I/O Multiplexing"]
            SelectServ["Select Server\n[server_select.c]"]
        end

        subgraph ThreadGroup["Thread-per-Client"]
            ThreadServ["Thread Listener\n[server_thread.c]"] -->|"spawns per client"| ThreadWorker["Thread Worker\n[server_thread.c]"]
            ThreadWorker -->|"broadcasts messages"| ThreadBroadcast["Thread Broadcast\n[server_thread.c]"]
        end
    end

    Client -->|"connects to"| DefListener
    DefBroadcast -->|"sends messages"| Client
    Client -->|"connects to"| ForkServ
    ForkHandler -->|"broadcasts messages"| Client
    Client -->|"connects to"| SelectServ
    SelectServ -->|"broadcasts messages"| Client
    Client -->|"connects to"| ThreadServ
    ThreadBroadcast -->|"sends messages"| Client

    classDef userClass fill:#dbeafe,stroke:#3b82f6,stroke-width:2px;
    classDef clientClass fill:#bfdbfe,stroke:#2563eb,stroke-width:2px;
```

---

## Project Structure

```
simple_tcp_chat/
├── server_select.c   # Approach 1: Single-threaded event loop using select()
├── server_thread.c   # Approach 2: Multi-threaded server using thread-per-client
├── server_fork.c     # Approach 3: Multi-process server using fork() + shared memory
├── server.c          # Dual-console interactive chat server (default)
├── client.c          # Cross-platform TCP chat client
├── COMPARISON.md     # In-depth architectural comparison and benchmark report
├── Makefile          # Build configuration for Linux / Ubuntu / WSL
└── README.md         # Documentation & execution instructions
```

Default Port: **9090**

---

## How to Compile and Run

### On Windows (PowerShell)

#### 1. Compile the Client:
```powershell
gcc -Wall -Wextra -std=c11 client.c -o client.exe -lws2_32
```

#### 2. Run Approach 1 — `select()` Server:
```powershell
gcc -Wall -Wextra -std=c11 server_select.c -o server_select.exe -lws2_32
.\server_select.exe
```

#### 3. Run Approach 2 — Thread-per-Client Server:
```powershell
gcc -Wall -Wextra -std=c11 server_thread.c -o server_thread.exe -lws2_32
.\server_thread.exe
```

#### 4. Run Clients (Open multiple terminal windows):
```powershell
.\client.exe 127.0.0.1 9090
```

---

### On Linux / Ubuntu / WSL (bash)

You can build all targets at once using the Makefile:
```bash
make
```

Or compile individual targets manually:

#### Approach 1 — `select()` Server:
```bash
gcc -Wall -Wextra -std=c11 server_select.c -o server_select
./server_select
```

#### Approach 2 — Thread-per-Client Server:
```bash
gcc -Wall -Wextra -std=c11 server_thread.c -o server_thread -lpthread
./server_thread
```

#### Approach 3 — Fork-per-Client Server:
```bash
gcc -Wall -Wextra -std=c11 server_fork.c -o server_fork
./server_fork
```

#### Client:
```bash
gcc -Wall -Wextra -std=c11 client.c -o client -lpthread
./client 127.0.0.1 9090
```

---

## Architectural Comparison Summary

| Metric | `fork()` per Client | Thread per Client | `select()` / `poll()` |
| :--- | :--- | :--- | :--- |
| **Model** | Multi-Process | Multi-Threaded | Event-Driven (Single Thread) |
| **Memory Overhead** | High (process table + CoW) | Moderate (thread stack) | Lowest (zero extra threads) |
| **Context Switch Cost** | Heavy (TLB/cache flush) | Lightweight | None (single event loop) |
| **State Sharing / Broadcast** | Complex (Requires IPC) | Easy (Shared heap + Mutex) | Trivial (Same memory, lock-free) |
| **Fault Isolation** | **High** (crash is isolated) | **Low** (crashes whole server)| **Low** (crashes whole server) |
| **Platform Portability** | POSIX only (Linux/WSL) | Cross-platform | Cross-platform |

For full details, graphs, and the C10K problem analysis, read [**COMPARISON.md**](COMPARISON.md).
