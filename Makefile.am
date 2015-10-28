ACLOCAL_AMFLAGS = -I m4
AM_LDFLAGS = @COVERAGE_LDFLAGS@
AM_CPPFLAGS = -Ilapse_controller
AM_CXXFLAGS = -Ilapse_controller
LIBTOOL_DEPS = @LIBTOOL_DEPS@
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status libtool

# conditional test coverage
if ENABLE_COVERAGE
.PHONY: clean-coverage
clean-coverage:
	-find -name '*.gcda' -exec rm -rf {} \;
	-$(LCOV) --directory $(top_builddir) -z
	-rm -rf coverage.info coverage/

.PHONY: coverage-report
coverage-report: clean-coverage
	-$(MAKE) $(AM_MAKEFLAGS) -k check
	$(MAKE) $(AM_MAKEFLAGS) coverage/index.html

coverage.info:
	$(LCOV) --directory $(top_builddir) --base-directory $(top_builddir) --no-external --capture --output-file $@ --no-checksum --compat-libtool

coverage/index.html: coverage.info
	$(GENHTML) --prefix $(top_builddir) --output-directory $(@D) --title "Test Coverage" --legend --show-details $<


.PHONY: clean-gcno
clean-gcno:
	-find -name '*.gcno' -exec rm -rf {} \;

clean-local: clean-coverage clean-gcno
endif

#distributed executables
bin_PROGRAMS = lapse_controller
lapse_controller_SOURCES = src/lapse_controller.cpp
lapse_controller_CPPFLAGS = $(DEPS_CFLAGS)
lapse_controller_LDADD = $(DEPS_LIBS) liblapse_controller.la

# tests
check_PROGRAMS =
test_lapse_controller_SOURCES = test/lapse_controller.cpp
test_lapse_controller_CPPFLAGS = $(DEPS_CFLAGS)
test_lapse_controller_LDADD = $(DEPS_LIBS) liblapse_controller.la

TESTS = $(check_PROGRAMS)
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = sh

test: check
