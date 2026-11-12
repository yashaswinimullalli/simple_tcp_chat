# Simple TCP Chat (Client - Server)

A cross-platform C-based TCP chat application supporting Windows (Winsock) and Linux/WSL (POSIX sockets + pthreads).

## Features
- Two-way communication between server and client.
- Multi-client support with message broadcasting.
- Cross-platform support for both Windows and Linux/WSL.
- Multi-threaded console input and network handling.

Default port: **9090**

---

## How to Run on Windows (PowerShell)

### 1. Server (Terminal 1)
Compile and start the server:
```powershell
gcc -Wall -Wextra -std=c11 server.c -o server.exe -lws2_32
.\server.exe
```

### 2. Client (Terminal 2)
Compile and connect the client:
```powershell
gcc -Wall -Wextra -std=c11 client.c -o client.exe -lws2_32
.\client.exe 127.0.0.1 9090
```

---

## How to Run on Linux / Ubuntu / WSL (bash)

### 1. Server (Terminal 1)
Compile and start the server:
```bash
gcc -Wall -Wextra -std=c11 server.c -o server -lpthread
./server
```

### 2. Client (Terminal 2)
Compile and connect the client:
```bash
gcc -Wall -Wextra -std=c11 client.c -o client -lpthread
./client 127.0.0.1 9090
```

---

## How to Use
- Type messages in the client terminal and press **Enter** to send them to the server and other connected clients.
- Type messages directly in the server terminal and press **Enter** to broadcast to all clients.
- Press **Ctrl + C** to disconnect or stop.
