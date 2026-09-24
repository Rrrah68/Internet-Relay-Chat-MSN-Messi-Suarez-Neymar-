#include "Server.hpp"

#include <iostream>

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
	std::cout << "[fd " << fd << "] "
		<< (isNotice ? "NOTICE" : "PRIVMSG") << " target=" << target
		<< std::endl;
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

	if (!channelIt->second.hasClient(fd)
		&& it->second.getNickname() != "ircbot")
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
	std::cout << "[fd " << fd << "] PING" << std::endl;

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
	std::cout << "[fd " << fd << "] QUIT" << std::endl;

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
			channel->second.removeInvite(fd);
			if (channel->second.getClients().empty())
				_channels.erase(channel++);
			else
			{
				promoteSuccessor(channel->second);
				++channel;
			}
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
