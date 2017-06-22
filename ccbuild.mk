#
# ccbuild.mk
#
# The goal here is to allow native and some degree of cross-compilation with a
# simple Makefile. The odd platform here is clearly iOS - the binary is a combination
# of multiple architectures and currently you can only build all of them from Mac OS X.
#
# I use this Makefile on several of my projects - it may become a submodule at some
# point.
#
# ** BASICS **
#
# Simply define NAME at the top of your Makefile and include 'ccbuild.mk'.
#
#   NAME=demo
#   include ccbuild.mk
#
# After this, be sure to set OBJ to the paths of any common .o files you will be using.
# OBJ_LIB can keep any object files to link into the static library and OBJ_BIN for the
# executable binary.
#
#   SRC := $(wildcard source/*.c)
#   OBJ := $(patsubst %.c,$(OUTDIR)/%.o,$(SRC))
#
# OUTDIR is a variable setup by ccbuild.mk which will output to a build directory
# specific to the architecture being targetted.
#
# You will also need to provide your own Makefile tasks for 'setup' and 'objects'.
# Usually, 'objects' is as simple as checking that $(OBJ) is there.
#
#   objects: $(OBJ) $(OBJ_LIB) $(OBJ_BIN)
#
# The 'setup' task will create the needed build directory structure. (And do anything
# else that needs to be done prior to build.)
#
#   setup: setup-dirs include/demo.h
#   
#   setup-dirs:
#   	@mkdir -p $(OUTDIR)/include
#   	@mkdir -p $(OUTDIR)/source
#   	@mkdir -p $(OUTDIR)/lib
#
# This is all the documentation for now. This Makefile is quite brief - individual variables
# and build tasks can be found below.
#
# LICENSE
#
#   This software is dual-licensed to the public domain and under the following
#   license: you are granted a perpetual, irrevocable license to copy, modify,
#   publish, and distribute this file as you see fit.

.SUFFIXES: .g .c .o

SHELL := /bin/bash

include ~/.ccbuild

PREFIX = /usr/local
LIB = $(PREFIX)/lib/$(NAME)
BIN = $(PREFIX)/bin

ifeq ($(PLATFORM), android)
	TOOLCHAIN ?= "$(ANDROIDNDK)/toolchains/$(ARCH)-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/$(ARCH)-linux-androideabi-"
	NDK = "$(ANDROIDNDK)/platforms/android-17/arch-$(ARCH)"
endif

ifeq ($(PLATFORM), windows)
	TOOLCHAIN ?= "$(MINGW64ROOT)/bin/$(ARCH)-w64-mingw32-"
endif

TOOLCHAIN ?=
AR = $(TOOLCHAIN)ar
CC = $(TOOLCHAIN)gcc
CACHESIZE ?= 16384
DEBUG ?= 0
ECHO = /bin/echo
MAKE_S = $(MAKE) --no-print-directory -s
MEMCHECK ?= 0
STRIP ?= $(TOOLCHAIN)strip -x

HOST := $(shell $(CC) -v 2>&1 | \
  sed -e "/Target:/b" -e "/--target=/b" -e d | \
	sed "s/.* --target=//; s/Target: //; s/ .*//" | head -1)
ARCH ?= $(shell $(ECHO) "$(HOST)" | sed "s/-.*//")
PLATFORM ?= $(shell $(ECHO) "$(HOST)" | \
	sed "s/^x86_64-apple-darwin.*/mac/; s/^x86_64-.*linux-gnu.*/linux/; s/^.*-w64-mingw.*/windows/; s/^.*-linux-android.*/android/;")
TARGET = $(PLATFORM)-$(ARCH)
VERSION := $(shell cat include/$(NAME).h | \
	sed '/$(DEFPREFIX)_VERSION/!d' | grep -Eow "[0-9]+\\.[0-9]+")
MAJORVER := $(shell $(ECHO) "$(VERSION)" | sed "s/\\.[0-9]*//")
MINORVER := $(shell $(ECHO) "$(VERSION)" | sed "s/^[0-9]*\\.//")
DATE := $(shell date +%Y-%m-%d)
REVISION := $(shell git rev-list HEAD | wc -l | sed "s/ //g")
COMMIT := $(shell git rev-list HEAD -1 --abbrev=7 --abbrev-commit)
RELEASE ?= $(VERSION).$(REVISION)
OUTDIR ?= build/$(TARGET)

CFLAGS = -std=c99 -Wall -Wformat
LDFLAGS = -L.
LIBS = -lm

VALGRIND = valgrind --tool=memcheck --leak-check=full --show-reachable=yes --num-callers=20 --track-fds=yes
MEMCHECK_CMD := $(shell $(ECHO) "$(MEMCHECK)" | sed "s/0//; s/1/$(VALGRIND)/")

ifeq ($(PLATFORM), android)
	CFLAGS += -I$(NDK)/usr/include
endif

ifeq ($(PLATFORM), windows)
	CFLAGS += -I$(TOOLCHAIN)x86_64-w64-mingw32/include
endif

ifeq ($(PLATFORM), mac)
	LIBS += -framework OpenGL -framework Foundation -framework CoreText -framework CoreGraphics -lglfw
endif

ifeq ($(PLATFORM), linux)
	CFLAGS += -D_GNU_SOURCE \
	          `pkg-config --cflags glfw3` \
						`pkg-config --cflags pangoft2` \
						`curl-config --cflags`
	LDFLAGS += `pkg-config --libs glfw3` \
						 `pkg-config --libs pangoft2` \
						 `curl-config --libs`
	LIBS += -lGL -lGLU -lGLEW
endif

ifeq ($(PLATFORM), ios6)
	ifeq ($(IOS_PLATFORM),)
		OUTLIB = $(OUTDIR)/lib/lib$(NAME)-ios6.a
		ARCH = universal
	else
		XCODE = $(shell xcode-select --print-path)
		IOS_DEV = ${XCODE}/Platforms/${IOS_PLATFORM}.platform/Developer
		IOS_SDK = ${IOS_DEV}/SDKs/$(shell ls ${IOS_DEV}/SDKs | sort -r | head -n1)
		CFLAGS += -isysroot ${IOS_SDK} -I${IOS_SDK}/usr/include -arch ${ARCH} \
			-fembed-bitcode -miphoneos-version-min=6.0
	endif
endif

ifeq ($(DEBUG), 1)
	CFLAGS += -g -D_DEBUG
else
	CFLAGS += -Os
endif

OUTLIB ?= $(OUTDIR)/lib/lib$(NAME).a
OUTBIN ?= $(OUTDIR)/bin/$(NAME)
PKG := "$(NAME)-$(RELEASE)"
DEFPREFIX = $(shell echo $(NAME) | tr a-z A-Z)

default: all

config:
	@$(ECHO) "#define $(DEFPREFIX)_CC       \"$(CC)\""
	@$(ECHO) "#define $(DEFPREFIX)_CFLAGS   \"$(CFLAGS)\""
	@$(ECHO) "#define $(DEFPREFIX)_DEBUG    $(DEBUG)"
	@$(ECHO) "#define $(DEFPREFIX)_MAKE     \"$(MAKE)\""
	@$(ECHO) "#define $(DEFPREFIX)_PREFIX   \"$(PREFIX)\""
	@$(ECHO) "#define $(DEFPREFIX)_LIB      \"$(LIB)\""
	@$(ECHO) "#define $(DEFPREFIX)_BIN      \"$(BIN)\""
	@$(ECHO)
	@$(ECHO) "#define $(DEFPREFIX)_HOST     \"$(HOST)\""
	@$(ECHO) "#define $(DEFPREFIX)_PLATFORM \"$(PLATFORM)\""
	@$(ECHO) "#define $(DEFPREFIX)_ARCH     \"$(ARCH)\""
	@$(ECHO) "#define $(DEFPREFIX)_TARGET   \"$(TARGET)\""
	@$(ECHO) "#define $(DEFPREFIX)_CACHESIZE $(CACHESIZE)"

version:
	@$(ECHO) "#define $(DEFPREFIX)_VERSION_MAJOR $(MAJORVER)"
	@$(ECHO) "#define $(DEFPREFIX)_VERSION_MINOR $(MINORVER)"
	@$(ECHO) "#define $(DEFPREFIX)_REVISION      $(REVISION)"
	@$(ECHO) "#define $(DEFPREFIX)_RELEASE       \"$(RELEASE)\""
	@$(ECHO) "#define $(DEFPREFIX)_DATE          \"$(DATE)\""
	@$(ECHO) "#define $(DEFPREFIX)_COMMIT        \"$(COMMIT)\""

$(OUTDIR)/%.o: %.m setup
	@$(ECHO) CC $<
	@$(CC) -c $(CFLAGS) $(INCS) -fobjc-arc -o $@ $<

$(OUTDIR)/%.o: %.c setup
	@$(ECHO) CC $<
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

$(OUTDIR)/%.o: $(OUTDIR)/%.c setup
	@$(ECHO) CC $<
	$(CC) -c $(CFLAGS) $(INCS) -o $@ $<

$(OUTDIR)/lib/lib$(NAME).a: objects
	@$(ECHO) LINK lib$(NAME)
	$(AR) rcs $@ $(OBJ)

$(OUTDIR)/bin/$(NAME): objects
	@$(ECHO) LINK $(NAME)
	@$(CC) $(CFLAGS) $(OBJ_BIN) $(OBJ) $(LIBS) -o $(NAME)
	@if [ "$(DEBUG)" != "1" ]; then \
		$(ECHO) STRIP $(NAME); \
	  $(STRIP) $(NAME); \
	fi

$(OUTDIR)/lib/lib$(NAME)-ios6.a: setup
	@mkdir -p $(OUTDIR)/lib
	${MAKE} PLATFORM=ios6 ARCH=armv7  IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios6 ARCH=armv7s IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios6 ARCH=arm64  IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios6 ARCH=x86_64 IOS_PLATFORM=iPhoneSimulator
	${MAKE} PLATFORM=ios6 ARCH=i386   IOS_PLATFORM=iPhoneSimulator
	lipo -create \
		-arch armv7  build/ios6-armv7/lib/lib$(NAME).a \
		-arch armv7s build/ios6-armv7s/lib/lib$(NAME).a \
		-arch arm64  build/ios6-arm64/lib/lib$(NAME).a \
		-arch x86_64 build/ios6-x86_64/lib/lib$(NAME).a \
		-arch i386   build/ios6-i386/lib/lib$(NAME).a \
		-output      $@
	mv $@ $(OUTDIR)/lib/lib$(NAME).a
	@$(ECHO) COPY include
	@cp -r include/* $(OUTDIR)/include
	@$(ECHO) COPY platforms/$(PLATFORM)/include
	@-cp -r platforms/$(PLATFORM)/include/* $(OUTDIR)/include

todo:
	@grep -rInso 'TODO: \(.\+\)' core include platforms test

cloc:
	@cloc core include platforms

tarball: include/$(NAME)/version.h core/syntax.c
	mkdir -p pkg
	rm -rf $(PKG)
	git checkout-index --prefix=$(PKG)/ -a
	rm -f $(PKG)/.gitignore
	cp include/$(NAME)/version.h $(PKG)/include/$(NAME)/
	cp core/syntax.c $(PKG)/core/
	tar czvf pkg/$(PKG).tar.gz $(PKG)
	rm -rf $(PKG)

clean:
	@$(ECHO) cleaning
	@rm -rf $(OUTDIR)

.PHONY: clean cloc config todo version
