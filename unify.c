/*
** unify.c - change a diff to/from a context diff from/to a unified diff.
**
** Version 1.1
**
** Author:  Wayne Davison <davison@borland.com>
**
** Feel free to use this code in any way you desire.
*/

#include <stdio.h>

extern char *malloc();

#define FIND_NEXT	0
#define PARSE_UNIDIFF	1
#define UNI_LINES	2
#define PARSE_CDIFF	3
#define PARSE_OLD	4
#define CHECK_OLD	5
#define OLD_LINES	6
#define PARSE_NEW	7
#define NEW_LINES	8

#define strnEQ(s1,s2,n) (!strncmp(s1,s2,n))
#define strnNE(s1,s2,n) strncmp(s1,s2,n)

#define AtoL(cp,n) {n = atol(cp); while (*cp <= '9' && *cp >= '0') cp++; }

char buf[2048];

struct liner {
    struct liner *link;
    char type;
    int num;
    char str[1];
} root, *head = &root, *hold = &root, *line;

long o_first = 0, o_last = -1;
long n_first = 0, n_last = 0;

long o_start, o_end, o_line;
long n_start, n_end, n_line;

long line_num = 0;
int input_type = 0;
int output_type = 0;
int echo_comments = 0;
int strip_comments = 0;
int patch_format = 0;
int use_equals = 0;

int state = FIND_NEXT;
int found_index = 0;
char name[256] = { '\0' };

void ensure_name(), add_line(), generate_output();

int
main(argc, argv)
int argc;
char *argv[];
{
    char type;
    char ndiff;		/* Equals '*' when we have a new-style context diff */
    int odiff = 0;
    char star_dash_plus = ' ';
    FILE *fp_in = stdin;

    while (--argc) {
	if (**++argv == '-') {
	    while (*++*argv) {
		switch (**argv) {
		case '=':		/* use '=' not ' ' in unified diffs */
		    use_equals = 1;
		    break;
		case 'c':		/* force context diff output */
		    output_type = 2;
		    break;
		case 'e':		/* echo comments to stderr */
		    echo_comments = 1;
		    break;
		case 'o':		/* old-style diff, no matter what */
		    odiff = 1;
		    break;
		case 'p':		/* generate patch format */
		case 'P':
		    patch_format = 1;
		    break;
		case 's':		/* strip comment lines */
		    strip_comments = 1;
		    break;
		case 'U':		/* force patch-unified output */
		    patch_format = 1;
		case 'u':		/* force unified output */
		    output_type = 1;
		    break;
		default:
		    fprintf(stderr, "Unknown option: '%c'\n", **argv);
		    exit(1);
		}
	    }
	} else {
	    if (fp_in != stdin) {
		fprintf(stderr, "Only one filename allowed.\n", *argv);
		exit(1);
	    }
	    if ((fp_in = fopen(*argv, "r")) == NULL) {
		fprintf(stderr, "Unable to open '%s'.\n", *argv);
		exit(1);
	    }
	}
    }

    while (fgets(buf, sizeof buf, fp_in)) {
	line_num++;
      reprocess:
	switch (state) {
	case FIND_NEXT:
	    if (input_type < 2 && strnEQ(buf, "@@ -", 4)) {
		input_type = 1;
		if (!output_type) {
		    output_type = 2;
		}
		ensure_name();
		state = PARSE_UNIDIFF;
		goto reprocess;
	    }
	    if (!(input_type & 1) && strnEQ(buf, "********", 8)) {
		input_type = 2;
		if (!output_type) {
		    output_type = 1;
		}
		ensure_name();
		state = PARSE_OLD;
		break;
	    }
	    if (strnEQ(buf, "Index: ", 7)) {
		found_index = 1;
		printf("%s", buf);
	    } else if (strnEQ(buf, "Prereq: ", 8)) {
		printf("%s", buf);
	    } else if (strnEQ(buf, "*** ", 4)
		    || strnEQ(buf, "--- ", 4)
		    || strnEQ(buf, "+++ ", 4)) {
		if (!found_index) {
		    char *cp;
		    int len;

		    for (cp=buf+4,len=0; *cp>' ' && len<255; cp++,len++) {
			;
		    }
		    if (!*name || len < strlen(name)) {
			strncpy(name, buf+4, len);
			name[len] = '\0';
		    }
		}
		if (!patch_format) {
		    if (output_type == 1 && (*buf == '+'
		      || *buf == '-' && star_dash_plus != '*')
		     || output_type == 2 && (*buf == '*'
		      || *buf == '-' && star_dash_plus == '*')) {
			printf("%s", buf);
		    } else if (*buf == '*' || *buf == '+') {
			printf("---%s", buf+3);
		    } else if (*buf == '-' && star_dash_plus == '*') {
			printf("+++%s", buf+3);
		    } else {
			printf("***%s", buf+3);
		    }
		    star_dash_plus = *buf;
		}
	    } else if (patch_format
	     && (strnEQ(buf, "Only in ", 8) || strnEQ(buf, "Common subdir", 13)
	      || strnEQ(buf, "diff -", 6) || strnEQ(buf, "Binary files", 12))) {
		if (echo_comments) {
		    fprintf(stderr, "%s%s", strip_comments ? "" : "!!! ", buf);
		}
	    } else {
		if (echo_comments) {
		    fprintf(stderr, "%s", buf);
		}
		if (!strip_comments) {
		    printf("%s", buf);
		}
	    }
	    break;
	case PARSE_UNIDIFF:
	{
	    char *cp;

	    if (strnNE(buf, "@@ -", 4)) {
		found_index = 0;
		*name = '\0';
		state = FIND_NEXT;
		goto reprocess;
	    }
	    cp = buf+4;
	    AtoL(cp, o_start);
	    if (*cp++ == ',') {
		AtoL(cp, o_end);
		cp++;
	    } else {
		o_end = 1;
	    }
	    if (*cp++ != '+') {
		goto bad_header;
	    }
	    AtoL(cp, n_start);
	    if (*cp++ == ',') {
		AtoL(cp, n_end);
		cp++;
	    } else {
		n_end = 1;
	    }
	    if (*cp != '@') {
	    bad_header:
		fprintf(stderr, "Invalid unified diff header at line %ld.\n",
			line_num);
		exit(1);
	    }
	    o_end = (o_start ? o_start + o_end - 1 : 0);
	    n_end = (n_start ? n_start + n_end - 1 : 0);
	    o_first = o_start;
	    n_first = n_start;
	    if (o_start) {
		o_line = o_start-1;
	    } else {
		o_line = o_last = 0;
	    }
	    if (n_start) {
		n_line = n_start-1;
	    } else {
		n_line = n_last = 0;
	    }
	    state = UNI_LINES;
	    break;
	}
	case UNI_LINES:
	    switch (*buf) {
	    case ' ':
	    case '=':
		*buf = ' ';
		o_last = ++o_line;
		n_last = ++n_line;
		break;
	    case '-':
		o_last = ++o_line;
		break;
	    case '+':
		n_last = ++n_line;
		break;
	    default:
		fprintf(stderr, "Malformed unified diff at line %ld.\n",
			line_num);
		exit(1);
	    }
	    add_line(*buf, 0L, buf+1);
	    if (o_line == o_end && n_line == n_end) {
		generate_output();
		state = PARSE_UNIDIFF;
	    }
	    break;
	case PARSE_CDIFF:
	    if (strnNE(buf, "********", 8)) {
		generate_output();
		found_index = 0;
		*name = '\0';
		state = FIND_NEXT;
		goto reprocess;
	    }
	    state = PARSE_OLD;
	    break;
	case PARSE_OLD:
	    ndiff = ' ';
	    o_start = -1;
	    if (sscanf(buf, "*** %ld,%ld %c", &o_start, &o_end, &ndiff) < 2) {
		if (o_start < 0) {
		    fprintf(stderr,
			"Context diff missing 'old' header at line %ld.\n",
			line_num);
		    exit(1);
		}
		o_end = o_start;
		ndiff = ' ';
	    } else if (odiff) {
		ndiff = ' ';
	    }
	    if (o_last >= 0) {
		if (o_start > o_last) {
		    generate_output();
		} else {
		    ndiff = ' ';
		    while (head->link && head->link->num != o_start) {
			head = head->link;
		    }
		}
	    }
	    o_line = o_start-1;
	    n_line = 0;
	    if (!o_first) {
		o_first = o_start;
	    }
	    if (!o_start) {
		state = PARSE_NEW;
	    } else {
		state = CHECK_OLD;
	    }
	    break;
	case CHECK_OLD:
	    if (strnEQ(buf, "--- ", 4)) {
		state = PARSE_NEW;
	    } else {
		state = OLD_LINES;
		hold = head;
	    }
	    goto reprocess;
	case OLD_LINES:
	    if (buf[0] == '\n') {
		strcpy(buf, "  \n");
	    }
	    if (buf[1] == '\n') {
		strcpy(buf+1, " \n");
	    }
	    if (buf[1] != ' ') {
		fprintf(stderr, "Malformed context diff at line %ld.\n",
			line_num);
		exit(1);
	    }
	    switch (*buf) {
	    case ' ':
		type = ' ';
		n_line++;
		o_line++;
		break;
	    case '-':
	    case '!':
		type = '-';
		o_line++;
		break;
	    default:
		fprintf(stderr, "Malformed context diff at line %ld.\n",
			line_num);
		exit(1);
	    }
	    if (o_line > o_last) {
		add_line(type, 0L, buf+2);
		o_last = o_line;
		n_last = n_line;
	    } else {
		do {
		    hold = hold->link;
		} while (hold->type == '+');
		if (type != ' ') {
		    hold->type = type;
		    hold->num = 0;
		}
	    }
	    if (o_line == o_end) {
		state = PARSE_NEW;
	    }
	    break;
	case PARSE_NEW:
	    if (*buf == '\n') {
		break;
	    }
	    n_start = -1;
	    if (sscanf(buf, "--- %ld,%ld", &n_start, &n_end) != 2) {
		if (n_start < 0) {
		    fprintf(stderr,
			"Context diff missing 'new' header at line %ld.\n",
			line_num);
		    exit(1);
		}
		n_end = n_start;
	    }
	    n_last = n_line;
	    o_line = o_start ? o_start-1 : 0;
	    n_line = n_start ? n_start-1 : 0;
	    n_last += n_line;
	    hold = head;
	    if (!n_first) {
		n_first = n_start;
		while (hold->link && hold->link->type == '-') {
		    hold = hold->link;
		    hold->num = ++o_line;
		}
	    }
	    if (ndiff == '*' && n_last == n_end) {
		state = PARSE_CDIFF;
		break;
	    }
	    state = NEW_LINES;
	    break;
	case NEW_LINES:
	    if (buf[0] == '\n') {
		strcpy(buf, "  \n");
	    }
	    if (buf[1] == '\n') {
		strcpy(buf+1, " \n");
	    }
	    if (buf[1] != ' ') {
		fprintf(stderr, "Malformed context diff at line %ld.\n",
			line_num);
		exit(1);
	    }
	    switch (*buf) {
	    case ' ':
		type = ' ';
		n_line++;
		o_line++;
		break;
	    case '+':
	    case '!':
		type = '+';
		n_line++;
		break;
	    default:
		fprintf(stderr, "Malformed context diff at line %ld.\n",
			line_num);
		exit(1);
	    }
	    if (o_line > o_last) {
		o_last = o_line;
		add_line(type, o_line, buf+2);
		n_last++;
	    } else if (type != ' ') {
		add_line(type, 0L, buf+2);
		n_last++;
	    } else {
		hold = hold->link;
		hold->num = o_line;
		while (hold->link && !hold->link->num
		    && hold->link->type != ' ') {
		    hold = hold->link;
		    if (hold->type == '-') {
			hold->num = ++o_line;
		    }
		}
	    }
	    if (o_line == o_end && n_line == n_end) {
		state = PARSE_CDIFF;
	    }
	    break;
	}/* switch */
    }/* while */
    generate_output();

    return 0;
}

void
ensure_name()
{
    char *cp = name;

    if (!found_index) {
	if (!*name) {
	    fprintf(stderr,
		"Couldn't find a name for the diff at line %ld.\n",
		line_num);
	} else if (patch_format) {
	    if (cp[0] == '.' && cp[1] == '/') {
		cp += 2;
	    }
	    printf("Index: %s\n", cp);
	}
    }
}

void
add_line(type, num, str)
char type;
long num;
char *str;
{
    line = (struct liner *)malloc(sizeof (struct liner) + strlen(str));
    if (!line) {
	fprintf(stderr, "Out of memory!\n");
	exit(1);
    }
    line->type = type;
    line->num = num;
    strcpy(line->str, str);
    line->link = hold->link;
    hold->link = line;
    hold = line;
}

void
generate_output()
{
    if (o_last < 0) {
	return;
    }
    if (output_type == 1) {
	long i, j;

	i = o_first ? o_last - o_first + 1 : 0;
	j = n_first ? n_last - n_first + 1 : 0;
	printf("@@ -%ld,%ld +%ld,%ld @@\n", o_first, i, n_first, j);
	for (line = root.link; line; line = hold) {
	    printf("%c%s", use_equals && line->type == ' '? '=' : line->type,
		line->str);
	    hold = line->link;
	    free(line);
	}
    } else { /* if output == 2 */
	struct liner *scan;
	int found_plus = 1;
	char ch;

	printf("***************\n*** %ld", o_first);
	if (o_first == o_last) {
	    printf(" ****\n");
	} else {
	    printf(",%ld ****\n", o_last);
	}
	for (line = root.link; line; line = line->link) {
	    if (line->type == '-') {
		break;
	    }
	}
	if (line) {
	    found_plus = 0;
	    ch = ' ';
	    for (line = root.link; line; line = line->link) {
		switch (line->type) {
		case '-':
		    if (ch != ' ') {
			break;
		    }
		    scan = line;
		    while ((scan = scan->link) != NULL && scan->type == '-') {
			;
		    }
		    if (scan && scan->type == '+') {
			do {
			    scan->type = '!';
			} while ((scan = scan->link) && scan->type == '+');
			ch = '!';
		    } else {
			ch = '-';
		    }
		    break;
		case '+':
		case '!':
		    found_plus = 1;
		    continue;
		case ' ':
		    ch = ' ';
		    break;
		}
		printf("%c %s", ch, line->str);
	    }/* for */
	}/* if */
	if (n_first == n_last) {
	    printf("--- %ld ----\n", n_first);
	} else {
	    printf("--- %ld,%ld ----\n", n_first, n_last);
	}
	if (found_plus) {
	    for (line = root.link; line; line = line->link) {
		if (line->type != '-') {
		    printf("%c %s", line->type, line->str);
		}
	    }
	}
	for (line = root.link; line; line = hold) {
	    hold = line->link;
	    free(line);
	}
    }/* if output == 2 */

    root.link = NULL;
    head = &root;
    hold = &root;

    o_first = 0;
    n_first = 0;
    o_last = -1;
    n_last = 0;
}
