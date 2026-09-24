#include "Server.hpp"

#include <iostream>
#include <cstdlib>

void Server::handleMode(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator client = _clients.find(fd);
	if (client == _clients.end())
		return ;
	std::cout << "[fd " << fd << "] MODE" << std::endl;
	std::cout << "[fd " << fd << "] MODE params:";
	for (size_t i = 0; i < command.params.size(); ++i)
		std::cout << " [" << command.params[i] << "]";
	std::cout << std::endl;

	if (!client->second.isRegistered())
		return (sendReply(fd, ":ircserv 451 * :You have not registered\r\n"));

	if (command.params.size() >= 2
		&& command.params[0] == client->second.getNickname())
		return ;

	if (command.params.empty())
		return ;

	std::string channelName = command.params[0];
	std::map<std::string, Channel>::iterator channelIt =
		_channels.find(channelName);

	if (channelIt == _channels.end())
		return (sendReply(fd, ":ircserv 403 " + channelName
			+ " :No such channel\r\n"));

	if (command.params.size() == 1)
		return (sendReply(fd, ":ircserv 324 " + client->second.getNickname()
			+ " " + channelName + " +\r\n"));

	Channel &channel = channelIt->second;
	std::string mode = command.params[1];

	if (!channel.hasClient(fd))
		return (sendReply(fd, ":ircserv 442 " + channelName
			+ " :You're not on that channel\r\n"));

	// Halfops can change every mode except +o/-o.
	if (!channel.isOperator(fd) && (!channel.isHalfOperator(fd)
		|| mode == "+o" || mode == "-o"))
		return (sendReply(fd, ":ircserv 482 " + channelName
			+ " :You're not channel operator\r\n"));

	if (mode == "b")
		return (sendReply(fd, ":ircserv 368 " + client->second.getNickname()
			+ " " + channelName + " :End of channel ban list\r\n"));

	if ((mode == "+k" || mode == "+l" || mode == "+o" || mode == "-o")
		&& command.params.size() < 3)
		return (sendReply(fd, ":ircserv 461 * MODE :Not enough parameters\r\n"));

	if (mode == "+t" || mode == "-t")
		channel.setTopicRestricted(mode[0] == '+');
	else if (mode == "+i")
		channel.setInviteOnly(true);
	else if (mode == "-i")
	{
		channel.setInviteOnly(false);
		joinPendingClients(channelName);
	}
	else if (mode == "+k")
		channel.setKey(command.params[2]);
	else if (mode == "-k")
		channel.removeKey();
	else if (mode == "+l")
	{
		int limit = std::atoi(command.params[2].c_str());
		if (limit <= 0)
			return (sendReply(fd, ":ircserv 461 * MODE :Invalid limit\r\n"));
		channel.setLimit(limit);
	}
	else if (mode == "-l")
		channel.removeLimit();
	else if (mode == "+o" || mode == "-o")
		return (setChannelOperator(fd, channel, mode, command.params[2]));
	else
		return (sendReply(fd, ":ircserv 472 " + mode
			+ " :is unknown mode char to me\r\n"));

	std::string message = ":" + client->second.getNickname()
		+ "!user@localhost MODE " + channelName + " " + mode;
	if (mode == "+k" || mode == "+l")
		message += " " + command.params[2];
	message += "\r\n";

	broadcastToChannel(channel, fd, message);
	sendReply(fd, message);
}

// +o on a channel that already has an operator only gives halfop rights.
// -o removes both, then makes sure the channel keeps an operator.
void Server::setChannelOperator(int fd, Channel &channel,
	const std::string &mode, const std::string &nickname)
{
	std::map<int, Client>::iterator target = _clients.begin();

	while (target != _clients.end()
		&& target->second.getNickname() != nickname)
		++target;

	if (target == _clients.end())
		return (sendReply(fd, ":ircserv 401 " + nickname
			+ " :No such nick\r\n"));

	if (!channel.hasClient(target->first))
		return (sendReply(fd, ":ircserv 441 " + nickname + " "
			+ channel.getName() + " :They aren't on that channel\r\n"));

	// Clients are told the real rank: +h/-h (irssi shows %) for a halfop.
	std::string announced = mode;
	if (mode == "-o")
	{
		if (!channel.isOperator(target->first)
			&& channel.isHalfOperator(target->first))
			announced = "-h";
		channel.removeOperator(target->first);
		channel.removeHalfOperator(target->first);
	}
	else if (channel.hasOperator())
	{
		channel.addHalfOperator(target->first);
		if (!channel.isOperator(target->first))
			announced = "+h";
	}
	else
		channel.addOperator(target->first);

	std::string message = ":" + _clients.find(fd)->second.getNickname()
		+ "!user@localhost MODE " + channel.getName()
		+ " " + announced + " " + nickname + "\r\n";
	broadcastToChannel(channel, fd, message);
	sendReply(fd, message);

	if (mode == "-o")
		promoteSuccessor(channel, target->first);
}

// Clients refused by +i retry their JOIN once the channel is opened.
void Server::joinPendingClients(const std::string &channelName)
{
	std::map<std::string, std::set<int> >::iterator pending =
		_pendingJoins.find(channelName);
	if (pending == _pendingJoins.end())
		return ;

	std::set<int> waiting = pending->second;
	_pendingJoins.erase(pending);
	for (std::set<int>::iterator waitingClient = waiting.begin();
		waitingClient != waiting.end(); ++waitingClient)
	{
		if (_clients.find(*waitingClient) == _clients.end())
			continue ;
		IRCCommand joinCommand;
		joinCommand.command = "JOIN";
		joinCommand.params.push_back(channelName);
		handleJoin(*waitingClient, joinCommand);
	}
}
