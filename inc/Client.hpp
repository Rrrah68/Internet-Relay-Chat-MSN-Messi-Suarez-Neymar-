#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>
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

		const std::string	&getUsername() const;
		void				setUsername(const std::string &username);

		bool				isRegistered() const;
		bool				isReceivingFile() const;
		void				setReceivingFile(bool value);

		size_t				getFileBytesRemaining() const;
		void				setFileBytesRemaining(size_t value);

		std::string			&getFileBuffer();
		void				appendToFileBuffer(const std::string &data);	
		
	private:
		int			_fd;
		std::string	_inBuffer;
		std::string	_outBuffer;

		bool		_authenticated;
		bool		_sentPass;
		std::string	_nickname;
		std::string	_username;
		bool		_receivingFile;
		size_t		_fileBytesRemaining;
		std::string	_fileBuffer;
};

#endif
