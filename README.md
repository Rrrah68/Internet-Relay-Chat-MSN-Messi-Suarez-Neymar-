*This project has been created as part of the 42 curriculum by <login1>, <login2>, <login3>.*

# ft_irc

## Description

'ft_irc' is a C++98 project from the 42 curriculum whose goal is to create a functional IRC (Internet Relay Chat) server.

The server communicates with IRC clients over TCP and is designed to handle multiple clients simultaneously without blocking. It implements the main IRC features required by the project, including client authentication, nickname and user registration, channel management, private and channel messaging, operator privileges, channel modes and error handling.

The project is organized around four main components:

- **Server**: manages the network sockets, connected clients, channels and command dispatching.
- **Client**: stores the state of each connected user, including its socket, nickname, username and communication buffers.
- **Parser**: converts raw IRC messages into structured commands and parameters.
- **Channel**: manages channel members, operators, invitations, topics and channel modes.

The server uses non-blocking sockets and `poll()` to monitor the listening socket and connected clients. Input and output buffers are used to correctly handle TCP fragmentation, multiple commands received together and pending outgoing messages.

### Implemented features

- TCP IPv4 server
- Multiple simultaneous client connections
- Non-blocking sockets
- I/O multiplexing with `poll()`
- Client authentication with `PASS`
- Nickname management with `NICK`
- User registration with `USER`
- Channel creation and management
- `JOIN` and `PART`
- `PRIVMSG`
- `KICK`
- `INVITE`
- `TOPIC`
- `MODE`
- Channel operator management
- Channel modes:
  - `+t` topic restriction
  - `+i` invite-only channels
  - `+k` channel password
  - `+o` operator privileges
  - `+l` user limit
- IRC numeric replies and error handling
- Client disconnection handling
- Signal handling
- Input and output buffering

### Bonus

The project also includes an IRC bot and additional server-side functionality.

The bot can respond to commands such as:

```text
!users
!channels
```

Custom server commands are used by the bot to retrieve information:

```text
BOTUSERS
BOTCHANNELS
```

A custom file transfer mechanism was also implemented using:

```text
FTSEND
FTDATA
FTEND
```

---

## Instructions

### Requirements

The project requires:

- A C++ compiler
- `make`
- A Unix-like environment
- An IRC client for testing

The project must be compiled using the C++98 standard.

### Compilation

Clone the repository and enter the project directory:

```bash
git clone <repository_url>
cd ft_irc
```

Compile the project:

```bash
make
```

The compilation uses:

```text
-Wall -Wextra -Werror -std=c++98
```

The server executable is:

```text
./ircserv
```

### Cleaning

Remove object files:

```bash
make clean
```

Remove object files and the executable:

```bash
make fclean
```

Rebuild the project:

```bash
make re
```

### Running the server

The server takes two arguments:

```bash
./ircserv <port> <password>
```

Example:

```bash
./ircserv 6667 mypass
```

### Connecting with Netcat

A simple way to test the server is with `nc`:

```bash
nc 127.0.0.1 6667
```

Then authenticate and register:

```text
PASS mypass
NICK rayan
USER rayan 0 * :Rayan
```

After registration, a client can join a channel:

```text
JOIN #general
```

and send a message:

```text
PRIVMSG #general :Hello everyone!
```

Multiple clients can connect to the same server and communicate through the same channel.

### Connecting with an IRC client

The server can also be tested with a standard IRC client.

Configure the client with:

```text
Server: 127.0.0.1
Port: 6667
Password: mypass
```

Once connected, users can register, join channels and communicate using the supported IRC commands.

---

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

### Change the topic

```text
TOPIC #general :Welcome to the channel
```

### Give operator privileges

```text
MODE #general +o Alice
```

### Make a channel invite-only

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

### Kick a user

```text
KICK #general Bob
```

### Invite a user

```text
INVITE Bob #general
```

---

## Technical choices

### TCP

TCP is used for communication between the server and IRC clients because it provides a reliable, connection-oriented byte stream.

### Non-blocking sockets

Sockets are configured as non-blocking so that an operation involving one client cannot block the entire server.

### `poll()`

`poll()` is used for I/O multiplexing. It allows the server to monitor the listening socket and all connected clients from a single event loop.

The general network flow is:

```text
socket()
    |
bind()
    |
listen()
    |
poll()
    |
+---+------------------+
|                      |
v                      v
accept()              recv()
                         |
                         v
                       Parser
                         |
                         v
                  Command handling
                         |
                         v
                    out buffer
                         |
                         v
                      poll()
                         |
                         v
                       send()
```

### Input buffering

TCP is a stream of bytes, so one `recv()` call does not necessarily contain one complete IRC command.

For example:

```text
recv #1:
PRIVMSG #gen

recv #2:
eral :Hello\r\n
```

The server stores the received data in the client's input buffer and extracts complete IRC commands when the appropriate line ending is received.

The same mechanism handles several commands received in a single network read.

### Output buffering

Outgoing messages are stored in the destination client's output buffer and sent when the socket is ready for writing.

This prevents a slow client from blocking the server.

### Object-oriented design

Responsibilities are separated between:

```text
Server
Client
Channel
Parser
```

This keeps networking, client state, protocol parsing and channel logic separated.

---

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
└── bot/
    └── main.cpp
```

---

## Testing

The server was tested using manual and automated tests.

### Manual tests

The project was tested with:

- `nc`
- IRC clients
- Multiple simultaneous connections

The following features were tested:

- Authentication
- Nickname and username registration
- Channel creation
- JOIN / PART
- PRIVMSG
- KICK
- INVITE
- TOPIC
- MODE
- Operator permissions
- Client disconnections
- Multiple clients communicating simultaneously

### Edge cases

Tests also cover:

- Fragmented IRC commands
- Multiple commands received in a single network read
- Invalid commands
- Incorrect command parameters
- Unauthorized operator commands
- Client disconnections
- Multiple simultaneous clients
- Channel lifecycle

### Memory testing

Valgrind was used during development to check for:

- Memory leaks
- Invalid memory accesses
- Use-after-free
- Uninitialized memory

---

## Resources

### IRC protocol

- RFC 1459 — Internet Relay Chat Protocol
- RFC 2811 — Internet Relay Chat: Channel Management
- RFC 2812 — Internet Relay Chat: Client Protocol

These references were used to understand IRC message formatting, commands, channel management and numeric replies.

### Network programming

- Linux `socket()` documentation
- Linux `bind()` documentation
- Linux `listen()` documentation
- Linux `accept()` documentation
- Linux `recv()` documentation
- Linux `send()` documentation
- Linux `poll()` documentation
- Linux `fcntl()` documentation

These resources were used to understand TCP communication, non-blocking sockets and I/O multiplexing.

### C++

- C++98 documentation
- C++ reference documentation
- Standard library documentation

These resources were used for C++98 language features, STL containers, strings and memory management.

### Testing and debugging

- Valgrind documentation
- Netcat (`nc`) documentation
- Linux networking tools

These resources were used to test the server and diagnose runtime and memory issues.

---

## AI usage

AI tools were used as development assistance during the project.

AI was used for:

- Understanding TCP socket programming
- Understanding `poll()` and non-blocking I/O
- Understanding IRC protocol concepts
- Discussing the project architecture
- Identifying potential bugs and edge cases
- Analyzing compiler and runtime errors
- Suggesting test cases
- Understanding Valgrind reports
- Reviewing code organization
- Developing and debugging parts of the IRC bot
- Reasoning about the custom file transfer functionality
- Preparing explanations for the project defense

AI was also used to help design test scenarios involving:

- Fragmented TCP input
- Multiple commands in one network read
- Multiple simultaneous clients
- Channel permissions
- Client disconnections
- Invalid IRC commands
- Memory management

AI suggestions were reviewed, adapted and tested by the team before being integrated into the project.

The team remained responsible for the implementation, integration, testing and final validation of the project.
