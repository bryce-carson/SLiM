#-------------------------------------------------
#
# Project created by QtCreator 2020-04-17T14:17:13
#
#-------------------------------------------------

QT       -= core gui

TEMPLATE = lib
CONFIG += staticlib


# Uncomment this line for a production build, to build for both Intel and Apple Silicon.  This only works with Qt6;
# Qt5 for macOS is built for Intel only.  Uncomment this for all components or you will get link errors.
#QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64


# Uncomment the lines below to enable ASAN (Address Sanitizer), for debugging of memory issues, in every
# .pro file project-wide.  See https://clang.llvm.org/docs/AddressSanitizer.html for discussion of ASAN
# Also set the ASAN_OPTIONS env. variable, in the Run Settings section of the Project tab in Qt Creator, to
# strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1
# This also enables undefined behavior sanitizing, in conjunction with ASAN, because why not.
#CONFIG += sanitizer sanitize_address sanitize_undefined


CONFIG -= qt
CONFIG += c11
QMAKE_CFLAGS += -std=c11
QMAKE_CFLAGS_DEBUG += -g -Og -DDEBUG=1
QMAKE_CFLAGS_RELEASE += -O3

# get rid of spurious errors on Ubuntu, for now
linux-*: {
    QMAKE_CFLAGS += -Wno-unknown-pragmas -Wno-pragmas
}


# prevent link dependency cycles
QMAKE_LFLAGS += $$QMAKE_LFLAGS_NOUNDEF


SOURCES += \
    zutil.c \
    adler32.c \
    compress.c \
    crc32.c \
    deflate.c \
    gzlib.c \
    gzwrite.c \
    trees.c

HEADERS += \
    zutil.h \
    crc32.h \
    deflate.h \
    gzguts.h \
    trees.h \
    zconf.h \
    zlib.h

