# Target library
lib := libfs.a

all: $(lib)

## TODO: Phase 1
lib     := libfs.a
targets := libfs.a
objs    := $(patsubst %.c,%.o,$(wildcard *.c))
ARCHIVE := ar rcs
CC      := gcc
CFLAGS  := -Wall -Wextra -Werror -MMD

CUR_PWD := $(shell pwd)

## Verbose Flag
ifneq ($(V),1)
Q = @
endif

## Debug flag
ifneq ($(D),1)
CFLAGS += -O2
else
CFLAGS += -g
endif

all: $(targets)

deps := $(patsubst %.o,%.d,$(objs))
-include $(deps)

$(targets): $(objs)
	@echo "AR RCS	$@"
	$(Q)$(ARCHIVE) $@ $^

%.o: %.c
	@echo "CC	$@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "CLEAN	$(CUR_PWD)"
	$(Q)rm -f $(targets) $(objs) $(deps)
