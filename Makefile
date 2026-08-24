CXX = c++
ROOT = ..
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -I$(ROOT)/includes -I.
LDFLAGS =
LDLIBS = -lreadline
DEBUG_LDFLAGS = -fsanitize=address,undefined

LIB_TEST = test_library
UNIT_TEST = test_unit
CLI_TEST = test_cli
PROJECT_OBJS = $(shell find $(ROOT)/obj -name '*.o' ! -name 'main.o' 2>/dev/null)

HEADERS = ftest.hpp framework.hpp capture.hpp files.hpp fdtrack.hpp alloc.hpp process.hpp

all: selfcheck

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

clean:
	@rm -f $(LIB_TEST) $(UNIT_TEST) $(CLI_TEST)

.PHONY: all selfcheck project clean
