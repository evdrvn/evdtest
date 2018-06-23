SILENCE = @

#--- Inputs ----#
COMPONENT_NAME = evdtest
CPPUTEST_HOME = ../ext/evdsptc/test/ext/cpputest

CPPUTEST_USE_EXTENSIONS = Y
CPPUTEST_USE_MEM_LEAK_DETECTION = Y
CPPUTEST_USE_GCOV = Y

CPP_PLATFORM = Gcc

SRC_DIRS = \
	../src

TEST_SRC_DIRS = \
	./src

INCLUDE_DIRS =\
  ../src\
  ../ext/evdsptc/src\
  ../ext/lua-5.2.4/src\
  $(CPPUTEST_HOME)/include\

SWIG_DIR = ../ext/swig-3.0.12
SWIG_FILES_PREFIX = ../src/evdtestc
SWIG_FILES:=$(wildcard $(SWIG_FILES_PREFIX).*)
SWIG_OUTPUT = ./src/evdtestc_wrap.o

.PHONY: swig_all
swig_all: swig all

.PHONY: swig 
swig: $(SWIG_OUTPUT)
	
$(SWIG_OUTPUT): $(SWIG_FILES) 
	$(SWIG_DIR)/swig -lua $(SWIG_FILES_PREFIX).i
	gcc -I../src -I../ext/evdsptc/src -I../ext/lua-5.2.4/src -I$(SWIG_DIR)/Source/Swig -c $(SWIG_FILES_PREFIX)_wrap.c -o $(SWIG_OUTPUT)

CPPUTEST_CXXFLAGS+= -std=gnu++0x
CPPUTEST_CFLAGS  += -std=gnu99 -DEVDTESTRACE
CPPUTEST_LDFLAGS += -L../ext/evdsptc/test/lib -L../ext/lua-5.2.4/src -lpthread -lrt -levdsptc -llua -lm -ldl
CPPUTEST_PEDANTIC_ERRORS = N

include $(CPPUTEST_HOME)/build/MakefileWorker.mk
