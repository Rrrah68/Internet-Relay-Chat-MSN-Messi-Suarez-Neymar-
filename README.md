*This project has been created as part of the 42 curriculum by radahman, mmutsulk, mobullad.*

# ft_irc

## Description

This project consists of implementing a fully functional IRC server in C++98. The goal is to build a server that can accept multiple clients concurrently, handle IRC commands correctly, and manage channels, users, operators, and messaging over TCP.

The server uses non-blocking sockets and `poll()` to monitor several connections at the same time without blocking the whole application. It supports authentication, nickname registration, user registration, channel creation, messaging, operator commands, channel modes, and a built-in IRC bot.

The project is organized around several core components:

- `Server`: manages the listening socket, connected clients, channels, polling loop, and command dispatching.
- `Client`: stores user state, buffers, and registration data.
- `Channel`: stores channel members, operators, invitations, topics, keys, limits, and modes.
- `Parser`: parses raw IRC messages into structured commands and parameters.

### Implemented features

- TCP IPv4 server
- Multiple simultaneous client connections
- Non-blocking sockets
- I/O multiplexing with `poll()`
- Password authentication with `PASS`
- Nickname registration with `NICK`
- User registration with `USER`
- Channel management with `JOIN`, `PART`, and channel membership rules
- Private messaging with `PRIVMSG`
- Notice support with `NOTICE`
- Topic management with `TOPIC`
- Channel operator management with `MODE` and `KICK`
- Channel invitation system with `INVITE`
- Channel modes:
  - `+t` topic restriction
  - `+i` invite-only mode
  - `+k` channel key
  - `+o` operator privileges
  - `+l` user limit
- Error handling and numeric IRC replies
- Client disconnect handling
- Signal handling
- Input and output buffering
- Built-in IRC bot
- DCC file transfers through IRC clients such as Irssi

## Instructions

### Requirements

- A C++ compiler
- `make`
- A Unix-like environment (Linux)
- An IRC client or `nc` to test the server

The project must be compiled using the C++98 standard.

### Compilation

From the project root:

```bash
make
```

This will build:

- the server executable: `ircserv`
- the bot executable: `ircbot`

The compilation flags used are:

```bash
-Wall -Wextra -Werror -std=c++98
```

### Cleaning

```bash
make clean
```

Removes compiled object files.

```bash
make fclean
```

Removes object files and executables.

```bash
make re
```

Rebuilds the project from scratch.

### Running the server

```bash
./ircserv <port> <password>
```

Example:

```bash
./ircserv 6667 mypass
```

### Connecting with Irssi

Start Irssi, then connect with:

```text
/connect 127.0.0.1 6667 mypass Alice
```

Irssi sends the IRC registration commands (`PASS`, `NICK`, and `USER`)
automatically. The server also joins a registered client to `#general` by
default when that channel is not invite-only.

Essential Irssi commands:

```text
/join #general                    Join a channel
/part #general                    Leave a channel
/msg #general Hello everyone      Send a channel message
/msg Alice Hello                  Send a private message
/query Alice                      Open a private conversation
/nick NewNick                     Change nickname
/topic #general Welcome           Change the channel topic
/mode #general +i                 Enable invite-only mode
/mode #general -i                 Disable invite-only mode
/invite Alice #general            Invite a client
/kick #general Alice              Remove a client
/names #general                   List channel members
/who #general                     List channel users
/dcc send Alice file.txt          Send a file directly to Alice
/dcc get Alice                    Accept a DCC file transfer
/quit                             Disconnect from the server
```

If a `JOIN` is refused while a channel is invite-only, Irssi can still open a
local channel window. This does not mean the join succeeded: the client must
appear in the channel member list. After the operator disables `+i`, the
server admits pending join attempts automatically. If the Irssi window is
stale, use `/part #general` and then `/join #general`.

### Running the bot

```bash
./ircbot <ip> <port> <password>
```

Example:

```bash
./ircbot 127.0.0.1 6667 mypass
```

The bot authenticates to the server, joins the `#bot` channel, and can respond to commands such as:

```text
!help
!ping
!hello
!users
!channels
```

The bot must be started after the server:

```bash
./ircserv 6667 mypass
./ircbot 127.0.0.1 6667 mypass
```

### File transfer with Irssi

DCC file transfers are negotiated through IRC messages and then transferred
directly between the two IRC clients. The server forwards the DCC negotiation;
it does not carry the file data itself.

On the sender's Irssi client:

```text
/dcc send <nickname> <path/to/file>
```

On the receiving Irssi client, accept the transfer with:

```text
/dcc get <nickname>
```

The clients must be able to establish a direct TCP connection to each other.

## Usage examples

### Authentication

```text
PASS mypass
NICK Alice
USER alice 0 * :Alice
```

### Join a channel

```text
JOIN #general
```

### Send a message

```text
PRIVMSG #general :Hello everyone!
```

### Leave a channel

```text
PART #general
```

### Change the channel topic

```text
TOPIC #general :Welcome to the channel
```

### Grant operator privileges

```text
MODE #general +o Alice
```

### Set invite-only mode

```text
MODE #general +i
```

### Set a channel password

```text
MODE #general +k secret
```

### Set a user limit

```text
MODE #general +l 10
```

### Invite a user

```text
INVITE Alice #general
```

### Kick a user

```text
KICK #general Bob
```

### Test with Netcat

```bash
nc 127.0.0.1 6667
```

Then send:

```text
PASS mypass
NICK alice
USER alice 0 * :Alice
JOIN #general
PRIVMSG #general :Hello from nc!
```

## Project structure

```text
.
├── Makefile
├── README.md
├── inc/
│   ├── Channel.hpp
│   ├── Client.hpp
│   ├── Parser.hpp
│   └── Server.hpp
├── src/
│   ├── main.cpp
│   ├── Server.cpp
│   ├── Client.cpp
│   ├── Parser.cpp
│   └── Channel.cpp
├── bot/
│   └── main.cpp
└── .gitignore
```

## Technical choices

### Non-blocking sockets

Client sockets are configured as non-blocking so that a slow or stalled client cannot block the whole server.

### `poll()`

The server uses `poll()` to monitor the listening socket and all connected clients from a single event loop.

### Input and output buffering

TCP delivers data as a stream, not as complete IRC messages. Incoming bytes are buffered and parsed into complete commands as they arrive, while outgoing data is queued in client output buffers and sent when the socket becomes writable.

### Object-oriented design

Responsibilities are separated between the main classes:

- `Server`
- `Client`
- `Channel`
- `Parser`

This keeps network code, client state, channel logic, and protocol parsing distinct and easier to maintain.

## Resources

### IRC protocol and networking

- RFC 1459 — Internet Relay Chat Protocol
- RFC 2811 — Internet Relay Chat: Channel Management
- RFC 2812 — Internet Relay Chat: Client Protocol
- Linux manual pages for `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`, `poll()`, and `fcntl()`
- General documentation on TCP sockets and non-blocking I/O

### C++

- C++98 language documentation
- Standard library references
- STL container usage documentation

### AI usage

AI tools were used to support the development workflow in several areas:

- understanding socket programming and TCP behavior
- reasoning about non-blocking I/O and `poll()`
- discussing IRC protocol structure and command semantics
- identifying edge cases in channel management and client handling
- troubleshooting compilation issues and runtime bugs
- helping design test scenarios for multiple clients and fragmented input
- reviewing code organization and documenting project behavior

The final implementation, validation, and integration were always reviewed and adapted by the project author before being kept in the codebase.

## Notes

This project is a complete IRC server implementation written in C++98, with the required core features and a built-in bot. It is designed to handle multiple active clients concurrently while remaining robust in the face of typical IRC traffic and edge cases.
