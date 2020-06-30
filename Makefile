W        = -Wall
OPT      = -O2 -g
STD      = -std=c++17
CXXFLAGS = $(STD) $(OPT) $(W) -fPIC $(XCXXFLAGS)
INCS     = -Iinclude -Iexternal -Iexternal/hoytech-cpp -Iexternal/docopt.cpp

LDLIBS   = -l:liblmdb.a -pthread
LDFLAGS  = -flto $(XLDFLAGS)

CHECK_SRCS = check.cpp external/hoytech-cpp/hex.cpp
TOOL_SRCS  = quadb.cpp external/hoytech-cpp/hex.cpp


CHECK_OBJS := $(CHECK_SRCS:.cpp=.o)
TOOL_OBJS  := $(TOOL_SRCS:.cpp=.o)
DEPS       := $(CHECK_SRCS:.cpp=.d) $(TOOL_SRCS:.cpp=.d)


.PHONY: phony

all: phony verify-submodules quadb check

check: $(CHECK_OBJS) $(DEPS)
	$(CXX) $(CHECK_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

quadb: $(TOOL_OBJS) $(DEPS)
	$(CXX) $(TOOL_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

%.o: %.cpp %.d
	$(CXX) $(CXXFLAGS) $(INCS) -MMD -MP -MT $@ -MF $*.d -c $< -o $@

quadb.o: XCXXFLAGS += -DDOCOPT_HEADER_ONLY -DQUADRABLE_VERSION='"'`git describe --tags`'"'

-include *.d

%.d: ;

verify-submodules: phony | external/hoytech-cpp/README.md

external/hoytech-cpp/README.md:
	@echo
	@echo "*** SUBMODULES NOT CHECKED OUT ***"
	@echo "Run this command:"
	@echo "  git submodule update --init"
	@echo
	@false


clean: phony
	rm -rf quadb check *.o external/hoytech-cpp/hex.o *.d testdb/ *.gcda *.gcno coverage.lcov coverage-report/

run-check: phony check
	mkdir -p testdb/
	rm -f testdb/*.mdb
	time ./check

test: XCXXFLAGS += -fsanitize=address
test: XLDFLAGS += -fsanitize=address
test: phony run-check


coverage: XCXXFLAGS += --coverage
coverage: XLDFLAGS += --coverage
coverage: phony clean run-check
	lcov --directory . --capture --output-file coverage.lcov
	mkdir -p coverage-report
	genhtml coverage.lcov --output-directory coverage-report
