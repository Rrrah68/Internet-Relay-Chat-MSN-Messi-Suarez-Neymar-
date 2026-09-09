#ifndef PARSER_HPP
# define PARSER_HPP

# include <string>
# include <vector>

struct IRCCommand
{
	std::string				command;
	std::vector<std::string>	params;
	std::string				prefix;
};

class Parser
{
	public:
		Parser();
		~Parser();

		IRCCommand	parse(const std::string &line) const;

	private:
		Parser(const Parser &other);
		Parser	&operator=(const Parser &other);

		void	skipSpaces(const std::string &line, size_t &pos) const;
		void	parsePrefix(const std::string &line, size_t &pos,
				IRCCommand &result) const;
		void	parseCommand(const std::string &line, size_t &pos,
				IRCCommand &result) const;
		void	parseParams(const std::string &line, size_t &pos,
				IRCCommand &result) const;
};

#endif