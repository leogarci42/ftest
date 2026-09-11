CXX = c++
CC  = cc
ROOT = ..
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -pthread -I. -I$(ROOT)/includes -Iftest -Iftl
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -Iftl
LDFLAGS =
LDLIBS = -lreadline -lpthread
DEBUG_LDFLAGS = -fsanitize=address,undefined

LIB_TEST = test_library
UNIT_TEST = test_unit
CLI_TEST = test_cli
SMOKE_TEST = c_smoke_test
PROJECT_OBJS = $(shell find $(ROOT)/obj -name '*.o' ! -name 'main.o' 2>/dev/null)

HEADERS = $(shell find ftl ftest -name '*.hpp' -o -name '*.h' | sort)
FT_HEADERS = $(shell find ftest -name '*.hpp' | sort)

all: selfcheck c-smoke

selfcheck: $(LIB_TEST)
	@printf "running ftest library self-check...\n"
	@./$(LIB_TEST)

project: $(UNIT_TEST) $(CLI_TEST)
	@printf "running unit series...\n"
	@./$(UNIT_TEST)
	@printf "running CLI regression series...\n"
	@./$(CLI_TEST)

$(LIB_TEST): test_library.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) test_library.cpp -o $@ $(DEBUG_LDFLAGS)

$(UNIT_TEST): test_unit.cpp $(HEADERS) $(PROJECT_OBJS)
	$(CXX) $(CXXFLAGS) test_unit.cpp $(PROJECT_OBJS) -o $@ $(LDFLAGS) $(LDLIBS) $(DEBUG_LDFLAGS)

$(CLI_TEST): test_cli.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) test_cli.cpp -o $@ $(DEBUG_LDFLAGS)

c-smoke: $(SMOKE_TEST)
	@printf "running pure-C API smoke test...\n"
	@./$(SMOKE_TEST)

$(SMOKE_TEST): c_smoke.c ftl/ftl.h
	$(CC) $(CFLAGS) c_smoke.c -o $@

# Strict-POSIX fallback probe: strips FTL_HAVE_* macros from a scratch copy
# of ftl/, then runs scripts/portable_fallback_test.cpp against it.
portable:
	@rm -rf /tmp/fakeftl && mkdir -p /tmp/fakeftl && cp -r ftl /tmp/fakeftl/ftl
	@sed -i 's/#define FTL_HAVE_EXECVPE 1//; s/#define FTL_HAVE_TIMEDJOIN_NP 1//' \
		/tmp/fakeftl/ftl/core/features.hpp
	@printf "running strict-POSIX fallback probe...\n"
	@$(CXX) $(filter-out -I., $(CXXFLAGS)) -I/tmp/fakeftl \
		scripts/portable_fallback_test.cpp -o /tmp/fakeftl/probe
	@/tmp/fakeftl/probe

clean:
	@rm -f $(LIB_TEST) $(UNIT_TEST) $(CLI_TEST) $(SMOKE_TEST)

test: selfcheck c-smoke

.PHONY: all test selfcheck project c-smoke portable clean
