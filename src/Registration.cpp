#include "Server.hpp"

#include <iostream>
#include <cctype>

void Server::handleCap(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);

	if (it == _clients.end() || command.params.empty())
		return ;

	if (command.params[0] == "LS")
	{
		it->second.appendToOutBuffer(":ircserv CAP * LS :\r\n");
		enableWrite(fd);
	}
	else if (command.params[0] == "REQ" && command.params.size() > 1)
	{
		it->second.appendToOutBuffer(":ircserv CAP * ACK :"
			+ command.params[1] + "\r\n");
		enableWrite(fd);
	}
}

void Server::handlePass(int fd, const IRCCommand &command)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end())
		return ;
	std::cout << "[fd " << fd << "] PASS received" << std::endl;
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
		std::cout << "[fd " << fd << "] authenticated" << std::endl;
	}
	else
	{
		std::cout << "[fd " << fd << "] authentication failed" << std::endl;
		it->second.appendToOutBuffer(":ircserv 464 * :Password incorrect\r\n");
	}
	if (!it->second.getOutBuffer().empty())
		enableWrite(fd);
}

bool Server::isValidNickname(const std::string &nickname) const
{
	if (nickname.empty())
		return false;
	if (!std::isalpha(nickname[0]) && nickname[0] != '['
		&& nickname[0] != ']' && nickname[0] != '\\'
		&& nickname[0] != '^' && nickname[0] != '_'
		&& nickname[0] != '`' && nickname[0] != '{'
		&& nickname[0] != '|' && nickname[0] != '}')
		return false;
	for (size_t i = 1; i < nickname.size(); ++i)
	{
		if (!std::isalnum(nickname[i]) && nickname[i] != '['
			&& nickname[i] != ']' && nickname[i] != '\\'
			&& nickname[i] != '^' && nickname[i] != '_'
			&& nickname[i] != '`' && nickname[i] != '{'
			&& nickname[i] != '|' && nickname[i] != '}')
			return false;
	}
	return true;
}

static std::string stripNickSuffix(const std::string &nick)
{
	size_t end = nick.size();

	while (end > 0 && (nick[end - 1] == '_'
		|| std::isdigit(static_cast<unsigned char>(nick[end - 1]))))
		--end;
	return (nick.substr(0, end));
}

// Irssi retries a refused nick by appending '_' (or digits once the nick
// reaches 9 chars, truncating it). Detect those automatic variants.
bool Server::isNicknameVariant(const std::string &nick,
	const std::set<std::string> &rejected) const
{
	std::string candidate = stripNickSuffix(nick);

	for (std::set<std::string>::const_iterator it = rejected.begin();
		it != rejected.end(); ++it)
	{
		std::string base = stripNickSuffix(*it);
		size_t minLength = base.size() < 8 ? base.size() : 8;

		if (nick == *it || candidate.empty() || base.empty())
			continue ;
		if (candidate.size() >= minLength
			&& base.compare(0, candidate.size(), candidate) == 0)
			return (true);
	}
	return (false);
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

	if (!it->second.isRegistered()
		&& isNicknameVariant(nick, it->second.getRejectedNicknames()))
	{
		it->second.appendToOutBuffer(
			":ircserv NOTICE * :Nickname " + nick
			+ " refused, choose another one with NICK <nickname>"
			+ " (irssi: /quote NICK <nickname>)\r\n");
		enableWrite(fd);
		return ;
	}

	for (std::map<int, Client>::iterator client = _clients.begin();
		client != _clients.end(); ++client)
	{
		if (client->first != fd
			&& client->second.getNickname() == nick)
		{
			if (!it->second.isRegistered())
				it->second.addRejectedNickname(nick);
					it->second.appendToOutBuffer(
							":ircserv NOTICE * :Nickname already in use\r\n");
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

	if (!it->second.isRegistered())
	{
		completeRegistration(fd);
		return ;
	}

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
	completeRegistration(fd);
}

// Called after NICK and USER: registration ends once PASS, NICK and USER
// are all valid, whatever order they arrived in.
void Server::completeRegistration(int fd)
{
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it == _clients.end() || !it->second.canRegister())
		return ;

	it->second.setRegistered(true);
	it->second.appendToOutBuffer(
		":ircserv 001 " + it->second.getNickname()
		+ " :Welcome to the IRC network\r\n");
	enableWrite(fd);
	std::cout << "[fd " << fd << "] CLIENT REGISTERED" << std::endl;

	std::map<std::string, Channel>::iterator general =
		_channels.find("#general");
	if (general == _channels.end() || !general->second.getInviteOnly())
	{
		IRCCommand defaultJoin;
		defaultJoin.command = "JOIN";
		defaultJoin.params.push_back("#general");
		handleJoin(fd, defaultJoin);
	}
}
