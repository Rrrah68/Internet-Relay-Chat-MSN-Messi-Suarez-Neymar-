#include "Server.hpp"

#include <iostream>

Channel &Server::getOrCreateChannel(const std::string &name)
{
	std::map<std::string, Channel>::iterator it = _channels.find(name);

	if (it != _channels.end())
		return (it->second);

	_channels.insert(std::make_pair(name, Channel(name)));
	std::cout << "[server] channel created " << name << std::endl;
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

		if (channelName == "0")
		{
			if (pos == std::string::npos)
				break ;
			start = pos + 1;
			continue ;
		}

		if (!channelName.empty() && channelName[0] == '#')
		{
			Channel &channel = getOrCreateChannel(channelName);

			if (!channel.hasClient(fd))
			{
				if (channel.getInviteOnly() && !channel.isInvited(fd))
				{
					_pendingJoins[channelName].insert(fd);
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
					// Only the first human member becomes operator: once
					// the channel has people, promoteSuccessor handles it.
					bool hasHumanMember = false;
					const std::vector<int> &current = channel.getClients();
					for (size_t i = 0; i < current.size(); ++i)
					{
						std::map<int, Client>::iterator member =
							_clients.find(current[i]);
						if (member != _clients.end()
							&& member->second.getNickname() != "ircbot")
							hasHumanMember = true;
					}

					channel.addClient(fd);
					channel.removeInvite(fd);
					_pendingJoins[channelName].erase(fd);

					if (it->second.getNickname() != "ircbot"
						&& !hasHumanMember)
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
						else if (channel.isHalfOperator(members[i]))
							names += "%";
						names += member->second.getNickname();
					}
					it->second.appendToOutBuffer(
						":ircserv 353 " + it->second.getNickname()
						+ " = " + channelName + " :" + names + "\r\n");

					it->second.appendToOutBuffer(
						":ircserv 366 " + it->second.getNickname()
						+ " " + channelName
						+ " :End of /NAMES list.\r\n");

enableWrite(fd);
					std::cout << "[fd " << fd << "] joined " << channelName
						<< std::endl;
				}
			}
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
	std::cout << "[fd " << fd << "] PART" << std::endl;

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
				channel.removeInvite(fd);

				broadcastToChannel(channel, fd, message);
				it->second.appendToOutBuffer(message);
				enableWrite(fd);

				if (channel.getClients().empty())
					_channels.erase(channelIt);
				else
					promoteSuccessor(channel);
			}
		}

		if (pos == std::string::npos)
			break ;
		start = pos + 1;
	}
}

void Server::handleKick(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;
	std::cout << "[fd " << fd << "] KICK" << std::endl;

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
	channel->second.removeInvite(target->first);

	if (channel->second.getClients().empty())
		_channels.erase(channel);
	else
		promoteSuccessor(channel->second);
}

void Server::handleInvite(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;
	std::cout << "[fd " << fd << "] INVITE" << std::endl;

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

void Server::handleTopic(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;
	std::cout << "[fd " << fd << "] TOPIC" << std::endl;

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

// Called when the channel may have lost its operator (member left or -o):
// the oldest halfop takes over, otherwise the oldest member (never ircbot).
// excludedFd (the de-opped client) is only picked if nobody else can be.
void Server::promoteSuccessor(Channel &channel, int excludedFd)
{
	if (channel.hasOperator() || channel.getClients().empty())
		return ;

	int successor = -1;
	const std::vector<int> &halfOperators = channel.getHalfOperators();
	if (!halfOperators.empty())
		successor = halfOperators[0];
	else
	{
		const std::vector<int> &members = channel.getClients();
		for (size_t i = 0; i < members.size(); ++i)
		{
			std::map<int, Client>::iterator member = _clients.find(members[i]);
			if (member != _clients.end() && members[i] != excludedFd
				&& member->second.getNickname() != "ircbot")
			{
				successor = members[i];
				break ;
			}
		}
		if (successor == -1)
			successor = excludedFd;
	}

	std::map<int, Client>::iterator client = _clients.find(successor);
	if (client == _clients.end())
		return ;

	if (channel.isHalfOperator(successor))
	{
		channel.removeHalfOperator(successor);
		broadcastToChannel(channel, -1, ":ircserv MODE " + channel.getName()
			+ " -h " + client->second.getNickname() + "\r\n");
	}
	channel.addOperator(successor);
	broadcastToChannel(channel, -1, ":ircserv MODE " + channel.getName()
		+ " +o " + client->second.getNickname() + "\r\n");
}
