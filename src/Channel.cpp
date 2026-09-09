#include "Channel.hpp"
#include <algorithm>

Channel::Channel() : _topicRestricted(false), _inviteOnly(false), _keyEnabled(false), _limitEnabled(false), _limit(0)
{
}

Channel::Channel(const std::string &name)
	: _name(name), _topicRestricted(false), _inviteOnly(false), _keyEnabled(false), _limitEnabled(false), _limit(0)
{
}

Channel::~Channel()
{
}

const std::string &Channel::getName() const
{
	return (_name);
}

void Channel::addClient(int fd)
{
	if (!hasClient(fd))
		_clients.push_back(fd);
}

void Channel::removeClient(int fd)
{
	std::vector<int>::iterator it = std::find(_clients.begin(),
		_clients.end(), fd);
	if (it != _clients.end())
		_clients.erase(it);
}

bool Channel::hasClient(int fd) const
{
	for (std::vector<int>::const_iterator it = _clients.begin();
		it != _clients.end(); ++it)
	{
		if (*it == fd)
			return (true);
	}
	return (false);
}

const std::vector<int> &Channel::getClients() const
{
	return (_clients);
}

const std::string &Channel::getTopic() const
{
	return (_topic);
}

void Channel::setTopic(const std::string &topic)
{
	_topic = topic;
}

bool Channel::getTopicRestricted() const
{
	return (_topicRestricted);
}

void Channel::setTopicRestricted(bool value)
{
	_topicRestricted = value;
}

void Channel::addOperator(int fd)
{
	if (!isOperator(fd))
		_operators.push_back(fd);
}

bool Channel::isOperator(int fd) const
{
	for (std::vector<int>::const_iterator it = _operators.begin();
		it != _operators.end(); ++it)
	{
		if (*it == fd)
			return (true);
	}
	return (false);
}

void Channel::removeOperator(int fd)
{
	std::vector<int>::iterator it = std::find(_operators.begin(),
		_operators.end(), fd);
	if (it != _operators.end())
		_operators.erase(it);
}

bool Channel::getInviteOnly() const
{
	return (_inviteOnly);
}

void Channel::setInviteOnly(bool value)
{
	_inviteOnly = value;
}

void Channel::addInvite(int fd)
{
	if (!isInvited(fd))
		_invited.push_back(fd);
}

bool Channel::isInvited(int fd) const
{
	for (std::vector<int>::const_iterator it = _invited.begin();
		it != _invited.end(); ++it)
	{
		if (*it == fd)
			return (true);
	}
	return (false);
}

void Channel::removeInvite(int fd)
{
	std::vector<int>::iterator it = std::find(_invited.begin(),
		_invited.end(), fd);
	if (it != _invited.end())
		_invited.erase(it);
}

bool Channel::getKeyEnabled() const
{
	return (_keyEnabled);
}

const std::string &Channel::getKey() const
{
	return (_key);
}

void Channel::setKey(const std::string &key)
{
	_key = key;
	_keyEnabled = true;
}

void Channel::removeKey()
{
	_key.clear();
	_keyEnabled = false;
}

bool Channel::getLimitEnabled() const
{
	return (_limitEnabled);
}

int Channel::getLimit() const
{
	return (_limit);
}

void Channel::setLimit(int limit)
{
	_limit = limit;
	_limitEnabled = true;
}

void Channel::removeLimit()
{
	_limit = 0;
	_limitEnabled = false;
}