COMPILER  = gcc
CFLAGS    = -g -O2 -MMD -MP -Wall -Wextra -std=gnu99
SRCDIR    = 
EXTDIR    = ../../ext
ifeq "$(shell getconf LONG_BIT)" "64"
LDFLAGS   = -lpthread
TLDFLAGS  = -rdynamic -L$(EXTDIR)/evdsptc/build -L$(EXTDIR)/lua-5.2.4/src -lpthread -lrt -lm -ldl -Wl,-Bstatic -levdsptc -llua -Wl,-Bdynamic
else
LDFLAGS   = -lpthread
TLDFLAGS  = -rdynamic -L$(EXTDIR)/evdsptc/build -L$(EXTDIR)/lua-5.2.4/src -lpthread -lrt -lm -ldl -Wl,-Bstatic -levdsptc -llua -Wl,-Bdynamic
endif
LIBS      =
TLIBS     = $(EXTDIR)/evdsptc/build/libevdsptc.a ../../src/libevdtest.a
TARGET    = ./minimal
TTARGET   = ./minimal_test
ifeq "$(strip $(SRCDIR))" ""
  SRCDIR  = .
endif
INCLUDE   = -I$(SRCDIR) -I$(EXTDIR)/evdsptc/src -I$(EXTDIR)/lua-5.2.4/src -I../../src

SOURCES   = $(wildcard $(SRCDIR)/*.c)
OBJDIR    = 
ifeq "$(strip $(OBJDIR))" ""
  OBJDIR  = .
endif
OBJECTS   = $(addprefix $(OBJDIR)/, $(notdir $(SOURCES:.c=.o)))
DEPENDS   = $(OBJECTS:.o=.d)

LUALIBDIR = $(shell pwd)/../lua_modules

.PHONY: all
all: $(TARGET)

.PHONY: test
test: override CFLAGS += -DTEST_MINIMAL
test: $(TTARGET)

$(TARGET): $(OBJECTS) $(LIBS)
	$(COMPILER) -o $@ $^ $(LDFLAGS)

$(TTARGET): $(OBJECTS) $(TLIBS)
	$(COMPILER) -o $@ $^ $(TLDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	-mkdir -p $(OBJDIR)
	$(COMPILER) $(CFLAGS) $(INCLUDE) -o $@ -c $<

clean:
	-rm -f $(OBJECTS) $(DEPENDS) $(TARGET) $(TESTTARGET)

-include $(DEPENDS)
