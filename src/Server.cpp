#include "Server.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <iostream>
#include <stdexcept>


volatile bool	Server::_shutdown = false;

// Lifecycle and socket setup.
Server::Server(int port, const std::string &password)
	: _port(port), _password(password), _listenFd(-1)
{
	setupSocket();
	struct sigaction sa;
	sa.sa_handler = Server::signalHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
}

Server::~Server()
{
	for (size_t i = 0; i < _pollFds.size(); ++i)
		close(_pollFds[i].fd);
}

void Server::signalHandler(int signum)
{
	(void)signum;
	_shutdown = true;
}

void Server::setupSocket()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd < 0)
		throw std::runtime_error("socket() failed");

	int opt = 1;
	if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		close(_listenFd);
		throw std::runtime_error("setsockopt() failed");
	}

	if(!setNonBlocking(_listenFd))
	{
		close(_listenFd);
		_listenFd = -1;
		throw std::runtime_error("fcntl(O_NONBLOCK) failed");
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(static_cast<uint16_t>(_port));

	if (bind(_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		close(_listenFd);
		throw std::runtime_error("bind() failed (port already in use?)");
	}

	if (listen(_listenFd, SOMAXCONN) < 0)
	{
		close(_listenFd);
		throw std::runtime_error("listen() failed");
	}

	struct pollfd pfd;
	pfd.fd = _listenFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);
	std::cout << "ircserv listening on port " << _port << std::endl;
}

bool Server::setNonBlocking(int fd)
{
	return (fcntl(fd, F_SETFL, O_NONBLOCK) != -1);
}

void Server::run()
{
	while (!_shutdown)
	{
		int ret = poll(&_pollFds[0], _pollFds.size(), -1);
		if (ret < 0)
		{
			if (_shutdown)
				break;
			throw std::runtime_error("poll() failed");
		}
		if (ret == 0)
			continue ;

		for (size_t i = 0; i < _pollFds.size();)
		{
			int fd = _pollFds[i].fd;
			short events = _pollFds[i].revents;

			if (events == 0)
			{
				++i;
				continue;
			}

			if (fd == _listenFd)
			{
				if (events & POLLIN)
					acceptNewClient();
				++i;
				continue;
			}

			if (events & (POLLERR | POLLNVAL))
			{
				disconnectClient(i);
				continue;
			}

			if (events & POLLIN)
			{
				handleClientRead(fd);
				if (_clients.find(fd) == _clients.end())
					continue;
			}

			if (events & POLLOUT)
			{
				flushClientWrite(fd);
				if (_clients.find(fd) == _clients.end())
					continue;
			}

			if (events & POLLHUP)
			{
				disconnectClient(i);
				continue;
			}
			++i;
		}
	}
	std::cout << "\nShutting down ircserv..." << std::endl;
}

void Server::addClient(int fd, const struct sockaddr_in &clientAddr)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);
	_clients.insert(std::make_pair(fd, Client(fd)));
	std::cout << "New connection: fd " << fd << " from "
		<< inet_ntoa(clientAddr.sin_addr) << std::endl;
}

void Server::acceptNewClient()
{
	struct sockaddr_in clientAddr;
	socklen_t len;
	int fd;

	len = sizeof(clientAddr);
	fd = accept(_listenFd,
		reinterpret_cast<struct sockaddr *>(&clientAddr), &len);
	if (fd < 0)
		return ;
	if (!setNonBlocking(fd))
	{
		close(fd);
		return ;
	}
	addClient(fd, clientAddr);
}

void Server::handleClientRead(int fd)
{
	char buffer[4096];
	ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
	std::map<int, Client>::iterator it = _clients.find(fd);

	if (it == _clients.end())
		return ;

	if (n <= 0)
	{
		for (size_t i = 0; i < _pollFds.size(); ++i)
		{
			if (_pollFds[i].fd == fd)
			{
				disconnectClient(i);
				return ;
			}
		}
		return ;
	}

	it->second.appendToInBuffer(
		std::string(buffer, static_cast<size_t>(n)));
	extractCommands(fd);
}

void Server::flushClientWrite(int fd)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	std::string &out = it->second.getOutBuffer();

	if (out.empty())
		return ;

	size_t sendLength = out.size();
	if (sendLength > 4096)
		sendLength = 4096;

	ssize_t n = send(fd, out.c_str(), sendLength, 0);
	if (n < 0)
	{
		for (size_t i = 0; i < _pollFds.size(); ++i)
		{
			if (_pollFds[i].fd == fd)
			{
				disconnectClient(i);
				return ;
			}
		}
		return ;
	}

	if (n == 0)
		return ;

	out.erase(0, static_cast<size_t>(n));

	if (out.empty())
	{
		for (size_t i = 0; i < _pollFds.size(); ++i)
		{
			if (_pollFds[i].fd == fd)
			{
				_pollFds[i].events = POLLIN;
				break ;
			}
		}
	}
}

void Server::disconnectClient(size_t pollIndex)
{
	int fd = _pollFds[pollIndex].fd;
	std::map<std::string, Channel>::iterator channel;
	std::map<std::string, std::set<int> >::iterator pending;

	std::cout << "Client disconnected: fd " << fd << std::endl;

	channel = _channels.begin();
	while (channel != _channels.end())
	{
		bool wasMember = channel->second.hasClient(fd);

		channel->second.removeClient(fd);
		channel->second.removeInvite(fd);
		if (channel->second.getClients().empty())
			_channels.erase(channel++);
		else
		{
			if (wasMember)
				promoteSuccessor(channel->second);
			++channel;
		}
	}

	close(fd);
	_clients.erase(fd);

	pending = _pendingJoins.begin();
	while (pending != _pendingJoins.end())
	{
		pending->second.erase(fd);
		if (pending->second.empty())
			_pendingJoins.erase(pending++);
		else
			++pending;
	}

	_pollFds[pollIndex] = _pollFds.back();
	_pollFds.pop_back();
}

// Command parsing and dispatch.
void Server::extractCommands(int fd)
{
	std::string &buf = _clients[fd].getInBuffer();
	size_t pos;

	while ((pos = buf.find('\n')) != std::string::npos)
	{
		std::string line = buf.substr(0, pos);

		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		buf.erase(0, pos + 1);

		if (!line.empty())
			processCommand(fd, line);
		// QUIT erases the client: buf would then point to freed memory.
		if (_clients.find(fd) == _clients.end())
			return ;
	}
}

void Server::processCommand(int fd, const std::string &line)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	IRCCommand command = _parser.parse(line);
	std::cout << "[fd " << fd << "] command " << command.command;
	if (command.command != "PASS" && command.command != "CAP")
		std::cout << " from " << (it->second.getNickname().empty()
			? "*" : it->second.getNickname());
	std::cout << std::endl;

	if (command.command == "PASS")
	{
		handlePass(fd, command);
		return ;
	}

	if (command.command == "CAP")
	{
		handleCap(fd, command);
		return ;
	}

	if (command.command == "NICK")
	{
		handleNick(fd, command);
		return ;
	}
	if (command.command == "USER")
	{
		handleUser(fd, command);
		return ;
	}
	if (command.command == "JOIN")
	{
		handleJoin(fd, command);
		return ;
	}
	if (command.command == "PART")
	{
		handlePart(fd, command);
		return ;
	}
	if (command.command == "PRIVMSG")
	{
		handlePrivmsg(fd, command);
		return ;
	}
	if (command.command == "NOTICE")
	{
		handlePrivmsg(fd, command, true);
		return ;
	}
	if (command.command == "PING")
	{
		handlePing(fd, command);
		return ;
	}
	if (command.command == "QUIT")
	{
		handleQuit(fd, command);
		return ;
	}
	if (command.command == "TOPIC")
	{
		handleTopic(fd, command);
		return ;
	}
	if (command.command == "MODE")
	{
		handleMode(fd, command);
		return ;
	}
	if (command.command == "INVITE")
	{
		handleInvite(fd, command);
		return ;
	}
	if (command.command == "KICK")
	{
		handleKick(fd, command);
		return ;
	}
	if (command.command == "BOTUSERS")
	{
		handleBotUsers(fd, command);
		return ;
	}
	if (command.command == "BOTCHANNELS")
	{
		handleBotChannels(fd, command);
		return ;
	}
	if (command.command == "WHO")
	{
		handleWho(fd, command);
		return ;
	}
	if (command.command == "WHOIS")
	{
		handleWhois(fd, command);
		return ;
	}
	if (command.command == "WHOWAS")
	{
		handleWhowas(fd, command);
		return ;
	}
	std::string reply = ":ircserv 421 * "
		+ command.command + " :Unknown command\r\n";
	it->second.appendToOutBuffer(reply);
	enableWrite(fd);
}

void Server::enableWrite(int fd)
{
	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (_pollFds[i].fd == fd)
		{
			_pollFds[i].events |= POLLOUT;
			return ;
		}
	}
}

void Server::sendReply(int fd, const std::string &message)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;
	client->second.appendToOutBuffer(message);
	enableWrite(fd);
}

void Server::broadcastToChannel(const Channel &channel, int senderFd,
	const std::string &message)
{
	const std::vector<int> &clients = channel.getClients();

	for (size_t i = 0; i < clients.size(); ++i)
	{
		if (clients[i] == senderFd)
			continue;

		std::map<int, Client>::iterator it = _clients.find(clients[i]);
		if (it == _clients.end())
			continue;

		it->second.appendToOutBuffer(message);
		enableWrite(clients[i]);
	}

	for (std::map<int, Client>::iterator client = _clients.begin();
		client != _clients.end(); ++client)
	{
		if (client->first != senderFd
			&& client->second.getNickname() == "ircbot"
			&& !channel.hasClient(client->first))
		{
			client->second.appendToOutBuffer(message);
			enableWrite(client->first);
		}
	}
}

std::string Server::getPassword() const
{
	return (_password);
}
