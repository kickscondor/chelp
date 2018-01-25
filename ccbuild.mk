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
SDK_VERSION =

ifeq ($(PLATFORM), android)
	ARCH ?= arm
	ARCHBIN = $(ARCH)-linux-android
	ARCHDIR = $(ARCH)
	ARCHABI = $(ARCH)
	ifeq ($(ARCH), x86)
		ARCHBIN = i686-linux-android
	else ifeq ($(ARCH), arm)
		ARCHDIR = arm-linux-androideabi
		ARCHBIN = arm-linux-androideabi
		ARCHABI = armeabi
	else ifeq ($(ARCH), arm64)
		ARCHDIR = aarch64-linux-android
		ARCHBIN = aarch64-linux-android
		ARCHABI = arm64-v8a
	endif
	TOOLCHAIN ?= "$(ANDROIDNDK)/toolchains/$(ARCHDIR)-4.9/prebuilt/darwin-x86_64/bin/$(ARCHBIN)-"
	NDK = "$(ANDROIDNDK)/platforms/android-21/arch-$(ARCH)"
  SDK_VERSION = $(shell $(ECHO) "$(ANDROIDNDK)" | grep -Eow "r[0-9]+")
endif

ifeq ($(PLATFORM), linux)
	ARCH ?= x86_64
endif

ifeq ($(PLATFORM), mac)
	ARCH ?= x86_64
	SDK_VERSION = $(shell otool -l /System/Library/Frameworks/Foundation.framework/Foundation | grep sdk)
endif

ifeq ($(PLATFORM), windows)
	ARCH ?= x86_64
	TOOLCHAIN ?= "$(MINGW64ROOT)/bin/$(ARCH)-w64-mingw32-"
endif

ifeq ($(PLATFORM), emscripten)
	AR = emar
	CC = emcc
	ARCH ?= wasm
	ifeq ($(ARCH), wasm)
		LIBS = -s WASM=1
	endif
	OUTBIN ?= $(NAME).js
	OUTBINGLOB = $(NAME).*
	STRIP = echo
endif

TOOLCHAIN ?=
AR ?= $(TOOLCHAIN)ar
CC ?= $(TOOLCHAIN)gcc
CACHESIZE ?= 16384
DEBUG ?= 0
ECHO = /bin/echo
MAKE_S = $(MAKE) --no-print-directory -s
MEMCHECK ?= 0
OPENGL ?= 0
STRIP ?= $(TOOLCHAIN)strip -x

HOST := $(shell $(CC) -v 2>&1 | \
  sed -e "/Target:/b" -e "/--target=/b" -e d | \
	sed "s/.* --target=//; s/Target: //; s/ .*//" | head -1)
ARCH ?= $(shell $(ECHO) "$(HOST)" | sed "s/-.*//")
PLATFORM ?= $(shell $(ECHO) "$(HOST)" | \
	sed "s/^x86_64-apple-darwin.*/mac/; s/^x86_64-.*linux-gnu.*/linux/; s/^.*-w64-mingw.*/windows/; s/^.*-linux-android.*/android/;")
TARGET = $(PLATFORM)-$(ARCH)
ifeq ($(VERSION), )
	VERSION := $(shell cat include/$(NAME).h | \
		sed '/$(DEFPREFIX)_VERSION/!d' | grep -Eow "[0-9]+\\.[0-9]+")
endif
MAJORVER := $(shell $(ECHO) "$(VERSION)" | sed "s/\\.[0-9]*//")
MINORVER := $(shell $(ECHO) "$(VERSION)" | sed "s/^[0-9]*\\.//")
DATE := $(shell date +%Y-%m-%d)
REVISION := $(shell git rev-list HEAD | wc -l | sed "s/ //g")
COMMIT := $(shell git rev-list HEAD -1 --abbrev=7 --abbrev-commit)
RELEASE ?= $(VERSION).$(REVISION)
OUTDIR ?= build/$(TARGET)

CFLAGS = -std=gnu99 -Wall -Wformat
LDFLAGS = -L.
LIBS ?= -lm

VALGRIND = valgrind --tool=memcheck --leak-check=full --show-reachable=yes --num-callers=20 --track-fds=yes
MEMCHECK_CMD := $(shell $(ECHO) "$(MEMCHECK)" | sed "s/0//; s/1/$(VALGRIND)/")

ifeq ($(PLATFORM), android)
	CFLAGS += --sysroot=$(NDK)
endif

ifeq ($(PLATFORM), windows)
	CFLAGS += -I$(TOOLCHAIN)x86_64-w64-mingw32/include
	OUTBIN ?= $(NAME).exe
endif

ifeq ($(OPENGL), 1)
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
	SDK_VERSION = glfw3-$(shell pkg-config --modversion glfw3) \
						    pangoft2-$(shell pkg-config --modversion pangoft2) \
						    $(shell curl-config --version)
endif
endif

ifeq ($(PLATFORM), ios)
	ifeq ($(DEBUG), 1)
		export XCODECONFIG = Debug
	else
		export XCODECONFIG = Release
	endif

	ifeq ($(IOS_PLATFORM),)
		OUTLIB = lib$(NAME)-ios.a
		ARCH = universal
	else
		XCODE = $(shell xcode-select --print-path)
		IOS_DEV = ${XCODE}/Platforms/${IOS_PLATFORM}.platform/Developer
		IOS_SDK = ${IOS_DEV}/SDKs/$(shell ls ${IOS_DEV}/SDKs | sort -r | head -n1)
		CFLAGS += -isysroot ${IOS_SDK} -I${IOS_SDK}/usr/include -arch ${ARCH} \
			-fembed-bitcode -miphoneos-version-min=8.0
		SDK_VERSION = $(shell $(ECHO) "$(IOS_SDK)" | grep -Eow "iPhoneOS[0-9]+\\.[0-9]+.sdk")
	endif
endif

ifeq ($(DEBUG), 1)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -Os
endif

OUTLIB ?= lib$(NAME).a
OUTBIN ?= $(NAME)
OUTBINGLOB ?= $(OUTBIN)
PKG := "$(NAME)-$(RELEASE)"
DEFPREFIX = $(shell echo $(NAME) | tr a-z A-Z)
LOCKHASH = { \
	"$(CC)" => "$(shell $(CC) --version 2>&1 | head -1)", \
	"sdk" => "$(SDK_VERSION)" \
}

default: all

list-targets:
	@$(ECHO) "PLATFORM: android (Set ANDROIDNDK path in ~/.ccbuild)"
	@$(ECHO) "  ARCH (default): arm"
	@$(ECHO) "  ARCH: arm64"
	@$(ECHO) "  ARCH: x86"
	@$(ECHO) "  ARCH: x86_64"
	@$(ECHO) "PLATFORM: emscripten (Web platforms)"
	@$(ECHO) "  ARCH (default): wasm (WebAssembly)"
	@$(ECHO) "  ARCH: asmjs (Asm.js)"
	@$(ECHO) "PLATFORM: ios"
	@$(ECHO) "  ARCH (default): universal"
	@$(ECHO) "  ARCH: armv7"
	@$(ECHO) "  ARCH: armv7s"
	@$(ECHO) "  ARCH: arm64"
	@$(ECHO) "  ARCH: i386"
	@$(ECHO) "  ARCH: x86_64"
	@$(ECHO) "PLATFORM: linux"
	@$(ECHO) "  ARCH: x86_64"
	@$(ECHO) "PLATFORM: mac"
	@$(ECHO) "  ARCH: x86_64"
	@$(ECHO) "PLATFORM: windows"
	@$(ECHO) "  ARCH: x86_64"

config:
	@$(ECHO) "#define $(DEFPREFIX)_CC        \"$(CC)\""
	@$(ECHO) "#define $(DEFPREFIX)_CFLAGS    \"$(CFLAGS)\""
	@$(ECHO) "#define $(DEFPREFIX)_DEBUG     $(DEBUG)"
	@$(ECHO) "#define $(DEFPREFIX)_MAKE      \"$(MAKE)\""
	@$(ECHO) "#define $(DEFPREFIX)_PREFIX    \"$(PREFIX)\""
	@$(ECHO) "#define $(DEFPREFIX)_LIB       \"$(LIB)\""
	@$(ECHO) "#define $(DEFPREFIX)_BIN       \"$(BIN)\""
	@$(ECHO)
	@$(ECHO) "#define $(DEFPREFIX)_HOST      \"$(HOST)\""
	@$(ECHO) "#define $(DEFPREFIX)_PLATFORM  \"$(PLATFORM)\""
	@$(ECHO) "#define __platform_$(PLATFORM)__"
	@$(ECHO) "#define $(DEFPREFIX)_ARCH      \"$(ARCH)\""
	@$(ECHO) "#define __arch_$(ARCH)__"
	@$(ECHO) "#define $(DEFPREFIX)_TARGET    \"$(TARGET)\""
	@$(ECHO) "#define $(DEFPREFIX)_ARCH_ID   $(DEFPREFIX)_$(ARCH)"
	@$(ECHO) "#define $(DEFPREFIX)_CACHESIZE $(CACHESIZE)"

version:
	@$(ECHO) "#define $(DEFPREFIX)_VERSION_MAJOR $(MAJORVER)"
	@$(ECHO) "#define $(DEFPREFIX)_VERSION_MINOR $(MINORVER)"
	@$(ECHO) "#define $(DEFPREFIX)_REVISION      $(REVISION)"
	@$(ECHO) "#define $(DEFPREFIX)_RELEASE       \"$(RELEASE)\""
	@$(ECHO) "#define $(DEFPREFIX)_DATE          \"$(DATE)\""
	@$(ECHO) "#define $(DEFPREFIX)_COMMIT        \"$(COMMIT)\""

lockhash:
	@ruby -ryaml -e 'puts({"$(TARGET)" => $(LOCKHASH)}.to_yaml)'

lockfile:
	@ruby -ryaml -e 'l = YAML.load_file("Makefile.lock") rescue {}; \
		l.merge!({"$(TARGET)" => $(LOCKHASH)}); \
		File.open("Makefile.lock", "w") { |f| f << l.to_yaml }'

check-env:
	@ruby -ryaml -e 'needs = YAML.load_file("Makefile.lock")["$(TARGET)"] rescue nil; \
		abort "** No $(TARGET) found in Makefile.lock." unless needs; \
		has = $(LOCKHASH); \
		if needs.to_s != has.to_s; puts "** Environment has changed."; \
		  puts "  -> Original environment: #{needs}"; \
			abort "  -> Current environment: #{has}"; end'

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
	$(AR) rcs $@ $(OBJ) $(OBJ_LIB)

$(OUTDIR)/bin/$(OUTBIN): objects
	@$(ECHO) LINK $(NAME)
	$(CC) $(CFLAGS) $(OBJ_BIN) $(OBJ) $(LDFLAGS) $(LIBS) -o $(OUTBIN)
	@cp $(OUTBINGLOB) $(OUTDIR)/bin
	@if [ "$(DEBUG)" != "1" ]; then \
		$(ECHO) STRIP $(NAME); \
		$(STRIP) $(OUTDIR)/bin/$(OUTBIN); \
	fi

$(OUTDIR)/lib/lib$(NAME)-ios.a: setup
	@mkdir -p $(OUTDIR)/lib
	${MAKE} PLATFORM=ios ARCH=armv7  IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios ARCH=armv7s IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios ARCH=arm64  IOS_PLATFORM=iPhoneOS
	${MAKE} PLATFORM=ios ARCH=x86_64 IOS_PLATFORM=iPhoneSimulator
	${MAKE} PLATFORM=ios ARCH=i386   IOS_PLATFORM=iPhoneSimulator
	lipo -create \
		-arch armv7  build/ios-armv7/lib/lib$(NAME).a \
		-arch armv7s build/ios-armv7s/lib/lib$(NAME).a \
		-arch arm64  build/ios-arm64/lib/lib$(NAME).a \
		-arch x86_64 build/ios-x86_64/lib/lib$(NAME).a \
		-arch i386   build/ios-i386/lib/lib$(NAME).a \
		-output      $@
	mv $@ $(OUTDIR)/lib/lib$(NAME).a
	@$(ECHO) COPY include
	@cp -r include/* $(OUTDIR)/include
	@if [[ -f platforms/$(PLATFORM)/include ]]; then \
		$(ECHO) COPY platforms/$(PLATFORM)/include; \
		cp -r platforms/$(PLATFORM)/include/* $(OUTDIR)/include; \
	fi

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
