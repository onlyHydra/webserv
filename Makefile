# **************************************************************************** #
#                                    CONFIG                                    #
# **************************************************************************** #

NAME                := webserv

# Test binaries
TEST_BIN            := test_webserv
TEST_BIN_STUBS      := test_webserv_stubs
CATCH2_LIB          := libcatch2.a

# Directories
SRC_DIR         := src
TEST_DIR        := tests
INC_DIR         := include
TESTS_INC_DIR   := $(TEST_DIR)/includes
CATCH2_DIR      := libraries/Catch2/src

BUILD_DIR       := build
OBJ98_DIR       := $(BUILD_DIR)/obj98
OBJ98_STUBS_DIR := $(BUILD_DIR)/obj98_stubs
OBJ14_DIR       := $(BUILD_DIR)/obj14_tests
LIB_DIR         := $(BUILD_DIR)/lib

# Auto-parallelism
JOBS      := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS += -j$(JOBS)

# ccache (optional)
CCACHE := $(shell command -v ccache 2>/dev/null)
CXX    := $(if $(CCACHE),ccache ,)c++
CC     := $(if $(CCACHE),ccache ,)cc
RM     := rm -rf
AR     := ar rcs

# Optional PCH (drop include/pch.hpp to enable)
PCH     := $(INC_DIR)/pch.hpp
PCHFLAG := $(if $(wildcard $(PCH)),-include $(PCH),)

# Flags
CXXFLAGS_98 := -Wall -Wextra -Werror -std=c++98 -MMD -MP -g -pipe $(PCHFLAG)
CXXFLAGS_14 := -std=c++14 -MMD -MP -pthread -pipe $(PCHFLAG) -g
CFLAGS_C    := -Wall -Wextra -Werror -std=c99   -MMD -MP -g -pipe

INCS      := -I$(INC_DIR)
TEST_INCS := -I$(TESTS_INC_DIR) -I$(INC_DIR) -I$(CATCH2_DIR)

TESTFLAGS := --reporter console --durations yes

# **************************************************************************** #
#                                   SOURCES                                    #
# **************************************************************************** #

# Production sources (auto-discovered)
SRCS_CPP        := $(shell find $(SRC_DIR) -type f -name "*.cpp")
SRCS_C          := $(shell find $(SRC_DIR) -type f -name "*.c")
MAIN_SRC        := $(SRC_DIR)/main.cpp
SRCS_CPP_NOMAIN := $(filter-out $(MAIN_SRC),$(SRCS_CPP))

# Tests
TEST_SRCS_CPP   := $(shell find $(TEST_DIR) -type f -name "*.cpp")

# Catch2 sources (prefer single-file amalgamation if present)
CATCH2_AMALG    := $(CATCH2_DIR)/catch_amalgamated.cpp
ifeq ($(wildcard $(CATCH2_AMALG)),)
  CATCH2_SRCS     := $(shell find $(CATCH2_DIR)/catch2 -type f -name "*.cpp" -not -path "*/tests/*" -not -path "*/examples/*")
  CATCH2_OBJS_14  := $(CATCH2_SRCS:$(CATCH2_DIR)/%.cpp=$(OBJ14_DIR)/catch2/%.o)
  CATCH2_AMALG_BUILD := 0
else
  CATCH2_SRCS     := $(CATCH2_AMALG)
  CATCH2_OBJS_14  := $(OBJ14_DIR)/catch2/catch_amalgamated.o
  CATCH2_AMALG_BUILD := 1
endif
CATCH2_LIB_PATH := $(LIB_DIR)/$(CATCH2_LIB)

# Objects
OBJS_98        := $(SRCS_CPP:$(SRC_DIR)/%.cpp=$(OBJ98_DIR)/%.o) \
                  $(SRCS_C:$(SRC_DIR)/%.c=$(OBJ98_DIR)/%.o)
OBJS_98_NOMAIN := $(SRCS_CPP_NOMAIN:$(SRC_DIR)/%.cpp=$(OBJ98_DIR)/%.o) \
                  $(SRCS_C:$(SRC_DIR)/%.c=$(OBJ98_DIR)/%.o)

OBJS_98_STUBS  := $(SRCS_CPP_NOMAIN:$(SRC_DIR)/%.cpp=$(OBJ98_STUBS_DIR)/%.o) \
                  $(SRCS_C:$(SRC_DIR)/%.c=$(OBJ98_STUBS_DIR)/%.o)

TEST_OBJS_14   := $(TEST_SRCS_CPP:$(TEST_DIR)/%.cpp=$(OBJ14_DIR)/%.o)

# Deps
DEPS_98         := $(OBJS_98:.o=.d)
DEPS_98_STUBS   := $(OBJS_98_STUBS:.o=.d)
TEST_DEPS_14    := $(TEST_OBJS_14:.o=.d)
CATCH2_DEPS_14  := $(CATCH2_OBJS_14:.o=.d)

# **************************************************************************** #
#                                    RULES                                     #
# **************************************************************************** #

.PHONY: all clean fclean re prod help \
        test test-fast run-tests \
        test-stubs run-tests-stubs \
        clean-tests catch2-lib re-tests

# ------------------------------ Production (42) ------------------------------ #

all: $(NAME)

$(NAME): $(OBJS_98)
	$(CXX) $(OBJS_98) -o $@

# C++98 objects (mirror subdirs)
$(OBJ98_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_98) $(INCS) -c $< -o $@

# C objects
$(OBJ98_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_C) $(INCS) -c $< -o $@

clean:
	@$(RM) $(OBJ98_DIR) $(OBJ98_STUBS_DIR) $(OBJ14_DIR) $(LIB_DIR)

fclean: clean
	@$(RM) $(NAME) $(TEST_BIN) $(TEST_BIN_STUBS)

re: fclean all

prod: all

# ------------------------------ Catch2 (tests) ------------------------------- #

catch2-lib: $(CATCH2_LIB_PATH)

$(CATCH2_LIB_PATH): $(CATCH2_OBJS_14)
	@mkdir -p $(LIB_DIR)
	@echo "Creating Catch2 library..."
	@$(AR) $@ $^

ifeq ($(CATCH2_AMALG_BUILD),1)
$(OBJ14_DIR)/catch2/catch_amalgamated.o: $(CATCH2_AMALG)
	@mkdir -p $(dir $@)
	@echo "Compiling Catch2 (amalgamated)..."
	$(CXX) $(CXXFLAGS_14) $(TEST_INCS) -c $< -o $@
else
$(OBJ14_DIR)/catch2/%.o: $(CATCH2_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling Catch2: $(notdir $<)"
	$(CXX) $(CXXFLAGS_14) $(TEST_INCS) -c $< -o $@
endif

# ------------------------------ Tests (prod objs) ---------------------------- #

test: $(TEST_BIN)

test-fast: $(TEST_BIN)
	@echo "Running tests..."
	@./$(TEST_BIN) $(TESTFLAGS)

run-tests:
	@if [ -f $(TEST_BIN) ]; then \
		echo "Running tests..."; \
		./$(TEST_BIN) $(TESTFLAGS); \
	else \
		echo "Test binary not found. Run 'make test' first."; \
		exit 1; \
	fi

$(TEST_BIN): $(OBJS_98_NOMAIN) $(TEST_OBJS_14) $(CATCH2_LIB_PATH)
	@echo "Linking test executable..."
	$(CXX) $(OBJS_98_NOMAIN) $(TEST_OBJS_14) $(CATCH2_LIB_PATH) -pthread -o $@

$(OBJ14_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_14) -DUNIT_TEST $(TEST_INCS) -c $< -o $@

# ------------------------------ Tests (stubs) -------------------------------- #

test-stubs: $(TEST_BIN_STUBS)

run-tests-stubs:
	@if [ -f $(TEST_BIN_STUBS) ]; then \
		echo "Running stubbed tests..."; \
		./$(TEST_BIN_STUBS) $(TESTFLAGS); \
	else \
		echo "Stubbed test binary not found. Run 'make test-stubs' first."; \
		exit 1; \
	fi

$(OBJ98_STUBS_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_98) -DUSE_STUBS -DUNIT_TEST $(TEST_INCS) -c $< -o $@

$(OBJ98_STUBS_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_C) -DUSE_STUBS -DUNIT_TEST $(TEST_INCS) -c $< -o $@

$(TEST_BIN_STUBS): $(OBJS_98_STUBS) $(TEST_OBJS_14) $(CATCH2_LIB_PATH)
	@echo "Linking stubbed test executable..."
	$(CXX) $(OBJS_98_STUBS) $(TEST_OBJS_14) $(CATCH2_LIB_PATH) -pthread -o $@

# ------------------------------ Utilities ----------------------------------- #

clean-tests:
	@$(RM) $(OBJ14_DIR) $(TEST_BIN) $(TEST_BIN_STUBS) $(OBJ98_STUBS_DIR) $(LIB_DIR)/$(CATCH2_LIB)

re-tests: clean-tests test

help:
	@echo ""
	@echo "==================== Make Help ===================="
	@echo "Production (42-style):"
	@echo "  make / all      - Build $(NAME) (C++98)"
	@echo "  clean           - Remove object and build directories"
	@echo "  fclean          - clean + remove binaries ($(NAME), tests)"
	@echo "  re              - Rebuild from scratch (fclean + all)"
	@echo ""
	@echo "Tests:"
	@echo "  catch2-lib      - Build Catch2 static library ($(CATCH2_LIB))"
	@echo "  test            - Build test executable ($(TEST_BIN)) using prod objects"
	@echo "  test-fast       - Build and run tests"
	@echo "  run-tests       - Run tests without rebuilding"
	@echo "  test-stubs      - Build stubbed test executable ($(TEST_BIN_STUBS))"
	@echo "  run-tests-stubs - Run stubbed tests without rebuilding"
	@echo "  clean-tests     - Remove test objs/bin and Catch2 lib"
	@echo "  re-tests        - Clean tests then build test"
	@echo ""
	@echo "Build speedups enabled:"
	@echo "  - Parallel jobs: $(JOBS)"
	@echo "  - ccache: $(if $(CCACHE),enabled,disabled)"
	@echo "  - PCH: $(if $(wildcard $(PCH)),using $(PCH),disabled)"
	@echo "  - Catch2 amalgamation: $(if $(filter 1,$(CATCH2_AMALG_BUILD)),enabled,disabled)"
	@echo "==================================================="
	@echo ""

# ------------------------------ Dependencies -------------------------------- #

-include $(DEPS_98)
-include $(DEPS_98_STUBS)
-include $(TEST_DEPS_14)
-include $(CATCH2_DEPS_14)
