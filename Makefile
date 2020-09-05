CC ?= cc
STND ?= -ansi -pedantic
CFLAGS += $(STND) -O2 -Wall -Wextra -Werror -Wunreachable-code -ftrapv

all: lmc lmasm

lmc_deps = lmc.o
lmc: $(lmc_deps)
	$(CC) -o lmc $(lmc_deps)

lmasm_deps = lmasm.o
lmasm: $(lmasm_deps)
	$(CC) -o lmasm $(lmasm_deps)

clean:
	rm -f lmc lmasm *.o

.PHONY: clean all
