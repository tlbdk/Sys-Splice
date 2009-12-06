/*
 * This file contains the ini and command liner parser main.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "parse.h"
#include "debug.h"

static struct fio_option *fio_options;

static int vp_cmp(const void *p1, const void *p2)
{
	const struct value_pair *vp1 = p1;
	const struct value_pair *vp2 = p2;

	return strlen(vp2->ival) - strlen(vp1->ival);
}

static void posval_sort(struct fio_option *o, struct value_pair *vpmap)
{
	const struct value_pair *vp;
	int entries;

	memset(vpmap, 0, PARSE_MAX_VP * sizeof(struct value_pair));

	for (entries = 0; entries < PARSE_MAX_VP; entries++) {
		vp = &o->posval[entries];
		if (!vp->ival || vp->ival[0] == '\0')
			break;

		memcpy(&vpmap[entries], vp, sizeof(*vp));
	}

	qsort(vpmap, entries, sizeof(struct value_pair), vp_cmp);
}

static void show_option_range(struct fio_option *o)
{
	if (!o->minval && !o->maxval)
		return;

	printf("%20s: min=%d, max=%d\n", "range", o->minval, o->maxval);
}

static void show_option_values(struct fio_option *o)
{
	int i = 0;

	do {
		const struct value_pair *vp = &o->posval[i];

		if (!vp->ival)
			break;

		printf("%20s: %-10s", i == 0 ? "valid values" : "", vp->ival);
		if (vp->help)
			printf(" %s", vp->help);
		printf("\n");
		i++;
	} while (i < PARSE_MAX_VP);

	if (i)
		printf("\n");
}

static unsigned long get_mult_time(char c)
{
	switch (c) {
	case 'm':
	case 'M':
		return 60;
	case 'h':
	case 'H':
		return 60 * 60;
	case 'd':
	case 'D':
		return 24 * 60 * 60;
	default:
		return 1;
	}
}

static unsigned long get_mult_bytes(char c)
{
	switch (c) {
	case 'k':
	case 'K':
		return 1024;
	case 'm':
	case 'M':
		return 1024 * 1024;
	case 'g':
	case 'G':
		return 1024 * 1024 * 1024;
	case 'e':
	case 'E':
		return 1024 * 1024 * 1024 * 1024UL;
	default:
		return 1;
	}
}

/*
 * convert string into decimal value, noting any size suffix
 */
int str_to_decimal(const char *str, long long *val, int kilo)
{
	int len;

	len = strlen(str);
	if (!len)
		return 1;

	*val = strtoll(str, NULL, 10);
	if (*val == LONG_MAX && errno == ERANGE)
		return 1;

	if (kilo)
		*val *= get_mult_bytes(str[len - 1]);
	else
		*val *= get_mult_time(str[len - 1]);

	return 0;
}

static int check_str_bytes(const char *p, long long *val)
{
	return str_to_decimal(p, val, 1);
}

static int check_str_time(const char *p, long long *val)
{
	return str_to_decimal(p, val, 0);
}

void strip_blank_front(char **p)
{
	char *s = *p;

	while (isspace(*s))
		s++;

	*p = s;
}

void strip_blank_end(char *p)
{
	char *s;

	s = strchr(p, ';');
	if (s)
		*s = '\0';
	s = strchr(p, '#');
	if (s)
		*s = '\0';
	if (s)
		p = s;

	s = p + strlen(p);
	while ((isspace(*s) || iscntrl(*s)) && (s > p))
		s--;

	*(s + 1) = '\0';
}

static int check_range_bytes(const char *str, long *val)
{
	char suffix;

	if (!strlen(str))
		return 1;

	if (sscanf(str, "%lu%c", val, &suffix) == 2) {
		*val *= get_mult_bytes(suffix);
		return 0;
	}

	if (sscanf(str, "%lu", val) == 1)
		return 0;

	return 1;
}

static int check_int(const char *p, int *val)
{
	if (!strlen(p))
		return 1;
	if (strstr(p, "0x") || strstr(p, "0X")) {
		if (sscanf(p, "%x", val) == 1)
			return 0;
	} else {
		if (sscanf(p, "%u", val) == 1)
			return 0;
	}

	return 1;
}

static struct fio_option *find_option(struct fio_option *options,
				      const char *opt)
{
	struct fio_option *o;

	for (o = &options[0]; o->name; o++) {
		if (!strcmp(o->name, opt))
			return o;
		else if (o->alias && !strcmp(o->alias, opt))
			return o;
	}

	return NULL;
}

#define val_store(ptr, val, off, data)			\
	do {						\
		ptr = td_var((data), (off));		\
		*ptr = (val);				\
	} while (0)

static int __handle_option(struct fio_option *o, const char *ptr, void *data,
			   int first, int more)
{
	int il, *ilp;
	long long ull, *ullp;
	long ul1, ul2;
	char **cp;
	int ret = 0, is_time = 0;

	dprint(FD_PARSE, "__handle_option=%s, type=%d, ptr=%s\n", o->name,
							o->type, ptr);

	if (!ptr && o->type != FIO_OPT_STR_SET && o->type != FIO_OPT_STR) {
		fprintf(stderr, "Option %s requires an argument\n", o->name);
		return 1;
	}

	switch (o->type) {
	case FIO_OPT_STR: {
		fio_opt_str_fn *fn = o->cb;
		const struct value_pair *vp;
		struct value_pair posval[PARSE_MAX_VP];
		int i;

		posval_sort(o, posval);

		for (i = 0; i < PARSE_MAX_VP; i++) {
			vp = &posval[i];
			if (!vp->ival || vp->ival[0] == '\0')
				break;
			ret = 1;
			if (!strncmp(vp->ival, ptr, strlen(vp->ival))) {
				ret = 0;
				if (!o->off1)
					break;
				val_store(ilp, vp->oval, o->off1, data);
				break;
			}
		}

		if (ret)
			show_option_values(o);
		else if (fn)
			ret = fn(data, ptr);
		break;
	}
	case FIO_OPT_STR_VAL_TIME:
		is_time = 1;
	case FIO_OPT_STR_VAL:
	case FIO_OPT_STR_VAL_INT: {
		fio_opt_str_val_fn *fn = o->cb;

		if (is_time)
			ret = check_str_time(ptr, &ull);
		else
			ret = check_str_bytes(ptr, &ull);

		if (ret)
			break;

		if (o->maxval && ull > o->maxval) {
			fprintf(stderr, "max value out of range: %lld"
					" (%d max)\n", ull, o->maxval);
			return 1;
		}
		if (o->minval && ull < o->minval) {
			fprintf(stderr, "min value out of range: %lld"
					" (%d min)\n", ull, o->minval);
			return 1;
		}

		if (fn)
			ret = fn(data, &ull);
		else {
			if (o->type == FIO_OPT_STR_VAL_INT) {
				if (first)
					val_store(ilp, ull, o->off1, data);
				if (!more && o->off2)
					val_store(ilp, ull, o->off2, data);
			} else {
				if (first)
					val_store(ullp, ull, o->off1, data);
				if (!more && o->off2)
					val_store(ullp, ull, o->off2, data);
			}
		}
		break;
	}
	case FIO_OPT_STR_STORE: {
		fio_opt_str_fn *fn = o->cb;

		cp = td_var(data, o->off1);
		*cp = strdup(ptr);
		if (fn) {
			ret = fn(data, ptr);
			if (ret) {
				free(*cp);
				*cp = NULL;
			}
		}
		break;
	}
	case FIO_OPT_RANGE: {
		char tmp[128];
		char *p1, *p2;

		strncpy(tmp, ptr, sizeof(tmp) - 1);

		p1 = strchr(tmp, '-');
		if (!p1) {
			p1 = strchr(tmp, ':');
			if (!p1) {
				ret = 1;
				break;
			}
		}

		p2 = p1 + 1;
		*p1 = '\0';
		p1 = tmp;

		ret = 1;
		if (!check_range_bytes(p1, &ul1) &&
		    !check_range_bytes(p2, &ul2)) {
			ret = 0;
			if (ul1 > ul2) {
				unsigned long foo = ul1;

				ul1 = ul2;
				ul2 = foo;
			}

			if (first) {
				val_store(ilp, ul1, o->off1, data);
				val_store(ilp, ul2, o->off2, data);
			}
			if (o->off3 && o->off4) {
				val_store(ilp, ul1, o->off3, data);
				val_store(ilp, ul2, o->off4, data);
			}
		}

		break;
	}
	case FIO_OPT_INT:
	case FIO_OPT_BOOL: {
		fio_opt_int_fn *fn = o->cb;

		ret = check_int(ptr, &il);
		if (ret)
			break;

		if (o->maxval && il > (int) o->maxval) {
			fprintf(stderr, "max value out of range: %d (%d max)\n",
								il, o->maxval);
			return 1;
		}
		if (o->minval && il < o->minval) {
			fprintf(stderr, "min value out of range: %d (%d min)\n",
								il, o->minval);
			return 1;
		}

		if (o->neg)
			il = !il;

		if (fn)
			ret = fn(data, &il);
		else {
			if (first)
				val_store(ilp, il, o->off1, data);
			if (!more && o->off2)
				val_store(ilp, il, o->off2, data);
		}
		break;
	}
	case FIO_OPT_STR_SET: {
		fio_opt_str_set_fn *fn = o->cb;

		if (fn)
			ret = fn(data);
		else {
			if (first)
				val_store(ilp, 1, o->off1, data);
			if (!more && o->off2)
				val_store(ilp, 1, o->off2, data);
		}
		break;
	}
	case FIO_OPT_DEPRECATED:
		fprintf(stdout, "Option %s is deprecated\n", o->name);
		break;
	default:
		fprintf(stderr, "Bad option type %u\n", o->type);
		ret = 1;
	}

	return ret;
}

static int handle_option(struct fio_option *o, const char *ptr, void *data)
{
	const char *ptr2 = NULL;
	int r1, r2;

	dprint(FD_PARSE, "handle_option=%s, ptr=%s\n", o->name, ptr);

	/*
	 * See if we have a second set of parameters, hidden after a comma.
	 * Do this before parsing the first round, to check if we should
	 * copy set 1 options to set 2.
	 */
	if (ptr &&
	    (o->type != FIO_OPT_STR_STORE) &&
	    (o->type != FIO_OPT_STR)) {
		ptr2 = strchr(ptr, ',');
		if (!ptr2)
			ptr2 = strchr(ptr, ':');
		if (!ptr2)
			ptr2 = strchr(ptr, '-');
	}

	/*
	 * Don't return early if parsing the first option fails - if
	 * we are doing multiple arguments, we can allow the first one
	 * being empty.
	 */
	r1 = __handle_option(o, ptr, data, 1, !!ptr2);

	if (!ptr2)
		return r1;

	ptr2++;
	r2 = __handle_option(o, ptr2, data, 0, 0);

	return r1 && r2;
}

static struct fio_option *get_option(const char *opt,
				     struct fio_option *options, char **post)
{
	struct fio_option *o;
	char *ret;

	ret = strchr(opt, '=');
	if (ret) {
		*post = ret;
		*ret = '\0';
		ret = (char *) opt;
		(*post)++;
		strip_blank_end(ret);
		o = find_option(options, ret);
	} else {
		o = find_option(options, opt);
		*post = NULL;
	}

	return o;
}

static int opt_cmp(const void *p1, const void *p2)
{
	struct fio_option *o1, *o2;
	char *s1, *s2, *foo;
	int prio1, prio2;

	s1 = strdup(*((char **) p1));
	s2 = strdup(*((char **) p2));

	o1 = get_option(s1, fio_options, &foo);
	o2 = get_option(s2, fio_options, &foo);
	
	prio1 = prio2 = 0;
	if (o1)
		prio1 = o1->prio;
	if (o2)
		prio2 = o2->prio;

	free(s1);
	free(s2);
	return prio2 - prio1;
}

void sort_options(char **opts, struct fio_option *options, int num_opts)
{
	fio_options = options;
	qsort(opts, num_opts, sizeof(char *), opt_cmp);
	fio_options = NULL;
}

int parse_cmd_option(const char *opt, const char *val,
		     struct fio_option *options, void *data)
{
	struct fio_option *o;

	o = find_option(options, opt);
	if (!o) {
		fprintf(stderr, "Bad option <%s>\n", opt);
		return 1;
	}

	if (!handle_option(o, val, data))
		return 0;

	fprintf(stderr, "fio: failed parsing %s=%s\n", opt, val);
	return 1;
}

/*
 * Return a copy of the input string with substrings of the form ${VARNAME}
 * substituted with the value of the environment variable VARNAME.  The
 * substitution always occurs, even if VARNAME is empty or the corresponding
 * environment variable undefined.
 */
static char *option_dup_subs(const char *opt)
{
	char out[OPT_LEN_MAX+1];
	char in[OPT_LEN_MAX+1];
	char *outptr = out;
	char *inptr = in;
	char *ch1, *ch2, *env;
	ssize_t nchr = OPT_LEN_MAX;
	size_t envlen;

	in[OPT_LEN_MAX] = '\0';
	strncpy(in, opt, OPT_LEN_MAX);

	while (*inptr && nchr > 0) {
		if (inptr[0] == '$' && inptr[1] == '{') {
			ch2 = strchr(inptr, '}');
			if (ch2 && inptr+1 < ch2) {
				ch1 = inptr+2;
				inptr = ch2+1;
				*ch2 = '\0';

				env = getenv(ch1);
				if (env) {
					envlen = strlen(env);
					if (envlen <= nchr) {
						memcpy(outptr, env, envlen);
						outptr += envlen;
						nchr -= envlen;
					}
				}

				continue;
			}
		}

		*outptr++ = *inptr++;
		--nchr;
	}

	*outptr = '\0';
	return strdup(out);
}

int parse_option(const char *opt, struct fio_option *options, void *data)
{
	struct fio_option *o;
	char *post, *tmp;

	tmp = option_dup_subs(opt);

	o = get_option(tmp, options, &post);
	if (!o) {
		fprintf(stderr, "Bad option <%s>\n", tmp);
		free(tmp);
		return 1;
	}

	if (!handle_option(o, post, data)) {
		free(tmp);
		return 0;
	}

	fprintf(stderr, "fio: failed parsing %s\n", opt);
	free(tmp);
	return 1;
}

/*
 * Option match, levenshtein distance. Handy for not quite remembering what
 * the option name is.
 */
static int string_distance(const char *s1, const char *s2)
{
	unsigned int s1_len = strlen(s1);
	unsigned int s2_len = strlen(s2);
	unsigned int *p, *q, *r;
	unsigned int i, j;

	p = malloc(sizeof(unsigned int) * (s2_len + 1));
	q = malloc(sizeof(unsigned int) * (s2_len + 1));

	p[0] = 0;
	for (i = 1; i <= s2_len; i++)
		p[i] = p[i - 1] + 1;

	for (i = 1; i <= s1_len; i++) {
		q[0] = p[0] + 1;
		for (j = 1; j <= s2_len; j++) {
			unsigned int sub = p[j - 1];

			if (s1[i - 1] != s2[j - 1])
				sub++;

			q[j] = min(p[j] + 1, min(q[j - 1] + 1, sub));
		}
		r = p;
		p = q;
		q = r;
	}

	i = p[s2_len];
	free(p);
	free(q);
	return i;
}

static void show_option_help(struct fio_option *o)
{
	const char *typehelp[] = {
		"string (opt=bla)",
		"string with possible k/m/g postfix (opt=4k)",
		"string with range and postfix (opt=1k-4k)",
		"string with time postfix (opt=10s)",
		"string (opt=bla)",
		"string with dual range (opt=1k-4k,4k-8k)",
		"integer value (opt=100)",
		"boolean value (opt=1)",
		"no argument (opt)",
	};

	if (o->alias)
		printf("%20s: %s\n", "alias", o->alias);

	printf("%20s: %s\n", "type", typehelp[o->type]);
	printf("%20s: %s\n", "default", o->def ? o->def : "no default");
	show_option_range(o);
	show_option_values(o);
}

static struct fio_option *find_child(struct fio_option *options,
				     struct fio_option *o)
{
	struct fio_option *__o;

	for (__o = options + 1; __o->name; __o++)
		if (__o->parent && !strcmp(__o->parent, o->name))
			return __o;

	return NULL;
}

static void __print_option(struct fio_option *o, struct fio_option *org,
			   int level)
{
	char name[256], *p;
	int depth;

	if (!o)
		return;
	if (!org)
		org = o;

	p = name;
	depth = level;
	while (depth--)
		p += sprintf(p, "%s", "  ");

	sprintf(p, "%s", o->name);

	printf("%-24s: %s\n", name, o->help);
}

static void print_option(struct fio_option *o)
{
	struct fio_option *parent;
	struct fio_option *__o;
	unsigned int printed;
	unsigned int level;

	__print_option(o, NULL, 0);
	parent = o;
	level = 0;
	do {
		level++;
		printed = 0;

		while ((__o = find_child(o, parent)) != NULL) {
			__print_option(__o, o, level);
			o = __o;
			printed++;
		}

		parent = o;
	} while (printed);
}

int show_cmd_help(struct fio_option *options, const char *name)
{
	struct fio_option *o, *closest;
	unsigned int best_dist;
	int found = 0;
	int show_all = 0;

	if (!name || !strcmp(name, "all"))
		show_all = 1;

	closest = NULL;
	best_dist = -1;
	for (o = &options[0]; o->name; o++) {
		int match = 0;

		if (o->type == FIO_OPT_DEPRECATED)
			continue;

		if (name) {
			if (!strcmp(name, o->name) ||
			    (o->alias && !strcmp(name, o->alias)))
				match = 1;
			else {
				unsigned int dist;

				dist = string_distance(name, o->name);
				if (dist < best_dist) {
					best_dist = dist;
					closest = o;
				}
			}
		}

		if (show_all || match) {
			found = 1;
			if (match)
				printf("%24s: %s\n", o->name, o->help);
			if (show_all) {
				if (!o->parent)
					print_option(o);
				continue;
			}
		}

		if (!match)
			continue;

		show_option_help(o);
	}

	if (found)
		return 0;

	printf("No such command: %s", name);
	if (closest) {
		printf(" - showing closest match\n");
		printf("%20s: %s\n", closest->name, closest->help);
		show_option_help(closest);
	} else
		printf("\n");

	return 1;
}

/*
 * Handle parsing of default parameters.
 */
void fill_default_options(void *data, struct fio_option *options)
{
	struct fio_option *o;

	dprint(FD_PARSE, "filling default options\n");

	for (o = &options[0]; o->name; o++)
		if (o->def)
			handle_option(o, o->def, data);
}

/*
 * Sanitize the options structure. For now it just sets min/max for bool
 * values and whether both callback and offsets are given.
 */
void options_init(struct fio_option *options)
{
	struct fio_option *o;

	dprint(FD_PARSE, "init options\n");

	for (o = &options[0]; o->name; o++) {
		if (o->type == FIO_OPT_DEPRECATED)
			continue;
		if (o->type == FIO_OPT_BOOL) {
			o->minval = 0;
			o->maxval = 1;
		}
		if (o->type == FIO_OPT_STR_SET && o->def) {
			fprintf(stderr, "Option %s: string set option with"
					" default will always be true\n",
						o->name);
		}
		if (!o->cb && !o->off1) {
			fprintf(stderr, "Option %s: neither cb nor offset"
					" given\n", o->name);
		}
		if (o->type == FIO_OPT_STR || o->type == FIO_OPT_STR_STORE)
			continue;
		if (o->cb && (o->off1 || o->off2 || o->off3 || o->off4)) {
			fprintf(stderr, "Option %s: both cb and offset given\n",
								 o->name);
		}
	}
}
