# **************************************************************************** #
#                                    CONFIG                                    #
# **************************************************************************** #

NAME                := webserv

# Directories
SRC_DIR         := src
INC_DIR         := include

BUILD_DIR       := build
OBJ98_DIR       := $(BUILD_DIR)/obj98

# ccache (optional)
CXX    := c++
RM     := rm -rf
AR     := ar rcs

# Flags
CXXFLAGS_98 := -Wall -Wextra -Werror -std=c++98 -MMD -MP -g -pipe

INCS      := -I$(INC_DIR)

# **************************************************************************** #
#                                   SOURCES                                    #
# **************************************************************************** #

# Production sources (auto-discovered)
SRCS_CPP       = \
src/routing/RouteResolver.cpp \
src/routing/ProxyHandler.cpp \
src/routing/PutPatchHandler.cpp \
src/routing/ReturnHandler.cpp \
src/routing/Router.cpp \
src/routing/IpList.cpp \
src/routing/CgiHandler.cpp \
src/routing/StaticHandler.cpp \
src/cgi/CgiRegistry.cpp \
src/cgi/CGIStreamer.cpp \
src/cgi/CgiProcess.cpp \
src/core/ExpectContinue.cpp \
src/core/HeaderProcessor.cpp \
src/core/RequestGuard.cpp \
src/core/ServerPipeline.cpp \
src/networking/ConnectionIO.cpp \
src/networking/ContentLengthReader.cpp \
src/networking/ClientHandler.cpp \
src/networking/Server.cpp \
src/networking/ChunkedReader.cpp \
src/networking/ClientConnection.cpp \
src/networking/EventLoop.cpp \
src/networking/FileBodyReader.cpp \
src/util/ETagUtil.cpp \
src/util/Atoi.cpp \
src/util/PathUtil.cpp \
src/util/RateLimiter.cpp \
src/util/TimeUtil.cpp \
src/util/Util.cpp \
src/util/ResponseFactory.cpp \
src/config/CgiSpec.cpp \
src/config/Location.cpp \
src/config/MultipartReader.cpp \
src/config/VirtualServer.cpp \
src/config/ServerConfig.cpp \
src/config/UploadHandler.cpp \
src/http/CiLess.cpp \
src/http/CookieJar.cpp \
src/http/Headers.cpp \
src/http/HttpPreconditions.cpp \
src/http/HttpRequest.cpp \
src/http/HttpResponse.cpp \
src/main.cpp

# Object
OBJS_98        := $(SRCS_CPP:$(SRC_DIR)/%.cpp=$(OBJ98_DIR)/%.o)

# Deps
DEPS_98         := $(OBJS_98:.o=.d)

# **************************************************************************** #
#                                    RULES                                     #
# **************************************************************************** #

.PHONY: all clean fclean re val

# ------------------------------ Production (42) ------------------------------ #

all: $(NAME)

src:
	echo $(OBJS_98)

$(NAME): $(OBJS_98)
	$(CXX) $(OBJS_98) -o $@

# C++98 objects (mirror subdirs)
$(OBJ98_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_98) $(INCS) -c $< -o $@

clean:
	@$(RM) $(OBJ98_DIR)

fclean: clean
	@$(RM) $(NAME)

re: fclean all

val: $(NAME)
	valgrind --leak-check=full --track-fds=yes --trace-children=yes ./$(NAME)

# ------------------------------ Dependencies -------------------------------- #

-include $(DEPS_98)
