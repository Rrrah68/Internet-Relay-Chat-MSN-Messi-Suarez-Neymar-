#include <iostream>
#include <cstdlib>
#include <cctype>
#include "Server.hpp"

static bool isValidPort(const std::string &s, int &outPort)
{
	if (s.empty())
		return (false);
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return (false);
	}
	long value = std::atol(s.c_str());
	if (value <= 0 || value > 65535)
		return (false);
	outPort = static_cast<int>(value);
	return (true);
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
		return (1);
	}

	int port;
	if (!isValidPort(argv[1], port))
	{
		std::cerr << "Error: invalid port '" << argv[1] << "'" << std::endl;
		return (1);
	}

	std::string password(argv[2]);
	if (password.empty())
	{
		std::cerr << "Error: password cannot be empty" << std::endl;
		return (1);
	}

	try
	{
		Server server(port, password);
		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Fatal error: " << e.what() << std::endl;
		return (1);
	}

	return (0);
}
