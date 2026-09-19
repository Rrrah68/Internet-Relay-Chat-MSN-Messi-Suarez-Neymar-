#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
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


		Parser 							_parser;

		
		
		static volatile bool			_shutdown;

		void	setupSocket();
		bool	setNonBlocking(int fd);

		void	acceptNewClient();
		void	addClient(int fd, const struct sockaddr_in &clientAddr);
		void	handleClientRead(int fd);
		void	flushClientWrite(int fd);
		void	disconnectClient(size_t pollIndex);

		void	extractCommands(int fd);
		void	processCommand(int fd, const std::string &line);
		void	handlePass(int fd, const IRCCommand &command);
		void	handleCap(int fd, const IRCCommand &command);
		void	handleNick(int fd, const IRCCommand &command);
		void	handleUser(int fd, const IRCCommand &command);
		void	enableWrite(int fd);
		
		void	handleJoin(int fd, const IRCCommand &command);
		void	handlePart(int fd, const IRCCommand &command);
		void	handlePrivmsg(int fd, const IRCCommand &command,
			bool isNotice = false);
		void	handlePing(int fd, const IRCCommand &command);
		void	handleQuit(int fd, const IRCCommand &command);
		void	handleTopic(int fd, const IRCCommand &command);
		void	handleInvite(int fd, const IRCCommand &command);
		void	handleBotUsers(int fd, const IRCCommand &command);
		void	handleKick(int fd, const IRCCommand &command);
		void	handleBotChannels(int fd, const IRCCommand &command);
		Channel	&getOrCreateChannel(const std::string &name);
		void	broadcastToChannel(const Channel &channel, int senderFd, const std::string &message);
		void	handleWho(int fd, const IRCCommand &command);
		void	handleMode(int fd, const IRCCommand &command);
		bool	isValidNickname(const std::string &nickname) const;
		

		std::string	getPassword() const;
};

#endif
