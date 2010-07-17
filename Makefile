# =========================================================
# Makefile for NFORenum
#
#	Don't put any local configuration in here
#	Change Makefile.local instead, it'll be
#	preserved when updating the sources
# =========================================================

MAKEFILELOCAL=Makefile.local

# Gnu compiler settings
SHELL = /bin/sh
CC = g++
CXX = g++
STRIP = strip
UPX = upx
AWK = awk

# OS detection: Cygwin vs Linux
ISCYGWIN = $(shell [ ! -d /cygdrive/ ]; echo $$?)
ISMINGW = $(shell [ `$(CC) -dumpmachine` != mingw32 ]; echo $$?)

# OS dependent variables
NFORENUM = $(shell [ \( $(ISCYGWIN) -eq 1 \) -o \( $(ISMINGW) -eq 1 \) ] && echo renum.exe || echo renum)

# use 386 instructions but optimize for pentium II/III
ifeq ($(ISCYGWIN),1)
CFLAGS = -g -O1 -I $(BOOST_INCLUDE) -Wall -Wno-uninitialized $(CFLAGAPP)
else
CFLAGS = -g -O1 -idirafter$(BOOST_INCLUDE) -Wall -Wno-uninitialized $(CFLAGAPP)
endif

ifeq ($(shell uname),Darwin)
CFLAGS += -isystem/opt/local/include
endif

CXXFLAGS = $(CFLAGS)

-include ${MAKEFILELOCAL}

ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
CXXFLAGS += -DDEBUG
endif

# Somewhat automatic detection of the correct boost include folder
ifndef BOOST_INCLUDE
BOOST_INCLUDE=$(shell \
find /usr/include /usr/local/include /opt/local/include -maxdepth 1 -name 'boost-*' 2> /dev/null | sort -t - -k 2 | tail -n 1 )
ifeq ($(BOOST_INCLUDE),)
BOOST_INCLUDE=$(shell \
( [ -d /usr/include/boost/date_time ] && echo /usr/include ) || \
( [ -d /usr/local/include/boost/date_time ] && echo /usr/local/include ) || \
( [ -d /opt/local/include/boost/date_time ] && echo /opt/local/include ) )
endif
endif

ifeq ($(BOOST_INCLUDE),)
BOOST_ERROR = echo Error: Boost not found. Compilation will fail.
endif

ifndef V
V=0 # verbose build default off
endif

# =======================================================================
#           setup verbose/non-verbose make process
# =======================================================================

# _E = prefix for the echo [TYPE] TARGET
# _C = prefix for the actual command(s)
# _I = indentation for sub-make
# _Q = number of 'q's for UPX
# _S = sub-makes silent?
ifeq (${V},1)
	# verbose, set _C = nothing (print command), _E = comment (don't echo)
	_C=
	_E=@\#
	_Q=-qq
	_S=
else
	# not verbose, _C = @ (suppress cmd line), _E = @echo (echo type&target)
	_C=@
	_E:=@echo ${_I}
	_Q=-qqq
	_S=-s
endif

# increase indentation level for sub-makes
_I := ${_I}"	"
export _I

# standard compilation commands should be of the form
# target:	prerequisites
#	${_E} [CMD] $@
#	${_C}${CMD} ...arguments...
#
# non-standard commands (those not invoked by make all/dos/win) should
# use the regular syntax (without the ${_E} line and without the ${_C} prefix)
# because they'll be only used for testing special conditions
#
# =======================================================================

# sources to be compiled and linked
NFORENUMSRC=IDs.cpp act0.cpp act123.cpp act123_classes.cpp act5.cpp act6.cpp \
  act79D.cpp actB.cpp actF.cpp command.cpp data.cpp globals.cpp inject.cpp \
  messages.cpp pseudo.cpp rangedint.cpp renum.cpp sanity.cpp strings.cpp \
  utf8.cpp getopt.cpp help.cpp message_mgr.cpp language_mgr.cpp \
  mapescapes.cpp pseudo_seq.cpp

ifndef NOREV
NOREV = 0
endif

ifndef NO_BOOST
NO_BOOST = 0
endif

ifneq ($(NO_BOOST),0)
BOOST_WARN = echo Warning: NO_BOOST is no longer obeyed.
endif

# targets
all: $(NFORENUM)
remake:
	$(_E) [CLEAN]
	$(_C)$(MAKE) ${_S} clean
	$(_E) [REBUILD]
	$(_C)$(MAKE) ${_S} all


${MAKEFILELOCAL}:
	@/bin/sh -c "export PATH=\"/bin\" && \
        echo ${MAKEFILELOCAL} did not exist, using defaults. Please edit it if compilation fails. && \
        cp ${MAKEFILELOCAL}.sample $@"

$(NFORENUM): $(NFORENUMSRC:%.cpp=%.o)
	$(_E) [LD] $@
	$(_C)$(CXX) -o $@ $(CFLAGS) $^ $(LDOPT)


clean:
	rm -rf *.o *.d *.exe *.EXE renum bundle bundles
	rm -f version.h

release: FORCE
	$(_E)[REBUILD] $(NFORENUM)
	$(_C)rm -f $(NFORENUM)
	$(_C)$(MAKE) $(_S)
	$(_E) [STRIP] $(NFORENUM)
	$(_C)$(STRIP) $(NFORENUM)
ifneq ($(UPX),)
	$(_E) [UPX] $(@:%_r=%)
	$(_C)$(UPX) $(_Q) --best  $(@:%_r=%)
endif

FORCE:
	@$(BOOST_WARN)
	@$(BOOST_ERROR)

include version.def

version.h: FORCE
	@echo // Autogenerated by make.  Do not edit.  Edit version.def or the Makefile instead. > $@.tmp
	@echo "#define VERSION \"v$(VERSIONSTR)\"" >> $@.tmp
	@echo "#define YEARS \"2004-$(YEAR)\"" >> $@.tmp
	@(diff $@.tmp $@ > /dev/null 2>&1 && rm -f $@.tmp) || (rm -f $@ ; mv $@.tmp $@)


# Gnu compiler rules

%.o : %.c
	$(_E) [CC] $@
	$(_C)$(CC) -c -o $@ $(CFLAGS) -MMD -MF $@.d -MT $@ $<

%.o : %.cpp
	$(_E) [CPP] $@
	$(_C)$(CXX) -c -o $@ $(CXXFLAGS) -MMD -MF $@.d -MT $@ $<

# On some installations a version.h exists in /usr/include. This one is then
# found by the dependency tracker and thus the dependencies do not contain
# a reference to version.h, so it isn't generated and compilation fails.
%.o.d: version.h
	$(_E) [CPP DEP] $@
	$(_C)$(CC) $(CFLAGS) -DMAKEDEP -MM -MG $*.c* -MF $@

ifndef NO_MAKEFILE_DEP
-include $(NFORENUMSRC:.cpp=.o.d)
endif

include Makefile.bundle
