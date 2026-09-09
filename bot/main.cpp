#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void sendCommand(int fd, const std::string &command)
{
	send(fd, command.c_str(), command.size(), 0);
}

static std::string getNick(const std::string &message)
{
	size_t start;
	size_t end;

	if (message.empty() || message[0] != ':')
		return ("");

	start = 1;
	end = message.find('!');

	if (end == std::string::npos)
		return ("");

	return (message.substr(start, end - start));
}

static void handleMessage(int fd, const std::string &message)
{
	std::string nick;
	std::string response;
	size_t pos;

	if (message.find("PRIVMSG ") == std::string::npos
	    && message.find(" 900 ") == std::string::npos
	    && message.find(" 901 ") == std::string::npos)
	    return ;

	if (message.find(" 900 ") != std::string::npos)
	{
		pos = message.find(" :Users: ");
		if (pos != std::string::npos)
		{
			response = "PRIVMSG #bot :Utilisateurs connectes: "
				+ message.substr(pos + 9) + "\r\n";
			sendCommand(fd, response);
		}
		return ;
	}
    if (message.find(" 901 ") != std::string::npos)
    {
	    pos = message.find(" :Channels: ");
	    if (pos != std::string::npos)
	    {
		    response = "PRIVMSG #bot :Channels actifs:"
		    	+ message.substr(pos + 11) + "\r\n";
            std::cout << ">> " << response;
		    sendCommand(fd, response);
	    }
        return ;
    }
	nick = getNick(message);
	if (nick.empty() || nick == "ircbot")
		return ;

	if (message.find(":!help") != std::string::npos)
	{
		response = "PRIVMSG " + nick
			+ " :Commandes: !help !ping !hello !users !channels !send\r\n";
		sendCommand(fd, response);
	}
	else if (message.find(":!ping") != std::string::npos)
	{
		response = "PRIVMSG " + nick
			+ " :PONG !\r\n";
		sendCommand(fd, response);
	}
	else if (message.find(":!hello") != std::string::npos)
	{
		response = "PRIVMSG " + nick
			+ " :Bonjour " + nick + " !\r\n";
		sendCommand(fd, response);
	}
	else if (message.find(":!users") != std::string::npos)
	{
		sendCommand(fd, "BOTUSERS\r\n");
	}
    else if (message.find(":!channels") != std::string::npos)
    {
	    sendCommand(fd, "BOTCHANNELS\r\n");
    }
    else if (message.find(":!send ") != std::string::npos)
    {
	    std::cout << "Commande !send recue" << std::endl;
    }
}

int main(int argc, char **argv)
{
	int fd;
	int port;
	struct sockaddr_in server;
	char buffer[4096];
	std::string input;
	std::string line;
	size_t pos;

	if (argc != 4)
	{
		std::cerr << "Usage: ./ircbot <ip> <port> <password>"
			<< std::endl;
		return (1);
	}

	port = std::atoi(argv[2]);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		std::cerr << "socket() failed" << std::endl;
		return (1);
	}

	std::memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if (inet_pton(AF_INET, argv[1], &server.sin_addr) <= 0)
	{
		std::cerr << "Invalid IP address" << std::endl;
		close(fd);
		return (1);
	}

	if (connect(fd, reinterpret_cast<struct sockaddr *>(&server),
			sizeof(server)) < 0)
	{
		std::cerr << "connect() failed" << std::endl;
		close(fd);
		return (1);
	}

	std::cout << "Connected to IRC server" << std::endl;

	sendCommand(fd, "PASS " + std::string(argv[3]) + "\r\n");
	sendCommand(fd, "NICK ircbot\r\n");
	sendCommand(fd, "USER bot 0 * :IRC Bot\r\n");
	sendCommand(fd, "JOIN #bot\r\n");

	while (true)
	{
		std::memset(buffer, 0, sizeof(buffer));

		ssize_t bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);

		if (bytes <= 0)
			break ;

		input.append(buffer, static_cast<size_t>(bytes));

		while ((pos = input.find('\n')) != std::string::npos)
		{
			line = input.substr(0, pos);
			input.erase(0, pos + 1);

			if (!line.empty() && line[line.size() - 1] == '\r')
				line.erase(line.size() - 1);

			std::cout << "<< " << line << std::endl;

			if (line.find("PING ") == 0)
			{
				sendCommand(
					fd,
					"PONG " + line.substr(5) + "\r\n"
				);
			}
			else
				handleMessage(fd, line);
		}
	}

	close(fd);
	std::cout << "Bot disconnected" << std::endl;

	return (0);
}