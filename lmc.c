/*
 * lmc - Little Man Computer
 * Copyright (C) 2020 David McMackins II
 *
 * Redistributions, modified or unmodified, in whole or in part, must retain
 * applicable copyright or other legal privilege notices, these conditions, and
 * the following license terms and disclaimer.  Subject to these conditions,
 * the holder(s) of copyright or other legal privileges, author(s) or
 * assembler(s), and contributors of this work hereby grant to any person who
 * obtains a copy of this work in any form:
 *
 * 1. Permission to reproduce, modify, distribute, publish, sell, sublicense,
 * use, and/or otherwise deal in the licensed material without restriction.
 *
 * 2. A perpetual, worldwide, non-exclusive, royalty-free, irrevocable patent
 * license to reproduce, modify, distribute, publish, sell, use, and/or
 * otherwise deal in the licensed material without restriction, for any and all
 * patents:
 *
 *     a. Held by each such holder of copyright or other legal privilege,
 *     author or assembler, or contributor, necessarily infringed by the
 *     contributions alone or by combination with the work, of that privilege
 *     holder, author or assembler, or contributor.
 *
 *     b. Necessarily infringed by the work at the time that holder of
 *     copyright or other privilege, author or assembler, or contributor made
 *     any contribution to the work.
 *
 * NO WARRANTY OF ANY KIND IS IMPLIED BY, OR SHOULD BE INFERRED FROM, THIS
 * LICENSE OR THE ACT OF DISTRIBUTION UNDER THE TERMS OF THIS LICENSE,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS,
 * ASSEMBLERS, OR HOLDERS OF COPYRIGHT OR OTHER LEGAL PRIVILEGE BE LIABLE FOR
 * ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN ACTION OF CONTRACT, TORT,
 * OR OTHERWISE ARISING FROM, OUT OF, OR IN CONNECTION WITH THE WORK OR THE USE
 * OF OR OTHER DEALINGS IN THE WORK.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef NUM_MAILBOXES
# define NUM_MAILBOXES 100
#endif

#ifndef NUM_DIGITS
# define NUM_DIGITS 3
#endif

#ifndef MAX_VALUE
# define MAX_VALUE 999
#endif

struct lmc_cpu
{
	int a;
	int pc;
	int instruction;
	int opcode;
	int addr;
	bool neg;
	bool halted;

	bool error; /* controls program exit code */
};

struct lmc
{
	int mailboxes[NUM_MAILBOXES];
	struct lmc_cpu cpu;
};

static void
bad_instruction(struct lmc *lmc)
{
	struct lmc_cpu *cpu = &lmc->cpu;

	fprintf(stderr, "Bad instruction! (%d)\n", cpu->instruction);
	fprintf(stderr, "a  = %d\n", cpu->a);
	fprintf(stderr, "pc = %d\n", cpu->pc);
	fprintf(stderr, "opcode = %d\n", cpu->opcode);
	fprintf(stderr, "addr   = %d\n", cpu->addr);
	fprintf(stderr, "neg    = %d\n", !!cpu->neg);
	fprintf(stderr, "halt   = %d\n", !!cpu->halted);

	cpu->halted = true;
	cpu->error = true;
}

typedef void (*lmc_op)(struct lmc *lmc);

static void
lmc_halt(struct lmc *lmc)
{
	lmc->cpu.halted = true;
}

static void
lmc_add(struct lmc *lmc)
{
	lmc->cpu.a += lmc->mailboxes[lmc->cpu.addr];
	lmc->cpu.neg = lmc->cpu.a > MAX_VALUE;
	if (lmc->cpu.neg)
		lmc->cpu.a -= MAX_VALUE + 1;
}

static void
lmc_sub(struct lmc *lmc)
{
	lmc->cpu.a -= lmc->mailboxes[lmc->cpu.addr];
	lmc->cpu.neg = lmc->cpu.a < 0;
	if (lmc->cpu.neg)
		lmc->cpu.a += MAX_VALUE + 1;
}

static void
lmc_store(struct lmc *lmc)
{
	lmc->mailboxes[lmc->cpu.addr] = lmc->cpu.a;
}

static void
lmc_load(struct lmc *lmc)
{
	lmc->cpu.a = lmc->mailboxes[lmc->cpu.addr];
}

static void
lmc_branch(struct lmc *lmc)
{
	lmc->cpu.pc = lmc->cpu.addr;
}

static void
lmc_branch_zero(struct lmc *lmc)
{
	if (0 == lmc->cpu.a)
		lmc->cpu.pc = lmc->cpu.addr;
}

static void
lmc_branch_positive(struct lmc *lmc)
{
	if (!lmc->cpu.neg)
		lmc->cpu.pc = lmc->cpu.addr;
}

static void
lmc_io(struct lmc *lmc)
{
	int rc;

	switch (lmc->cpu.addr)
	{
	case 1:
		rc = 0;
		while (rc != 1)
		{
			printf("Input number (0-%d): ", MAX_VALUE);
			rc = scanf("%d", &lmc->cpu.a);
		}
		break;

	case 2:
		printf("%d\n", lmc->cpu.a);
		break;

	default:
		bad_instruction(lmc);
		break;
	}
}

const lmc_op OPS[] =
{
	lmc_halt,
	lmc_add,
	lmc_sub,
	lmc_store,
	NULL, /* no 4xx instruction in ISA */
	lmc_load,
	lmc_branch,
	lmc_branch_zero,
	lmc_branch_positive,
	lmc_io
};

int
main(int argc, char *argv[])
{
	struct lmc lmc;
	FILE *input_file;
	int c, i;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: lmc <input>\n");
		return 1;
	}

	memset(&lmc, 0, sizeof lmc);

	input_file = fopen(argv[1], "rb");
	if (!input_file)
	{
		fprintf(stderr, "Error opening %s: %s\n", argv[1],
			strerror(errno));
		return 1;
	}

	i = 0;
	while ((c = fgetc(input_file)) != EOF)
	{
		int *mailbox = &lmc.mailboxes[i / NUM_DIGITS];
		*mailbox *= 10;
		*mailbox += c;

		++i;
	}

	if (ferror(input_file))
	{
		fprintf(stderr, "Error loading %s: %s\n", argv[1],
			strerror(errno));
		fclose(input_file);
		return 1;
	}

	fclose(input_file);

	if ((i % NUM_DIGITS) != 0)
	{
		fprintf(stderr,
			"File size is not a multiple of the number of digits per mailbox\n");
		return 1;
	}

	printf("%s loaded. %d mailboxes.\n", argv[1], i / NUM_DIGITS);

	while (!lmc.cpu.halted)
	{
		lmc_op op;

		lmc.cpu.instruction = lmc.mailboxes[lmc.cpu.pc++];
		lmc.cpu.opcode = lmc.cpu.instruction / NUM_MAILBOXES;
		lmc.cpu.addr = lmc.cpu.instruction % NUM_MAILBOXES;

		if (lmc.cpu.opcode > 9 || (op = OPS[lmc.cpu.opcode]) == NULL)
		{
			bad_instruction(&lmc);
			break;
		}

		op(&lmc);
	}

	return !!lmc.cpu.error;
}
