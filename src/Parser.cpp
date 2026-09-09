#include "Parser.hpp"
#include <cctype>

Parser::Parser()
{
}

Parser::~Parser()
{
}

IRCCommand	Parser::parse(const std::string &line) const
{
	IRCCommand	result;
	size_t		pos;

	pos = 0;
	result.prefix = "";
	result.command = "";

	skipSpaces(line, pos);
	if (pos >= line.size())
		return result;

	if (line[pos] == ':')
		parsePrefix(line, pos, result);

	skipSpaces(line, pos);
	parseCommand(line, pos, result);
	skipSpaces(line, pos);
	parseParams(line, pos, result);

	return result;
}

void	Parser::skipSpaces(const std::string &line, size_t &pos) const
{
	while (pos < line.size() && line[pos] == ' ')
		++pos;
}

void	Parser::parsePrefix(const std::string &line, size_t &pos,
	IRCCommand &result) const
{
	size_t	start;

	if (pos >= line.size() || line[pos] != ':')
		return ;

	++pos;
	start = pos;
	while (pos < line.size() && line[pos] != ' ')
		++pos;

	result.prefix = line.substr(start, pos - start);
}

void	Parser::parseCommand(const std::string &line, size_t &pos,
	IRCCommand &result) const
{
	size_t	start;

	start = pos;
	while (pos < line.size() && line[pos] != ' ')
		++pos;

	result.command = line.substr(start, pos - start);
	for (size_t i = 0; i < result.command.size(); ++i)
	{
		result.command[i] = static_cast<char>(std::toupper(
			static_cast<unsigned char>(result.command[i])));
	}
}

void	Parser::parseParams(const std::string &line, size_t &pos,
	IRCCommand &result) const
{
	while (pos < line.size())
	{
		skipSpaces(line, pos);
		if (pos >= line.size())
			break ;

		if (line[pos] == ':')
		{
			++pos;
			result.params.push_back(line.substr(pos));
			return ;
		}

		size_t start = pos;
		while (pos < line.size() && line[pos] != ' ')
			++pos;

		result.params.push_back(line.substr(start, pos - start));
	}
}
