NAME		= ircserv
BOT_NAME	= ircbot

CXX			= c++

CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

SRC_DIR		= src
INC_DIR		= inc
OBJ_DIR		= obj
BOT_DIR		= bot

SRCS		= $(SRC_DIR)/main.cpp \
			  $(SRC_DIR)/Server.cpp \
			  $(SRC_DIR)/Registration.cpp \
			  $(SRC_DIR)/ChannelCommands.cpp \
			  $(SRC_DIR)/Mode.cpp \
			  $(SRC_DIR)/Messages.cpp \
			  $(SRC_DIR)/Queries.cpp \
			  $(SRC_DIR)/Client.cpp \
			  $(SRC_DIR)/Parser.cpp \
			  $(SRC_DIR)/Channel.cpp

OBJS		= $(OBJ_DIR)/main.o \
			  $(OBJ_DIR)/Server.o \
			  $(OBJ_DIR)/Registration.o \
			  $(OBJ_DIR)/ChannelCommands.o \
			  $(OBJ_DIR)/Mode.o \
			  $(OBJ_DIR)/Messages.o \
			  $(OBJ_DIR)/Queries.o \
			  $(OBJ_DIR)/Client.o \
			  $(OBJ_DIR)/Parser.o \
			  $(OBJ_DIR)/Channel.o

BOT_SRC		= $(BOT_DIR)/main.cpp
BOT_OBJ		= $(OBJ_DIR)/bot_main.o

all: $(NAME) $(BOT_NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(BOT_NAME): $(BOT_OBJ)
	$(CXX) $(CXXFLAGS) $(BOT_OBJ) -o $(BOT_NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

$(BOT_OBJ): $(BOT_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME) $(BOT_NAME)

re: fclean all

.PHONY: all clean fclean re