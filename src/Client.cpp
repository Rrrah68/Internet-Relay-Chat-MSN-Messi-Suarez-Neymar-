#include "Client.hpp"

Client::Client() : _fd(-1), _authenticated(false), _sentPass(false)
{
}

Client::Client(int fd) : _fd(fd), _authenticated(false), _sentPass(false)
{
}

Client::~Client()
{
}

Client::Client(const Client &other)
{
	*this = other;
}

Client &Client::operator=(const Client &other)
{
	if (this != &other)
	{
		_fd = other._fd;
		_inBuffer = other._inBuffer;
		_outBuffer = other._outBuffer;
		_authenticated = other._authenticated;
		_sentPass = other._sentPass;
		_nickname = other._nickname;
		_username = other._username;
	}
	return (*this);
}

int Client::getFd() const
{
	return (_fd);
}

std::string &Client::getInBuffer()
{
	return (_inBuffer);
}

void Client::appendToInBuffer(const std::string &data)
{
	_inBuffer += data;
}

std::string &Client::getOutBuffer()
{
	return (_outBuffer);
}

void Client::appendToOutBuffer(const std::string &data)
{
	_outBuffer += data;
}

bool Client::isAuthenticated() const
{
	return (_authenticated);
}

void Client::setAuthenticated(bool value)
{
	_authenticated = value;
}

bool Client::hasSentPass() const
{
	return (_sentPass);
}

void Client::setSentPass(bool value)
{
	_sentPass = value;
}

const std::string &Client::getNickname() const
{
	return (_nickname);
}

void Client::setNickname(const std::string &nick)
{
	_nickname = nick;
}

const std::string &Client::getUsername() const
{
	return (_username);
}

void Client::setUsername(const std::string &username)
{
	_username = username;
}

bool Client::isRegistered() const
{
	return (_authenticated && _sentPass
		&& !_nickname.empty() && !_username.empty());
}