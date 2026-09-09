#ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include <string>
# include <vector>

class Channel
{
	public:

		Channel();
		Channel(const std::string &name);
		~Channel();

		void					addOperator(int fd);
		void					addClient(int fd);
		void					addInvite(int fd);
		void					removeInvite(int fd);
		void					removeClient(int fd);
		void					removeOperator(int fd);
		void					setTopic(const std::string &topic);
		void					setTopicRestricted(bool value);
		void					setInviteOnly(bool value);
		void					setKey(const std::string &key);
		void					removeKey();

		bool					isOperator(int fd) const;
		bool					isInvited(int fd) const;
		bool					hasClient(int fd) const;
		bool					getTopicRestricted() const;
		bool					getInviteOnly() const;
		bool					getKeyEnabled() const;

		bool 					getLimitEnabled() const;
		int 					getLimit() const;
		void					setLimit(int limit);
		void					removeLimit();
	
		const std::string		&getName() const;
		const std::vector<int>	&getClients() const;
		const std::string		&getTopic() const;
		const std::string		&getKey() const;

	private:
		std::string			_name;
		std::vector<int>	_clients;
		std::string			_topic;
		bool				_topicRestricted;
		std::vector<int>	_operators;
		bool				_inviteOnly;
		std::vector<int>	_invited;
		bool				_keyEnabled;
		std::string			_key;
		bool				_limitEnabled;
		int					_limit;
};

#endif