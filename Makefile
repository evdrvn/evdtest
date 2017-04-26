COMPILER  = gcc
CFLAGS    = -g -O2 -MMD -MP -Wall -Wextra -std=gnu99
LIBS      =
TARGET    = ./src/libevdtest.a
SRCDIR    = ./src
EXTDIR    = ./ext
ifeq "$(strip $(SRCDIR))" ""
  SRCDIR  = .
endif
INCLUDE   = -I$(SRCDIR) -I$(EXTDIR)/evdsptc/src -I$(EXTDIR)/lua-5.2.4/src

SOURCES   = $(wildcard $(SRCDIR)/*.c)
OBJDIR    = ./src
ifeq "$(strip $(OBJDIR))" ""
  OBJDIR  = .
endif
OBJECTS   = $(addprefix $(OBJDIR)/, $(notdir $(SOURCES:.c=.o)))
DEPENDS   = $(OBJECTS:.o=.d)

SWIG_DIR = $(EXTDIR)/swig-3.0.12
SWIG_FILES_PREFIX = $(SRCDIR)/evdtestc
SWIG_FILES:=$(wildcard $(SWIG_FILES_PREFIX).*)
SWIG_OUTPUT = $(SRCDIR)/evdtestc_wrap.o

.PHONY: all
all: $(TARGET)

.PHONY: swig 
swig: $(SWIG_OUTPUT) all
	
$(SWIG_OUTPUT): $(SWIG_FILES) 
	$(SWIG_DIR)/swig -lua $(SWIG_FILES_PREFIX).i
	gcc $(INCLUDE) -I$(SWIG_DIR)/Source/Swig -c $(SWIG_FILES_PREFIX)_wrap.c -o $(SWIG_OUTPUT)

$(TARGET): $(OBJECTS) $(LIBS)
	ar rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	-mkdir -p $(OBJDIR)
	$(COMPILER) $(CFLAGS) $(INCLUDE) -o $@ -c $<

clean:
	-rm -f $(OBJECTS) $(DEPENDS) $(TARGET)

-include $(DEPENDS)
