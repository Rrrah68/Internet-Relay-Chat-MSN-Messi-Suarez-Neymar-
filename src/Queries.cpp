#include "Server.hpp"

void Server::handleWho(int fd, const IRCCommand &command)
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
			":ircserv 315 " + client->second.getNickname()
			+ " * :End of /WHO list.\r\n");
		enableWrite(fd);
		return ;
	}

	std::string channelName = command.params[0];
	std::map<std::string, Channel>::iterator channel;

	channel = _channels.find(channelName);
	if (channel == _channels.end())
	{
		client->second.appendToOutBuffer(
			":ircserv 315 " + client->second.getNickname()
			+ " " + channelName
			+ " :End of /WHO list.\r\n");
		enableWrite(fd);
		return ;
	}

	// On ajoutera ici les utilisateurs du channel.

	client->second.appendToOutBuffer(
		":ircserv 315 " + client->second.getNickname()
		+ " " + channelName
		+ " :End of /WHO list.\r\n");
	enableWrite(fd);
}

// Minimal WHOIS: only 311 (user info) or 401, then 318.
void Server::handleWhois(int fd, const IRCCommand &command)
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
			":ircserv 431 " + client->second.getNickname()
			+ " :No nickname given\r\n");
		enableWrite(fd);
		return ;
	}

	// "WHOIS <server> <nick>" is also valid: the nick is the last param.
	std::string nickname = command.params[command.params.size() - 1];
	std::string requester = client->second.getNickname();
	std::map<int, Client>::iterator target = _clients.begin();

	while (target != _clients.end()
		&& (!target->second.isRegistered()
		|| target->second.getNickname() != nickname))
		++target;

	if (target == _clients.end())
		client->second.appendToOutBuffer(
			":ircserv 401 " + requester + " " + nickname
			+ " :No such nick/channel\r\n");
	else
		client->second.appendToOutBuffer(
			":ircserv 311 " + requester + " " + nickname + " "
			+ target->second.getUsername() + " localhost * :"
			+ target->second.getUsername() + "\r\n");
	client->second.appendToOutBuffer(
		":ircserv 318 " + requester + " " + nickname
		+ " :End of /WHOIS list.\r\n");
	enableWrite(fd);
}

// No nick history is kept: irssi sends WHOWAS after a WHOIS 401.
void Server::handleWhowas(int fd, const IRCCommand &command)
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

	std::string requester = client->second.getNickname();
	if (command.params.empty())
	{
		client->second.appendToOutBuffer(
			":ircserv 431 " + requester + " :No nickname given\r\n");
		enableWrite(fd);
		return ;
	}

	client->second.appendToOutBuffer(
		":ircserv 406 " + requester + " " + command.params[0]
		+ " :There was no such nickname\r\n");
	client->second.appendToOutBuffer(
		":ircserv 369 " + requester + " " + command.params[0]
		+ " :End of WHOWAS\r\n");
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
