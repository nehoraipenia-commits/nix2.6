CXX ?= c++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Icpp/include -Icpp/src

# Qt6 Frameworks & Headers configuration
QT_ROOT ?= /opt/homebrew
QT_INCLUDES = -F$(QT_ROOT)/lib \
              -I$(QT_ROOT)/lib/QtWidgets.framework/Headers \
              -I$(QT_ROOT)/lib/QtGui.framework/Headers \
              -I$(QT_ROOT)/lib/QtCore.framework/Headers \
              -I$(QT_ROOT)/include
QT_LDFLAGS = -F$(QT_ROOT)/lib \
             -framework QtWidgets \
             -framework QtGui \
             -framework QtCore \
             -Wl,-rpath,$(QT_ROOT)/lib

LIB_DIR = lib
STATIC_QT_LIB = $(LIB_DIR)/libnix_qt.a
SHARED_QT_LIB = $(LIB_DIR)/libnix_qt.dylib
QT_OBJ = $(LIB_DIR)/nix_qt.o

SRCS = cpp/src/main.cpp
HEADERS = cpp/include/nix_runtime.hpp cpp/include/nix_qt.hpp cpp/src/frontend.hpp cpp/src/interpreter.hpp cpp/src/compiler.hpp
TARGET = nix

all: $(STATIC_QT_LIB) $(SHARED_QT_LIB) $(TARGET)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(QT_OBJ): cpp/src/nix_qt.cpp cpp/include/nix_qt.hpp cpp/include/nix_runtime.hpp | $(LIB_DIR)
	$(CXX) $(CXXFLAGS) -fPIC $(QT_INCLUDES) -c cpp/src/nix_qt.cpp -o $(QT_OBJ)

$(STATIC_QT_LIB): $(QT_OBJ)
	ar rcs $(STATIC_QT_LIB) $(QT_OBJ)

$(SHARED_QT_LIB): $(QT_OBJ)
	$(CXX) -dynamiclib $(QT_OBJ) $(QT_LDFLAGS) -o $(SHARED_QT_LIB)

$(TARGET): $(SRCS) $(HEADERS) $(STATIC_QT_LIB)
	$(CXX) $(CXXFLAGS) $(SRCS) $(STATIC_QT_LIB) $(QT_LDFLAGS) -o $(TARGET)

clean:
	rm -rf $(TARGET) $(LIB_DIR) nix_bin test_suite_exec test_suite_exec.cpp bench_fib_bin

.PHONY: all clean
