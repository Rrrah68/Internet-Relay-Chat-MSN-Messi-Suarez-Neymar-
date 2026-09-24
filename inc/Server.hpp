#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <set>
# include <poll.h>
# include "Client.hpp"
# include "Parser.hpp"
# include "Channel.hpp"

class Server
{
	public:
		Server(int port, const std::string &password);
		~Server();

		void				run();

		static void			signalHandler(int signum);

	private:
		Server();
		Server(const Server &other);
		Server &operator=(const Server &other);

		int								_port;
		std::string						_password;
		int								_listenFd;
		std::vector<struct pollfd>		_pollFds;
		std::map<int, Client>			_clients;
		std::map<std::string, Channel>	_channels;
		std::map<std::string, std::set<int> >	_pendingJoins;

		Parser 							_parser;

		static volatile bool			_shutdown;

		/* Server.cpp: sockets, event loop, dispatch and output helpers. */
		void	setupSocket();
		bool	setNonBlocking(int fd);
		void	acceptNewClient();
		void	addClient(int fd, const struct sockaddr_in &clientAddr);
		void	handleClientRead(int fd);
		void	flushClientWrite(int fd);
		void	disconnectClient(size_t pollIndex);
		void	extractCommands(int fd);
		void	processCommand(int fd, const std::string &line);
		void	enableWrite(int fd);
		void	sendReply(int fd, const std::string &message);
		void	broadcastToChannel(const Channel &channel, int senderFd, const std::string &message);
		std::string	getPassword() const;

		/* Registration.cpp: CAP, PASS, NICK, USER. */
		void	handleCap(int fd, const IRCCommand &command);
		void	handlePass(int fd, const IRCCommand &command);
		void	handleNick(int fd, const IRCCommand &command);
		void	handleUser(int fd, const IRCCommand &command);
		void	completeRegistration(int fd);
		bool	isValidNickname(const std::string &nickname) const;
		bool	isNicknameVariant(const std::string &nick,
			const std::set<std::string> &rejected) const;

		/* ChannelCommands.cpp: JOIN, PART, KICK, INVITE, TOPIC. */
		void	handleJoin(int fd, const IRCCommand &command);
		void	handlePart(int fd, const IRCCommand &command);
		void	handleKick(int fd, const IRCCommand &command);
		void	handleInvite(int fd, const IRCCommand &command);
		void	handleTopic(int fd, const IRCCommand &command);
		Channel	&getOrCreateChannel(const std::string &name);
		void	promoteSuccessor(Channel &channel, int excludedFd = -1);

		/* Mode.cpp: MODE. */
		void	handleMode(int fd, const IRCCommand &command);
		void	setChannelOperator(int fd, Channel &channel,
			const std::string &mode, const std::string &nickname);
		void	joinPendingClients(const std::string &channelName);

		/* Messages.cpp: PRIVMSG, NOTICE, PING, QUIT. */
		void	handlePrivmsg(int fd, const IRCCommand &command,
			bool isNotice = false);
		void	handlePing(int fd, const IRCCommand &command);
		void	handleQuit(int fd, const IRCCommand &command);

		/* Queries.cpp: WHO, WHOIS, WHOWAS and bot queries. */
		void	handleWho(int fd, const IRCCommand &command);
		void	handleWhois(int fd, const IRCCommand &command);
		void	handleWhowas(int fd, const IRCCommand &command);
		void	handleBotUsers(int fd, const IRCCommand &command);
		void	handleBotChannels(int fd, const IRCCommand &command);
};

#endif
