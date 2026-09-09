#include "Server.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <set>


volatile bool	Server::_shutdown = false;

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
		dispatchDelayedReplies();
		int ret = poll(&_pollFds[0], _pollFds.size(), getPollTimeout());
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

	if (it->second.isReceivingFile())
	{
		size_t received = static_cast<size_t>(n);
		size_t remaining = it->second.getFileBytesRemaining();
		std::vector<FileTransfer>::iterator transfer;

		if (received > remaining)
			received = remaining;

		it->second.appendToFileBuffer(
			std::string(buffer, received));
		it->second.setFileBytesRemaining(
			remaining - received);

		transfer = _fileTransfers.begin();
		while (transfer != _fileTransfers.end())
		{
			if (transfer->senderFd == fd)
				break ;
			++transfer;
		}

		if (transfer != _fileTransfers.end())
		{
			_clients[transfer->receiverFd].appendToOutBuffer(
				it->second.getFileBuffer());
			enableWrite(transfer->receiverFd);
			it->second.getFileBuffer().clear();
			transfer->bytesTransferred += received;
		}

		if (remaining - received == 0)
			it->second.setReceivingFile(false);

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

long long Server::getCurrentTimeMs() const
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return static_cast<long long>(now.tv_sec) * 1000
		+ static_cast<long long>(now.tv_usec) / 1000;
}

int Server::getPollTimeout() const
{
	if (_delayedReplies.empty())
		return -1;

	long long now = getCurrentTimeMs();
	long long earliest = _delayedReplies[0].dueAtMs;
	for (size_t i = 1; i < _delayedReplies.size(); ++i)
	{
		if (_delayedReplies[i].dueAtMs < earliest)
			earliest = _delayedReplies[i].dueAtMs;
	}
	if (earliest <= now)
		return 0;
	if (earliest - now > 60000)
		return 60000;
	return static_cast<int>(earliest - now);
}

void Server::dispatchDelayedReplies()
{
	long long now = getCurrentTimeMs();
	size_t i = 0;

	while (i < _delayedReplies.size())
	{
		if (_delayedReplies[i].dueAtMs > now)
		{
			++i;
			continue ;
		}

		std::map<int, Client>::iterator client =
			_clients.find(_delayedReplies[i].clientFd);
		if (client != _clients.end())
		{
			client->second.appendToOutBuffer(_delayedReplies[i].message);
			enableWrite(client->first);
		}
		_delayedReplies.erase(_delayedReplies.begin() + i);
	}
}

void Server::queueDelayedReply(int fd, const std::string &message,
	long long delayMs)
{
	DelayedReply reply;

	reply.clientFd = fd;
	reply.dueAtMs = getCurrentTimeMs() + delayMs;
	reply.message = message;
	_delayedReplies.push_back(reply);
}

void Server::extractCommands(int fd)
{
	std::map<int, Client>::iterator it;
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

		it = _clients.find(fd);
		if (it == _clients.end())
			return ;

		if (it->second.isReceivingFile())
		{
			if (!buf.empty())
			{
				size_t remaining;
				size_t dataSize;
				std::vector<FileTransfer>::iterator transfer;

				remaining = it->second.getFileBytesRemaining();
				dataSize = buf.size();

				if (dataSize > remaining)
					dataSize = remaining;

				transfer = _fileTransfers.begin();
				while (transfer != _fileTransfers.end())
				{
					if (transfer->senderFd == fd)
						break ;
					++transfer;
				}

				if (transfer != _fileTransfers.end())
				{
					_clients[transfer->receiverFd].appendToOutBuffer(
						buf.substr(0, dataSize));
					enableWrite(transfer->receiverFd);
					transfer->bytesTransferred += dataSize;
				}

				buf.erase(0, dataSize);
				it->second.setFileBytesRemaining(
					remaining - dataSize);

				if (remaining - dataSize == 0)
					it->second.setReceivingFile(false);
			}
			return ;
		}
	}
}

void Server::processCommand(int fd, const std::string &line)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	IRCCommand command = _parser.parse(line);

	if (command.command == "PASS")
	{
		handlePass(fd, command);
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

	if (command.command == "FTSEND")
	{
		handleFileSend(fd, command);
		return ;
	}

	if (command.command == "FTDATA")
	{
		handleFileData(fd, command);
		return ;
	}

	if (command.command == "FTEND")
	{
		handleFileEnd(fd, command);
		return ;
	}
	std::string reply = ":ircserv 421 * "
		+ command.command + " :Unknown command\r\n";

	it->second.appendToOutBuffer(reply);
	enableWrite(fd);
}

void Server::handleBotChannels(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	std::map<std::string, Channel>::iterator channel;
	std::string channels;

	if (it == _clients.end())
		return ;

	(void)command;
	for (channel = _channels.begin(); channel != _channels.end(); ++channel)
	{
		if (!channels.empty())
			channels += ", ";
		channels += channel->first;
	}

	it->second.appendToOutBuffer(
		":ircserv 901 " + it->second.getNickname()
		+ " :Channels: " + channels + "\r\n");
	enableWrite(fd);
}

void Server::handlePass(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	if (command.params.size() != 1)
	{
		it->second.appendToOutBuffer(
			":ircserv 461 * PASS :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	if (it->second.hasSentPass())
	{
		it->second.appendToOutBuffer(
			":ircserv 462 * :You may not reregister\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params[0] == getPassword())
	{
		it->second.setAuthenticated(true);
		it->second.setSentPass(true);
	}
	else
	{
		it->second.appendToOutBuffer(
			":ircserv 464 * :Password incorrect\r\n");
	}

	if (!it->second.getOutBuffer().empty())
		enableWrite(fd);
}

bool Server::isValidNickname(const std::string &nickname) const
{
	if (nickname.empty())
		return false;

	if (!std::isalpha(nickname[0])
		&& nickname[0] != '['
		&& nickname[0] != ']'
		&& nickname[0] != '\\'
		&& nickname[0] != '^'
		&& nickname[0] != '_'
		&& nickname[0] != '`'
		&& nickname[0] != '{'
		&& nickname[0] != '|'
		&& nickname[0] != '}')
		return false;

	for (size_t i = 1; i < nickname.size(); ++i)
	{
		if (!std::isalnum(nickname[i])
			&& nickname[i] != '['
			&& nickname[i] != ']'
			&& nickname[i] != '\\'
			&& nickname[i] != '^'
			&& nickname[i] != '_'
			&& nickname[i] != '`'
			&& nickname[i] != '{'
			&& nickname[i] != '|'
			&& nickname[i] != '}'
			&& nickname[i] != '-')
			return false;
	}

	return true;
}

void Server::handleNick(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	if (command.params.size() != 1)
	{
		it->second.appendToOutBuffer(
			":ircserv 431 * :No nickname given\r\n");
		enableWrite(fd);
		return ;
	}

	std::string nick = command.params[0];

	if (!isValidNickname(nick))
	{
		it->second.appendToOutBuffer(
			":ircserv 432 " + nick
			+ " :Erroneous nickname\r\n");
		enableWrite(fd);
		return ;
	}

	for (std::map<int, Client>::iterator client = _clients.begin();
		client != _clients.end(); ++client)
	{
		if (client->first != fd
			&& client->second.getNickname() == nick)
		{
			it->second.appendToOutBuffer(
				":ircserv 433 * " + nick
				+ " :Nickname is already in use\r\n");
			enableWrite(fd);
			return ;
		}
	}

	std::string oldNick = it->second.getNickname();
	if (oldNick == nick)
		return ;

	it->second.setNickname(nick);

	if (!oldNick.empty())
	{
		std::string message = ":" + oldNick
			+ "!user@localhost NICK :" + nick + "\r\n";
		std::set<int> recipients;
		recipients.insert(fd);

		for (std::map<std::string, Channel>::iterator channel =
			_channels.begin(); channel != _channels.end(); ++channel)
		{
			if (channel->second.hasClient(fd))
			{
				const std::vector<int> &members = channel->second.getClients();
				for (size_t i = 0; i < members.size(); ++i)
					recipients.insert(members[i]);
			}
		}

		for (std::set<int>::iterator recipient = recipients.begin();
			recipient != recipients.end(); ++recipient)
		{
			std::map<int, Client>::iterator client = _clients.find(*recipient);
			if (client == _clients.end())
				continue ;
			client->second.appendToOutBuffer(message);
			enableWrite(*recipient);
		}
	}
	else if (it->second.isRegistered())
	{
		it->second.appendToOutBuffer(
			":ircserv 001 " + nick + " :Welcome to the IRC network\r\n");
		enableWrite(fd);
	}
}

void Server::handleUser(int fd, const IRCCommand &command)

{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	if (command.params.size() != 4)
	{
		it->second.appendToOutBuffer(
			":ircserv 461 * USER :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	if (!it->second.isAuthenticated())
	{
		it->second.appendToOutBuffer(
			":ircserv 464 * :Password incorrect\r\n");
		enableWrite(fd);
		return ;
	}

	if (!it->second.getUsername().empty())
	{
		it->second.appendToOutBuffer(
			":ircserv 462 * :You may not reregister\r\n");
		enableWrite(fd);
		return ;
	}

	it->second.setUsername(command.params[0]);
	if (it->second.getNickname().empty())
	{
		it->second.appendToOutBuffer(
			":ircserv 431 * :No nickname given\r\n");
		enableWrite(fd);
		return ;
	}

	it->second.appendToOutBuffer(
		":ircserv 001 " + it->second.getNickname()
		+ " :Welcome to the IRC network\r\n");
	enableWrite(fd);

	if (it->second.isRegistered())
		std::cout << "[fd " << fd << "] CLIENT REGISTERED"
			<< std::endl;

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

void Server::handleKick(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;

	if (!client->second.isRegistered())
	{
		client->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.size() < 2)
	{
		client->second.appendToOutBuffer(
			":ircserv 461 * KICK :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channelName = command.params[0];
	std::string nickname = command.params[1];

	std::map<std::string, Channel>::iterator channel;
	channel = _channels.find(channelName);

	if (channel == _channels.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 403 " + channelName
			+ " :No such channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.hasClient(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 442 " + channelName
			+ " :You're not on that channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.isOperator(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 482 " + channelName
			+ " :You're not channel operator\r\n");
		enableWrite(fd);
		return ;
	}

	std::map<int, Client>::iterator target = _clients.begin();

	while (target != _clients.end())
	{
		if (target->second.getNickname() == nickname)
			break ;
		++target;
	}

	if (target == _clients.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 401 " + nickname
			+ " :No such nick\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.hasClient(target->first))
	{
		client->second.appendToOutBuffer(
			":ircserv 441 " + nickname + " " + channelName
			+ " :They aren't on that channel\r\n");
		enableWrite(fd);
		return ;
	}

	std::string message = ":" + client->second.getNickname()
		+ "!user@localhost KICK " + channelName
		+ " " + nickname;

	if (command.params.size() >= 3)
		message += " :" + command.params[2];

	message += "\r\n";

	broadcastToChannel(channel->second, fd, message);
	client->second.appendToOutBuffer(message);
	enableWrite(fd);

	channel->second.removeClient(target->first);
	channel->second.removeOperator(target->first);
	channel->second.removeInvite(target->first);

	if (channel->second.getClients().empty())
		_channels.erase(channel);
}

Channel &Server::getOrCreateChannel(const std::string &name)
{
	std::map<std::string, Channel>::iterator it = _channels.find(name);

	if (it != _channels.end())
		return (it->second);

	_channels.insert(std::make_pair(name, Channel(name)));
	return (_channels.find(name)->second);
}

void Server::handleJoin(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	if (!it->second.isRegistered())
	{
		it->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.empty())
	{
		it->second.appendToOutBuffer(
			":ircserv 461 * JOIN :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channels = command.params[0];
	size_t start = 0;
	size_t pos;

	while (start < channels.size())
	{
		pos = channels.find(',', start);

		std::string channelName;
		if (pos == std::string::npos)
			channelName = channels.substr(start);
		else
			channelName = channels.substr(start, pos - start);

		if (!channelName.empty() && channelName[0] == '#')
		{
			Channel &channel = getOrCreateChannel(channelName);

			if (!channel.hasClient(fd))
			{
				if (channel.getInviteOnly() && !channel.isInvited(fd))
				{
					it->second.appendToOutBuffer(
						":ircserv 473 " + channelName
						+ " :Cannot join channel (+i)\r\n");
					enableWrite(fd);
				}
				else if (channel.getKeyEnabled()
					&& (command.params.size() < 2
					|| command.params[1] != channel.getKey()))
				{
					it->second.appendToOutBuffer(
						":ircserv 475 " + channelName
						+ " :Cannot join channel (+k)\r\n");
					enableWrite(fd);
				}
				else if (channel.getLimitEnabled()
					&& static_cast<int>(channel.getClients().size())
					>= channel.getLimit())
				{
					it->second.appendToOutBuffer(
						":ircserv 471 " + channelName
						+ " :Cannot join channel (+l)\r\n");
					enableWrite(fd);
				}
				else
				{
					bool wasEmpty = channel.getClients().empty();

					channel.addClient(fd);
					channel.removeInvite(fd);

					if (wasEmpty)
						channel.addOperator(fd);

					std::string message = ":"
						+ it->second.getNickname()
						+ "!user@localhost JOIN "
						+ channelName + "\r\n";

					broadcastToChannel(channel, fd, message);
					it->second.appendToOutBuffer(message);

					std::string names;
					const std::vector<int> &members = channel.getClients();
					for (size_t i = 0; i < members.size(); ++i)
					{
						std::map<int, Client>::iterator member =
							_clients.find(members[i]);
						if (member == _clients.end())
							continue ;
						if (!names.empty())
							names += " ";
						if (channel.isOperator(members[i]))
							names += "@";
						names += member->second.getNickname();
					}

					queueDelayedReply(fd,
						":ircserv 353 " + it->second.getNickname()
						+ " = " + channelName + " :" + names + "\r\n", 25);
					queueDelayedReply(fd,
						":ircserv 366 " + it->second.getNickname()
						+ " " + channelName
						+ " :End of /NAMES list.\r\n", 50);
					enableWrite(fd);
				}
			}
		}
		else if (!channelName.empty())
		{
			it->second.appendToOutBuffer(
				":ircserv 403 " + channelName
				+ " :No such channel\r\n");
			enableWrite(fd);
		}

		if (pos == std::string::npos)
			break ;
		start = pos + 1;
	}
}

void Server::handlePart(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	if (!it->second.isRegistered())
	{
		it->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.empty())
	{
		it->second.appendToOutBuffer(
			":ircserv 461 * PART :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channels = command.params[0];
	size_t start = 0;
	size_t pos;

	while (start < channels.size())
	{
		pos = channels.find(',', start);

		std::string channelName;
		if (pos == std::string::npos)
			channelName = channels.substr(start);
		else
			channelName = channels.substr(start, pos - start);

		std::map<std::string, Channel>::iterator channelIt;
		channelIt = _channels.find(channelName);

		if (channelIt == _channels.end())
		{
			it->second.appendToOutBuffer(
				":ircserv 403 " + channelName
				+ " :No such channel\r\n");
			enableWrite(fd);
		}
		else
		{
			Channel &channel = channelIt->second;

			if (!channel.hasClient(fd))
			{
				it->second.appendToOutBuffer(
					":ircserv 442 " + channelName
					+ " :You're not on that channel\r\n");
				enableWrite(fd);
			}
			else
			{
				std::string message = ":"
					+ it->second.getNickname()
					+ "!user@localhost PART "
					+ channelName;
				if (command.params.size() > 1)
					message += " :" + command.params[1];
				message += "\r\n";

				channel.removeClient(fd);
				channel.removeOperator(fd);
				channel.removeInvite(fd);

				broadcastToChannel(channel, fd, message);
				it->second.appendToOutBuffer(message);
				enableWrite(fd);

				if (channel.getClients().empty())
					_channels.erase(channelIt);
			}
		}

		if (pos == std::string::npos)
			break ;
		start = pos + 1;
	}
}

void Server::handlePrivmsg(int fd, const IRCCommand &command, bool isNotice)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	std::string commandName = isNotice ? "NOTICE" : "PRIVMSG";
	if (it == _clients.end())
		return ;

	if (!it->second.isRegistered())
	{
		if (!isNotice)
		{
			it->second.appendToOutBuffer(
				":ircserv 451 * :You have not registered\r\n");
			enableWrite(fd);
		}
		return ;
	}

	if (command.params.size() < 2)
	{
		if (!isNotice)
		{
			it->second.appendToOutBuffer(
				":ircserv 461 * PRIVMSG :Not enough parameters\r\n");
			enableWrite(fd);
		}
		return ;
	}

	std::string target = command.params[0];
	if (target[0] != '#')
	{
		for (std::map<int, Client>::iterator client = _clients.begin();
			client != _clients.end(); ++client)
		{
			if (client->second.getNickname() == target)
			{
				std::string message = ":" + it->second.getNickname()
					+ "!user@localhost " + commandName + " " + target + " :"
					+ command.params[1] + "\r\n";

				client->second.appendToOutBuffer(message);
				enableWrite(client->first);
				return ;
			}
		}

		if (!isNotice)
		{
			it->second.appendToOutBuffer(
				":ircserv 401 " + target + " :No such nick/channel\r\n");
			enableWrite(fd);
		}
		return ;
	}

	std::map<std::string, Channel>::iterator channelIt;
	channelIt = _channels.find(target);
	if (channelIt == _channels.end())
	{
		if (!isNotice)
		{
			it->second.appendToOutBuffer(
				":ircserv 403 " + target + " :No such channel\r\n");
			enableWrite(fd);
		}
		return ;
	}

	if (!channelIt->second.hasClient(fd))
	{
		if (!isNotice)
		{
			it->second.appendToOutBuffer(
				":ircserv 442 " + target
				+ " :You're not on that channel\r\n");
			enableWrite(fd);
		}
		return ;
	}

	std::string message = ":" + it->second.getNickname()
		+ "!user@localhost " + commandName + " " + target + " :"
		+ command.params[1] + "\r\n";

	broadcastToChannel(channelIt->second, fd, message);
}

void Server::handlePing(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;

	if (command.params.empty())
	{
		client->second.appendToOutBuffer(
			":ircserv 461 * PING :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	client->second.appendToOutBuffer(
		":ircserv PONG ircserv :" + command.params[0] + "\r\n");
	enableWrite(fd);
}

void Server::handleQuit(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	std::string reason = "Quit";
	if (!command.params.empty())
		reason = command.params[0];

	std::string message = ":" + it->second.getNickname()
		+ "!user@localhost QUIT :" + reason + "\r\n";

	std::map<std::string, Channel>::iterator channel = _channels.begin();
	while (channel != _channels.end())
	{
		if (channel->second.hasClient(fd))
		{
			broadcastToChannel(channel->second, fd, message);
			channel->second.removeClient(fd);
			channel->second.removeOperator(fd);
			channel->second.removeInvite(fd);
			if (channel->second.getClients().empty())
				_channels.erase(channel++);
			else
				++channel;
		}
		else
			++channel;
	}

	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (_pollFds[i].fd == fd)
		{
			disconnectClient(i);
			return ;
		}
	}
}

void Server::handleTopic(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;

	if (!client->second.isRegistered())
	{
		client->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.empty())
	{
		client->second.appendToOutBuffer(
			":ircserv 461 * TOPIC :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channelName = command.params[0];
	std::map<std::string, Channel>::iterator channel;
	channel = _channels.find(channelName);

	if (channel == _channels.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 403 " + channelName + " :No such channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.hasClient(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 442 " + channelName
			+ " :You're not on that channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.size() == 1)
	{
		if (channel->second.getTopic().empty())
			client->second.appendToOutBuffer(
				":ircserv 331 " + channelName + " :No topic is set\r\n");
		else
			client->second.appendToOutBuffer(
				":ircserv 332 " + channelName + " :"
				+ channel->second.getTopic() + "\r\n");
		enableWrite(fd);
		return ;
	}
	
	if (channel->second.getTopicRestricted()
		&& !channel->second.isOperator(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 482 " + channelName
			+ " :You're not channel operator\r\n");
		enableWrite(fd);
		return ;
	}

	channel->second.setTopic(command.params[1]);

	std::string message = ":" + client->second.getNickname()
		+ "!user@localhost TOPIC " + channelName + " :"
		+ command.params[1] + "\r\n";

	broadcastToChannel(channel->second, fd, message);
	client->second.appendToOutBuffer(message);
	enableWrite(fd);
}

void Server::handleMode(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;

	if (!client->second.isRegistered())
	{
		client->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.size() < 2)
	{
		client->second.appendToOutBuffer(
			":ircserv 461 * MODE :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channelName = command.params[0];
	std::string mode = command.params[1];

	std::map<std::string, Channel>::iterator channel;
	channel = _channels.find(channelName);

	if (channel == _channels.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 403 " + channelName
			+ " :No such channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.hasClient(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 442 " + channelName
			+ " :You're not on that channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.isOperator(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 482 " + channelName
			+ " :You're not channel operator\r\n");
		enableWrite(fd);
		return ;
	}

	if (mode == "+t")
		channel->second.setTopicRestricted(true);
	else if (mode == "-t")
		channel->second.setTopicRestricted(false);
	else if (mode == "+i")
		channel->second.setInviteOnly(true);
	else if (mode == "-i")
		channel->second.setInviteOnly(false);
	else if (mode == "+k")
	{
		if (command.params.size() < 3)
		{
			client->second.appendToOutBuffer(
				":ircserv 461 * MODE :Not enough parameters\r\n");
			enableWrite(fd);
			return ;
		}
		channel->second.setKey(command.params[2]);
	}
	else if (mode == "-k")
		channel->second.removeKey();
	else if (mode == "+l")
	{
		if (command.params.size() < 3)
		{
			client->second.appendToOutBuffer(
				":ircserv 461 * MODE :Not enough parameters\r\n");
			enableWrite(fd);
			return ;
		}

		int limit = std::atoi(command.params[2].c_str());
		if (limit <= 0)
		{
			client->second.appendToOutBuffer(
				":ircserv 461 * MODE :Invalid limit\r\n");
			enableWrite(fd);
			return ;
		}
		channel->second.setLimit(limit);
	}
	else if (mode == "-l")
		channel->second.removeLimit();
	else if (mode == "+o")
	{
		if (command.params.size() < 3)
		{
			client->second.appendToOutBuffer(
				":ircserv 461 * MODE :Not enough parameters\r\n");
			enableWrite(fd);
			return ;
		}

		std::string nickname = command.params[2];

		for (std::map<int, Client>::iterator target = _clients.begin();
			target != _clients.end(); ++target)
		{
			if (target->second.getNickname() == nickname)
			{
				if (!channel->second.hasClient(target->first))
				{
					client->second.appendToOutBuffer(
						":ircserv 441 " + nickname + " " + channelName
						+ " :They aren't on that channel\r\n");
					enableWrite(fd);
					return ;
				}

				channel->second.addOperator(target->first);

				std::string message = ":"
					+ client->second.getNickname()
					+ "!user@localhost MODE " + channelName
					+ " +o " + nickname + "\r\n";
				broadcastToChannel(channel->second, fd, message);
				client->second.appendToOutBuffer(message);
				enableWrite(fd);
				return ;
			}
		}

		client->second.appendToOutBuffer(
			":ircserv 401 " + nickname + " :No such nick\r\n");
		enableWrite(fd);
		return ;
	}
	else if (mode == "-o")
	{
		if (command.params.size() < 3)
		{
			client->second.appendToOutBuffer(
				":ircserv 461 * MODE :Not enough parameters\r\n");
			enableWrite(fd);
			return ;
		}

		std::string nickname = command.params[2];

		for (std::map<int, Client>::iterator target = _clients.begin();
			target != _clients.end(); ++target)
		{
			if (target->second.getNickname() == nickname)
			{
				if (!channel->second.hasClient(target->first))
				{
					client->second.appendToOutBuffer(
						":ircserv 441 " + nickname + " " + channelName
						+ " :They aren't on that channel\r\n");
					enableWrite(fd);
					return ;
				}

				channel->second.removeOperator(target->first);

				std::string message = ":"
					+ client->second.getNickname()
					+ "!user@localhost MODE " + channelName
					+ " -o " + nickname + "\r\n";
				broadcastToChannel(channel->second, fd, message);
				client->second.appendToOutBuffer(message);
				enableWrite(fd);
				return ;
			}
		}

		client->second.appendToOutBuffer(
			":ircserv 401 " + nickname + " :No such nick\r\n");
		enableWrite(fd);
		return ;
	}
	else
	{
		client->second.appendToOutBuffer(
			":ircserv 472 " + mode + " :is unknown mode char to me\r\n");
		enableWrite(fd);
		return ;
	}

	std::string message = ":" + client->second.getNickname()
		+ "!user@localhost MODE " + channelName
		+ " " + mode;

	if (mode == "+k" || mode == "+l")
		message += " " + command.params[2];

	message += "\r\n";

	broadcastToChannel(channel->second, fd, message);
	client->second.appendToOutBuffer(message);
	enableWrite(fd);
}

void Server::handleInvite(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;

	if (!client->second.isRegistered())
	{
		client->second.appendToOutBuffer(
			":ircserv 451 * :You have not registered\r\n");
		enableWrite(fd);
		return ;
	}

	if (command.params.size() < 2)
	{
		client->second.appendToOutBuffer(
			":ircserv 461 * INVITE :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	std::string nickname = command.params[0];
	std::string channelName = command.params[1];

	std::map<std::string, Channel>::iterator channel;
	channel = _channels.find(channelName);

	if (channel == _channels.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 403 " + channelName + " :No such channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.hasClient(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 442 " + channelName
			+ " :You're not on that channel\r\n");
		enableWrite(fd);
		return ;
	}

	if (!channel->second.isOperator(fd))
	{
		client->second.appendToOutBuffer(
			":ircserv 482 " + channelName
			+ " :You're not channel operator\r\n");
		enableWrite(fd);
		return ;
	}

	for (std::map<int, Client>::iterator target = _clients.begin();
		target != _clients.end(); ++target)
	{
		if (target->second.getNickname() == nickname)
		{
			channel->second.addInvite(target->first);

			target->second.appendToOutBuffer(
				":" + client->second.getNickname()
				+ "!user@localhost INVITE " + nickname
				+ " " + channelName + "\r\n");
			enableWrite(target->first);

			client->second.appendToOutBuffer(
				":ircserv 341 " + client->second.getNickname()
				+ " " + nickname + " " + channelName
				+ " :INVITE sent\r\n");
			enableWrite(fd);
			return ;
		}
	}

	client->second.appendToOutBuffer(
		":ircserv 401 " + nickname + " :No such nick\r\n");
	enableWrite(fd);
}

void Server::handleBotUsers(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;

	(void)command;
	std::string users;
	for (std::map<int, Client>::iterator client = _clients.begin();
		client != _clients.end(); ++client)
	{
		if (!client->second.isRegistered())
			continue ;
		if (!users.empty())
			users += ", ";
		users += client->second.getNickname();
	}

	it->second.appendToOutBuffer(
		":ircserv 900 " + it->second.getNickname()
		+ " :Users: " + users + "\r\n");
	enableWrite(fd);
}

void Server::handleFileSend(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator sender;
	std::map<int, Client>::iterator receiver;
	FileTransfer transfer;

	sender = _clients.find(fd);
	if (sender == _clients.end())
		return ;

	if (command.params.size() < 3)
	{
		sender->second.appendToOutBuffer(
			":ircserv 461 * FTSEND :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	receiver = _clients.begin();
	while (receiver != _clients.end())
	{
		if (receiver->second.getNickname() == command.params[0])
			break ;
		++receiver;
	}

	if (receiver == _clients.end())
	{
		sender->second.appendToOutBuffer(
			":ircserv 401 " + command.params[0]
			+ " :No such nick/channel\r\n");
		enableWrite(fd);
		return ;
	}

	transfer.senderFd = fd;
	transfer.receiverFd = receiver->first;
	transfer.filename = command.params[1];
	transfer.fileSize = std::atoi(command.params[2].c_str());
	transfer.bytesTransferred = 0;

	for (std::vector<FileTransfer>::iterator active =
		_fileTransfers.begin(); active != _fileTransfers.end();)
	{
		if (active->senderFd == fd)
			active = _fileTransfers.erase(active);
		else
			++active;
	}

	_fileTransfers.push_back(transfer);

	sender->second.setReceivingFile(true);
	sender->second.setFileBytesRemaining(transfer.fileSize);
	sender->second.getFileBuffer().clear();

	receiver->second.appendToOutBuffer(
		":" + sender->second.getNickname()
		+ "!user@localhost FTSEND " + transfer.filename
		+ " " + command.params[2] + "\r\n");
	enableWrite(receiver->first);

	sender->second.appendToOutBuffer(
		":ircserv 902 " + sender->second.getNickname()
		+ " :File transfer started\r\n");
	enableWrite(fd);
}

void Server::handleFileData(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator sender;
	std::vector<FileTransfer>::iterator transfer;

	sender = _clients.find(fd);
	if (sender == _clients.end())
		return ;

	if (command.params.size() < 2)
	{
		sender->second.appendToOutBuffer(
			":ircserv 461 * FTDATA :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	transfer = _fileTransfers.begin();
	while (transfer != _fileTransfers.end())
	{
		if (transfer->senderFd == fd
			&& _clients[transfer->receiverFd].getNickname()
			== command.params[0])
			break ;
		++transfer;
	}

	if (transfer == _fileTransfers.end())
	{
		sender->second.appendToOutBuffer(
			":ircserv 409 * FTDATA :No active file transfer\r\n");
		enableWrite(fd);
		return ;
	}

	std::string data = command.params[1];

	_clients[transfer->receiverFd].appendToOutBuffer(
		":" + sender->second.getNickname()
		+ "!user@localhost FTDATA :" + data + "\r\n");
	enableWrite(transfer->receiverFd);

	transfer->bytesTransferred += data.size();

	sender->second.appendToOutBuffer(
		":ircserv 903 " + sender->second.getNickname()
		+ " :Data sent\r\n");
	enableWrite(fd);
}

void Server::handleFileEnd(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator sender;
	std::vector<FileTransfer>::iterator transfer;

	sender = _clients.find(fd);
	if (sender == _clients.end())
		return ;

	if (command.params.empty())
	{
		sender->second.appendToOutBuffer(
			":ircserv 461 * FTEND :Not enough parameters\r\n");
		enableWrite(fd);
		return ;
	}

	transfer = _fileTransfers.begin();
	while (transfer != _fileTransfers.end())
	{
		if (transfer->senderFd == fd
			&& _clients[transfer->receiverFd].getNickname()
			== command.params[0])
			break ;
		++transfer;
	}

	if (transfer == _fileTransfers.end())
	{
		sender->second.appendToOutBuffer(
			":ircserv 409 * FTEND :No active file transfer\r\n");
		enableWrite(fd);
		return ;
	}

	_clients[transfer->receiverFd].appendToOutBuffer(
		":" + sender->second.getNickname()
		+ "!user@localhost FTEND\r\n");
	enableWrite(transfer->receiverFd);

	sender->second.appendToOutBuffer(
		":ircserv 904 " + sender->second.getNickname()
		+ " :File transfer completed\r\n");
	enableWrite(fd);

	_fileTransfers.erase(transfer);
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
}

void Server::disconnectClient(size_t pollIndex)
{
	int fd = _pollFds[pollIndex].fd;
	std::map<std::string, Channel>::iterator channel;

	std::cout << "Client disconnected: fd " << fd << std::endl;

	channel = _channels.begin();
	while (channel != _channels.end())
	{
		if (channel->second.hasClient(fd))
		{
			channel->second.removeClient(fd);
			channel->second.removeOperator(fd);
		}
		channel->second.removeInvite(fd);
		if (channel->second.getClients().empty())
			_channels.erase(channel++);
		else
			++channel;
	}

	for (std::vector<FileTransfer>::iterator transfer =
		_fileTransfers.begin(); transfer != _fileTransfers.end();)
	{
		if (transfer->senderFd == fd || transfer->receiverFd == fd)
			transfer = _fileTransfers.erase(transfer);
		else
			++transfer;
	}

	close(fd);
	_clients.erase(fd);

	_pollFds[pollIndex] = _pollFds.back();
	_pollFds.pop_back();
}

std::string Server::getPassword() const
{
	return (_password);
}
