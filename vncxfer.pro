lessThan(QT_MAJOR_VERSION, 5) : error("You need at least Qt 5.5 to build CEmu!")
lessThan(QT_MINOR_VERSION, 5) : error("You need at least Qt 5.5 to build CEmu!")

QT += core gui widgets multimedia

isEmpty(TARGET_NAME) {
    TARGET_NAME = vncxfer
}
TARGET = $$TARGET_NAME
TEMPLATE = app

CONFIG += c++11

CONFIG(release, debug|release) {
    #This is a release build
    DEFINES += QT_NO_DEBUG_OUTPUT
} else {
    #This is a debug build
    GLOBAL_FLAGS += -g3
}

# GCC/clang flags
if (!win32-msvc*) {
    GLOBAL_FLAGS    += -O3 -W -Wall -Wextra -Wunused-function -Werror=write-strings -Werror=redundant-decls -Werror=format -Werror=format-security -Werror=declaration-after-statement -Werror=implicit-function-declaration -Werror=return-type -Werror=pointer-arith -Winit-self
    GLOBAL_FLAGS    += -ffunction-sections -fdata-sections -fno-strict-overflow -fomit-frame-pointer
    QMAKE_CFLAGS    += -std=gnu11
} else {
    # TODO: add equivalent flags
    # Example for -Werror=shadow: /weC4456 /weC4457 /weC4458 /weC4459
    #     Source: https://connect.microsoft.com/VisualStudio/feedback/details/1355600/
    # /wd5045: disable C5045
    #          (new warning that causes errors: "Compiler will insert Spectre mitigation
    #          for memory load if /Qspectre switch specified")
    QMAKE_CXXFLAGS  += /Wall /wd5045

    # Add -MP to enable speedier builds
    QMAKE_CXXFLAGS += /MP
}

macx:  QMAKE_LFLAGS += -Wl,-dead_strip
linux: QMAKE_LFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--gc-sections -pie

QMAKE_CFLAGS    += $$GLOBAL_FLAGS
QMAKE_CXXFLAGS  += $$GLOBAL_FLAGS
QMAKE_LFLAGS    += $$GLOBAL_FLAGS

SOURCES += \
    main.cpp \
    vnc.c

HEADERS  += \
    rfbproto.h \
    vnc.h
