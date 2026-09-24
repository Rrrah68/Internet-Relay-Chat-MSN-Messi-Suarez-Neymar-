#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
# include <set>
# include <cstddef>

class Client
{
	public:
		Client();
		Client(int fd);
		~Client();
		Client(const Client &other);
		Client &operator=(const Client &other);

		int					getFd() const;

		std::string			&getInBuffer();
		void				appendToInBuffer(const std::string &data);

		std::string			&getOutBuffer();
		void				appendToOutBuffer(const std::string &data);

		bool				isAuthenticated() const;
		void				setAuthenticated(bool value);

		bool				hasSentPass() const;
		void				setSentPass(bool value);

		const std::string	&getNickname() const;
		void				setNickname(const std::string &nick);
		const std::set<std::string>	&getRejectedNicknames() const;
		void				addRejectedNickname(const std::string &nick);

		const std::string	&getUsername() const;
		void				setUsername(const std::string &username);

		bool				canRegister() const;
		bool				isRegistered() const;
		void				setRegistered(bool value);

	private:
		int			_fd;
		std::string	_inBuffer;
		std::string	_outBuffer;

		bool		_authenticated;
		bool		_sentPass;
		std::string	_nickname;
		std::set<std::string>	_rejectedNicknames;
		std::string	_username;
		bool		_registered;
};

#endif
