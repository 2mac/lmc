/*
 * lmasm - Little Man Computer assembler
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

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define UNUSED(X) (void)(X)

#define MAX_LABEL_LEN 32
#define MAX_OPCODE_LEN 3
#define MAX_NUM_DIGITS 5

#if !(_ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L)
int
isblank(int c)
{
	return ' ' == c || '\t' == c;
}
#endif

enum lmasm_arg_format
{
	NO_ARGUMENT,
	ONE_ARGUMENT,
	MAYBE_ARGUMENT
};

struct lmasm_conf
{
	FILE *output_file;
	char *buf;
	int num_digits;
	int max_addr;
	int max_dat;
};

struct lmasm_opcode
{
	const char *name;
	int (*write)(const struct lmasm_opcode *self,
		const struct lmasm_conf *conf, int addr);
	int code;
	enum lmasm_arg_format arg_format;
};

struct lmasm_label
{
	char name[MAX_LABEL_LEN + 1];
	int addr;
};

static void
encode_decimal(char *buf, int n, int num_digits)
{
	int i;
	for (i = num_digits - 1; i >= 0; --i)
	{
		buf[i] = n % 10;
		n /= 10;
	}
}

static int
lmasm_write_dat(const struct lmasm_opcode *self,
		const struct lmasm_conf *conf, int value)
{
	char buf[MAX_NUM_DIGITS];
	int i;

	UNUSED(self);

	if (value < 0 || value > conf->max_dat)
	{
		fprintf(stderr, "DAT value %d out of range\n", value);
		return -1;
	}

	encode_decimal(buf, value, conf->num_digits);

	for (i = 0; i < conf->num_digits; ++i)
	{
		int rc = fputc(buf[i], conf->output_file);
		if (EOF == rc)
			return rc;
	}

	return 0;
}

static int
lmasm_write_op(const struct lmasm_opcode *self,
	const struct lmasm_conf *conf, int addr)
{
	char buf[MAX_NUM_DIGITS];
	int rc = 0, i;

	if (addr < 0 || addr > conf->max_addr)
	{
		fprintf(stderr, "%s mailbox %d out of range\n", self->name,
			addr);
		return -1;
	}

	rc = fputc(self->code, conf->output_file);
	if (EOF == rc)
		return rc;

	encode_decimal(buf, addr, conf->num_digits);

	for (i = 1; i < conf->num_digits; ++i)
	{
		rc = fputc(buf[i], conf->output_file);
		if (EOF == rc)
			return rc;
	}

	return 0;
}

static int
lmasm_write_io(const struct lmasm_opcode *self,
	const struct lmasm_conf *conf, int addr)
{
	/* both I/O ops are machine code 9xx with preset address fields, so we
	   create a fake opcode with code 9 and pass our code as the address */
	static struct lmasm_opcode temp = { NULL, NULL, 9, NO_ARGUMENT };

	UNUSED(addr);

	return lmasm_write_op(&temp, conf, self->code);
}

const struct lmasm_opcode OPCODES[] =
{
	{ "DAT", lmasm_write_dat, -1, MAYBE_ARGUMENT },
	{ "HLT", lmasm_write_op, 0, NO_ARGUMENT },
	{ "COB", lmasm_write_op, 0, NO_ARGUMENT },
	{ "ADD", lmasm_write_op, 1, ONE_ARGUMENT },
	{ "SUB", lmasm_write_op, 2, ONE_ARGUMENT },
	{ "STA", lmasm_write_op, 3, ONE_ARGUMENT },
	{ "LDA", lmasm_write_op, 5, ONE_ARGUMENT },
	{ "BRA", lmasm_write_op, 6, ONE_ARGUMENT },
	{ "BRZ", lmasm_write_op, 7, ONE_ARGUMENT },
	{ "BRP", lmasm_write_op, 8, ONE_ARGUMENT },
	{ "INP", lmasm_write_io, 1, NO_ARGUMENT },
	{ "OUT", lmasm_write_io, 2, NO_ARGUMENT }
};

#define NUM_OPCODES ((int)(sizeof OPCODES / sizeof (struct lmasm_opcode)))

static void
syntax(const char *msg, int line)
{
	fprintf(stderr, "Syntax error on line %d: %s\n", line, msg);
}

static int
next_non_blank(FILE *input_file)
{
	int c;
	while ((c = fgetc(input_file)) != EOF && isblank(c))
		;

	return c;
}

static int
finish_line(FILE *input_file, bool in_comment, int cur_line)
{
	int c;
	while ((c = fgetc(input_file)) != '\n' && c != EOF)
	{
		if (!isblank(c) && !in_comment)
		{
			if (c != '/')
			{
				syntax("Expected end-of-line", cur_line);
				return 1;
			}

			c = fgetc(input_file);
			if (c != '/')
			{
				syntax("Unexpected '/'", cur_line);
				return 1;
			}

			in_comment = true;
		}
	}

	return 0;
}

static bool
islabel(int c)
{
	return isalpha(c) || isdigit(c) || '_' == c;
}

static int
parse_label(char *buf, FILE *input_file, int cur_line)
{
	int i = 0, c;

	c = fgetc(input_file);
	if (isdigit(c))
	{
		syntax("Label begins with digit", cur_line);
		return 1;
	}

	while (islabel(c) && i < MAX_LABEL_LEN)
	{
		buf[i++] = c;
		c = fgetc(input_file);
	}

	if (i == MAX_LABEL_LEN && islabel(c))
	{
		fprintf(stderr, "Label on line %d exceeds max length of %d\n",
			cur_line, MAX_LABEL_LEN);
		return 1;
	}

	ungetc(c, input_file);
	buf[i] = '\0';
	return 0;
}

static int
parse_addr(FILE *input_file, const struct lmasm_label *labels, int num_labels,
	int cur_line)
{
	int c, rc, addr = 0;

	c = fgetc(input_file);
        if (isdigit(c))
	{
		addr = c - '0';
		while (isdigit((c = fgetc(input_file))))
		{
			addr *= 10;
			addr += c - '0';
		}

		if (islabel(c))
		{
			syntax("Label begins with digit", cur_line);
			return -1;
		}

		if (c != '\n' && c != EOF)
		{
			ungetc(c, input_file);
			rc = finish_line(input_file, false, cur_line);
			if (rc)
				return -1;
		}
	}
	else if (islabel(c))
	{
		char buf[MAX_LABEL_LEN + 1];
		int i;

		ungetc(c, input_file);
		rc = parse_label(buf, input_file, cur_line);
		if (rc)
			return -1;

		for (i = 0; i < num_labels; ++i)
		{
			if (strcmp(buf, labels[i].name) == 0)
			{
				addr = labels[i].addr;
				break;
			}
		}

		if (i == num_labels)
		{
			fprintf(stderr, "On line %d: no such label %s\n",
				cur_line, buf);
			return -1;
		}

		rc = finish_line(input_file, false, cur_line);
		if (rc)
			return -1;
	}
	else
	{
		syntax("Invalid or missing address field", cur_line);
		return -1;
	}

	return addr;
}

int
main(int argc, char *argv[])
{
	struct lmasm_conf conf;
	struct lmasm_label *labels = NULL;
	FILE *input_file = NULL;
	char opcode_name[MAX_OPCODE_LEN + 1];
	int i, c, rc = 0, cur_addr, cur_line;
	int num_labels = 0, label_buf_size = 32;

	conf.num_digits = 3;
	conf.buf = NULL;
	conf.output_file = NULL;

	if (argc < 3)
	{
		fprintf(stderr, "Usage: fmasm <input> <output>\n");
		return 1;
	}

	conf.max_dat = 1;
	for (i = 0; i < conf.num_digits; ++i)
		conf.max_dat *= 10;

	conf.max_addr = (conf.max_dat / 10) - 1;
	conf.max_dat -= 1;

	printf("Assembling for a %d-digit system. Max value: %d\n",
		conf.num_digits, conf.max_dat);

	conf.buf = malloc(conf.num_digits);
	if (!conf.buf)
	{
		fprintf(stderr, "Out of memory\n");
		rc = 1;
		goto end;
	}

	labels = calloc(label_buf_size, sizeof (struct lmasm_label));
	if (!labels)
	{
		fprintf(stderr, "Out of memory\n");
		rc = 1;
		goto end;
	}

	input_file = fopen(argv[1], "r");
	if (!input_file)
	{
		fprintf(stderr, "Error opening %s: %s\n", argv[1],
			strerror(errno));
		rc = 1;
		goto end;
	}

	cur_addr = 0;
	cur_line = 0;
	while ((c = fgetc(input_file)) != EOF)
	{
		++cur_line;
		if ('\n' == c)
			continue;

		if ('/' == c)
		{
			ungetc(c, input_file);
			rc = finish_line(input_file, false, cur_line);
			if (rc)
				goto end;

			continue;
		}

		if (!isblank(c)) /* start of a label */
		{
			struct lmasm_label *label;

			if (++num_labels >= label_buf_size)
			{
				void *temp;

				label_buf_size += 32;
				temp = realloc(labels,
					label_buf_size * sizeof (void *));
				if (!temp)
				{
					fprintf(stderr, "Out of memory\n");
					rc = 1;
					goto end;
				}

				labels = temp;
			}

			label = labels + num_labels - 1;
			label->addr = cur_addr;

			ungetc(c, input_file);
			rc = parse_label(label->name, input_file, cur_line);
			if (rc)
				goto end;

		}

		if (isblank(c))
		{
			while ((c = fgetc(input_file)) != '\n' && c != EOF)
			{
				if ('/' == c)
				{
					ungetc(c, input_file);
					rc = finish_line(input_file, false,
						cur_line);
					if (rc)
						goto end;

					break;
				}

				if (!isblank(c))
				{
					++cur_addr;
					finish_line(input_file, true,
						cur_line);
					break;
				}
			}
		}
	}

	if (cur_addr > conf.max_addr)
	{
		fprintf(stderr, "Program is too long. %d mailboxes, max %d\n",
			cur_addr, conf.max_addr);
		rc = 1;
		goto end;
	}

	conf.output_file = fopen(argv[2], "wb");
	if (!conf.output_file)
	{
		fprintf(stderr, "Failed to open %s: %s\n", argv[2],
			strerror(errno));
		rc = 1;
		goto end;
	}

	rc = fseek(input_file, 0L, SEEK_SET);
	if (-1 == rc)
	{
		fprintf(stderr, "Failed to rewind %s: %s\n", argv[1],
			strerror(errno));
		rc = 1;
		goto end;
	}

	printf("Now assembling %s ...\n%d mailboxes, %d bytes on disk\n",
		argv[2], cur_addr, cur_addr * 3);

	cur_line = 0;
	opcode_name[MAX_OPCODE_LEN] = '\0';
	while ((c = fgetc(input_file)) != EOF)
	{
		const struct lmasm_opcode *instruction = NULL;
		int addr = 0;

		++cur_line;
		if ('\n' == c)
			continue;

		if ('/' == c)
		{
			ungetc(c, input_file);
			rc = finish_line(input_file, false, cur_line);
			if (rc)
				goto end;

			continue;
		}

		/* skip label */
		while (islabel(c) && (c = fgetc(input_file)) != EOF)
			;

		while (isblank(c) && (c = fgetc(input_file)) != EOF)
			;

		if ('\n' == c)
			continue;

		if (EOF == c)
			break;

		for (i = 0; i < MAX_OPCODE_LEN; ++i)
		{
			if (EOF == c)
			{
				fprintf(stderr,
					"Unexpected EOF reading instruction");
				rc = 1;
				goto end;
			}

			opcode_name[i] = c;
			c = fgetc(input_file);
		}

		if (!isspace(c) && c != EOF)
		{
			fprintf(stderr, "Opcode on line %d is too long\n",
				cur_line);
			rc = 1;
			goto end;
		}

		for (i = 0; i < NUM_OPCODES; ++i)
		{
			if (strcasecmp(OPCODES[i].name, opcode_name) == 0)
			{
				instruction = &OPCODES[i];
				break;
			}
		}

		if (!instruction)
		{
			fprintf(stderr, "Error on line %d: "
				"No such instruction %s\n",
				cur_line, opcode_name);
			rc = 1;
			goto end;
		}

		ungetc(c, input_file);
		c = next_non_blank(input_file);
		if (c != EOF)
			ungetc(c, input_file);

		switch (instruction->arg_format)
		{
		case NO_ARGUMENT:
			rc = finish_line(input_file, false, cur_line);
			if (rc)
				goto end;
			break;

		case MAYBE_ARGUMENT:
			if (EOF == c)
				break;

			if (!islabel(c))
			{
				rc = finish_line(input_file, false, cur_line);
				if (rc)
					goto end;

				break;
			}

			/* FALLS THROUGH! */

		case ONE_ARGUMENT:
			addr = parse_addr(input_file, labels, num_labels,
					cur_line);
			if (-1 == addr)
			{
				rc = 1;
				goto end;
			}
			break;
		}

		rc = instruction->write(instruction, &conf, addr);
		if (-1 == rc)
		{
			rc = 1;
			goto end;
		}

		if (EOF == rc)
		{
			fprintf(stderr, "Error writing to %s: %s\n", argv[2],
				strerror(errno));
			rc = 1;
			goto end;
		}
	}

end:
	if (input_file)
		fclose(input_file);

	if (conf.output_file)
		fclose(conf.output_file);

	if (conf.buf)
		free(conf.buf);

	if (labels)
		free(labels);

	return rc;
}
