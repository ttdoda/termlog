/*
 * termlog v0.1 - rearrange and serialize VT100/ANSI terminal screen log
 *
 *	Written by Junn Ohta, 1997/02/18.  Public Domain.
 */

char	*progname = "termlog";
char	*version  = "0.1";

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

typedef unsigned long	cell;

#define TRUE	1
#define FALSE	0

#define ESC	'\033'
#define SO	'\016'
#define SI	'\017'
#define DEL	'\177'
#define SS2	'\216'
#define SS3	'\217'

#define A_NORM	0x00000000L
#define A_REV	0x00010000L
#define A_UL	0x00020000L
#define A_GRAPH	0x00040000L
#define A_KNJ1	0x00100000L
#define A_KNJ2	0x00200000L
#define A_MASK	0x00ff0000L

#define C_MASK	0x0000ffffL

#define SPACE	(A_NORM | ' ')

#define KC_NONE	0
#define KC_EUC	1
#define KC_JIS	2
#define KC_SJIS	3

#define M_ASCII	0
#define M_KANJI	1
#define M_KANA	2
#define M_GRAPH	3

#define SJIS1(c)	((c)>=0x81 && (c)<=0x9f || (c)>=0xe0 && (c)<=0xfc)
#define SJIS2(c)	((c)>=0x40 && (c)!=0x7f && (c)<=0xfc)

int	kcode = KC_NONE;
int	kselk, ksela;
int	mode, savemode;

cell	**screen;
int	lines = 24;
int	cols = 80;
int	lin, col, attr;
int	savelin, savecol, saveattr;
int	scrtop, scrend;

int	verbose = FALSE;
int	keepgr = FALSE;

int	scrinit();
void	usage();
int	termlog();
int	csi();
void	scrollup();
void	scrolldown();
void	clearscr();
void	sepline();
void	flush();
void	flushline();

int
main(ac, av)
int	ac;
char	**av;
{
	FILE	*fp;

	if (scrinit(&ac, &av) < 0)
		exit(1);
	if (ac == 0) {
		termlog(stdin);
		exit(0);
	}
	while (ac > 0) {
		fp = fopen(*av, "rb");
		if (fp == NULL) {
			printf("%s: can't open %s\n", progname, *av);
		} else {
			termlog(fp);
			fclose(fp);
		}
		ac--, av++;
	}
	exit(0);

}

int
scrinit(acp, avp)
int	*acp;
char	***avp;
{
	int	i, ret, ac;
	char	*p, **av;

	ac = *acp;
	av = *avp;
	ret = -1;

	ac--, av++;
	while (ac > 0 && **av == '-') {
		switch ((*av)[1]) {
		case 'g':
			cols = atoi(*av + 2);
			p = strchr(*av + 2, 'x');
			if (p)
				lines = atoi(p + 1);
			if (cols <= 0 || lines <= 0) {
				usage();
				goto done;
			}
			break;
		case 'k':
			switch ((*av)[2]) {
			case 'e':
				kcode = KC_EUC;
				break;
			case 'j':
				kcode = KC_JIS;
				kselk = 'B';
				ksela = 'B';
				break;
			case 's':
				kcode = KC_SJIS;
				break;
			default:
				usage();
				goto done;
			}
			break;
		case 'm':
			keepgr = TRUE;
			break;
		case 'v':
			verbose = TRUE;
			break;
		default:
			usage();
			goto done;
		}
		ac--, av++;
	}
	if (ac == 0 && isatty(0)) {
		usage();
		goto done;
	}

	screen = (cell **)malloc(lines * sizeof(cell *));
	if (screen == NULL) {
		printf("%s: memory short\n", progname);
		goto done;
	}
	for (i = 0; i < lines; i++) {
		screen[i] = (cell *)malloc(cols * sizeof(cell));
		if (screen[i] == NULL) {
			printf("%s: memory short\n", progname);
			goto done;
		}
	}
	ret = 0;

done:
	*acp = ac;
	*avp = av;
	return ret;

}

void
usage()
{
	printf("Usage: %s [-gCOLSxLINES] [-k[ejs]] [-m] [-v] file ...\n", progname);
	printf("Options:\n");
	printf("    -g: set screen size (default: -g80x24)\n");
	printf("    -k: set input code (don't care if no kana exists)\n");
	printf("    -m: retain graphic renditions (reverse, underline)\n");
	printf("    -v: verbose output\n");

}

int
termlog(fp)
FILE	*fp;
{
	int	c, c1, flushed;
	cell	l;

	lin = savelin = 0;
	col = savecol = 0;
	attr = saveattr = A_NORM;
	scrtop = 0;
	scrend = lines;
	mode = savemode = M_ASCII;
	flushed = TRUE;
	sepline();
	clearscr();

	while ((c = getc(fp)) != EOF) {
		if (c >= ' ' && c < DEL || (c & 0x80)) {
		retry:
			if (col >= cols) {
				lin++;
				if (lin == scrend) {
					scrollup(1);
					lin = scrend - 1;
				} else if (lin == lines) {
					lin = lines - 1;
				}
				col = 0;
			}
			if (kcode == KC_EUC && c >= 0xa1 && c <= 0xfe ||
			    kcode == KC_SJIS && SJIS1(c) ||
			    kcode == KC_NONE && c >= 0x81 && c <= 0xfe ||
			    mode == M_KANJI) {
				if (col == cols - 1) {
					if (verbose && !flushed &&
					    screen[lin][col] != SPACE) {
						flush();
						flushed = TRUE;
					}
					screen[lin][col++] = attr|' ';
					goto retry;
				}
				c1 = getc(fp);
				if (c1 == EOF)
					goto done;
				if (kcode == KC_NONE) {
					if (c >= 0x81 && c <= 0x9f)
						kcode = KC_SJIS;
					else if (c >= 0xf0)
						kcode = KC_EUC;
				}
				if (verbose && !flushed &&
				    screen[lin][col] != SPACE &&
				    screen[lin][col + 1] != SPACE) {
					flush();
					flushed = TRUE;
				}
				l = (unsigned int)(c << 8 | c1);
				screen[lin][col++] = A_KNJ1|attr|l;
				screen[lin][col++] = A_KNJ2|attr|l;
				if (col < cols &&
				    screen[lin][col] & A_KNJ2)
					screen[lin][col] = SPACE;
			} else {
				if (kcode == KC_EUC && c == SS2) {
					c = getc(fp);
					if (c == EOF)
						goto done;
					c |= 0x80;
				}
				if (mode == M_KANA)
					c |= 0x80;
				if (verbose && !flushed &&
				    screen[lin][col] != SPACE) {
					flush();
					flushed = TRUE;
				}
				if (mode == M_GRAPH)
					screen[lin][col++] = A_GRAPH|attr|c;
				else
					screen[lin][col++] = attr|c;
				if (col < cols &&
				    screen[lin][col] & A_KNJ2)
					screen[lin][col] = SPACE;
			}
			continue;
		}
		flushed = FALSE;
		switch (c) {
		case '\b': /* backspace */
			if (col > 0)
				col--;
			break;
		case '\r': /* carriage return */
			col = 0;
			break;
		case '\n': /* linefeed */
			lin++;
			if (lin == scrend) {
				scrollup(1);
				lin = scrend - 1;
			} else if (lin == lines) {
				lin = lines - 1;
			}
			break;
		case '\t': /* tab */
			if (col < cols)
				col += 8 - (col % 8);
			break;
		case '\f': /* formfeed */
			flush();
			clearscr();
			break;
		case SO: /* shift out */
			if (kcode == KC_NONE)
				kcode = KC_JIS;
			savemode = mode;
			mode = M_KANA;
			break;
		case SI: /* shift in */
			if (kcode == KC_NONE)
				kcode = KC_JIS;
			mode = savemode;
			break;
		case ESC: /* ESC */
			c = getc(fp);
			switch (c) {
			case EOF:
				goto done;
			case '$':
				c = getc(fp);
				switch (c) {
				case EOF:
					goto done;
				case '@':
				case 'B':
					if (kcode == KC_NONE)
						kcode = KC_JIS;
					kselk = c;
					mode = M_KANJI;
					break;
				default:
					break;
				}
				break;
			case '(':
				c = getc(fp);
				switch (c) {
				case EOF:
					goto done;
				case 'H':
				case 'J':
				case 'B':
					ksela = c;
					mode = M_ASCII;
					break;
				case 'I':
					if (kcode == KC_NONE)
						kcode = KC_JIS;
					mode = M_KANA;
					break;
				case '0':
					mode = M_GRAPH;
					break;
				default:
					break;
				}
				break;
			case '7': /* save cursor */
				savelin = lin;
				savecol = col;
				saveattr = attr;
				break;
			case '8': /* restore cursor */
				lin = savelin;
				col = savecol;
				attr = saveattr;
				break;
			case 'D': /* scroll forward (col unchanged) */
				lin++;
				if (lin == scrend) {
					scrollup(1);
					lin = scrend - 1;
				} else if (lin == lines) {
					lin = lines - 1;
				}
				break;
			case 'E': /* new line */
				lin++;
				if (lin == scrend) {
					scrollup(1);
					lin = scrend - 1;
				} else if (lin == lines) {
					lin = lines - 1;
				}
				col = 0;
				break;
			case 'M': /* scroll backward (col unchanged) */
				lin--;
				if (lin == scrtop - 1) {
					scrolldown(1);
					lin = scrtop;
				} else if (lin == -1) {
					lin = 0;
				}
				break;
			case '=': /* keypad application mode */
			case '>': /* keypad normal mode */
				/* do nothing */
				break;
			case '#': /* line settings */
				c = getc(fp);
				if (c == EOF)
					goto done;
				break;
			case '[':
				if (csi(fp) == EOF)
					goto done;
				break;
			default:
				/* do nothing */
				break;
			}
			break;
		default:
			break;
		}
	}
done:
	flush();
	return 0;

}

int
csi(fp)
FILE	*fp;
{
	int	c, i, j, k, n, np, p[10];
	int	leader;
	cell	*tmp;

	c = getc(fp);
	if (c == EOF)
		return EOF;
	if (strchr("<=>?", c)) {
		leader = c;
		c = getc(fp);
		if (c == EOF)
			return EOF;
	}
	p[0] = p[1] = 0;
	np = 0;
	for (;;) {
		n = 0;
		while (isdigit(c)) {
			n *= 10;
			n += c - '0';
			c = getc(fp);
			if (c == EOF)
				return EOF;
		}
		if (np < 10)
			p[np++] = n;
		if (c != ';')
			break;
		c = getc(fp);
		if (c == EOF)
			return EOF;
	}
	switch (c) {
	case 'H': /* move cursor */
	case 'f': /* move cursor */
		lin = p[0]? p[0] - 1: 0;
		col = p[1]? p[1] - 1: 0;
		if (lin >= lines)
			lin = lines - 1;
		if (col >= cols)
			col = cols - 1;
		break;
	case 'A': /* move cursor up */
		n = p[0]? p[0]: 1;
		if (lin >= scrtop && lin - n < scrtop)
			lin = scrtop;
		else if (lin - n < 0)
			lin = 0;
		else
			lin -= n;
		break;
	case 'B': /* move cursor down */
		n = p[0]? p[0]: 1;
		if (lin < scrend && lin + n >= scrend)
			lin = scrend - 1;
		else if (lin + n >= lines)
			lin = lines - 1;
		else
			lin += n;
		break;
	case 'C': /* move cursor right */
		n = p[0]? p[0]: 1;
		if (col + n >= cols)
			col = cols - 1;
		else
			col += n;
		break;
	case 'D': /* move cursor left */
		n = p[0]? p[0]: 1;
		if (col - n < 0)
			col = 0;
		else
			col -= n;
		break;
	case 'J': /* clear display */
		switch (p[0]) {
		default:
		case 0:
			if (lin == 0 && col == 0) {
				flush();
				clearscr();
				break;
			}
			if (verbose)
				flush();
			for (j = col; j < cols; j++)
				screen[lin][j] = SPACE;
			for (i = lin + 1; i < lines; i++)
				for (j = 0; j < cols; j++)
					screen[i][j] = SPACE;
			break;
		case 1:
			if (verbose)
				flush();
			for (i = 0; i < lin; i++)
				for (j = 0; j < cols; j++)
					screen[i][j] = SPACE;
			for (j = 0; j < col; j++)
				screen[lin][j] = SPACE;
			break;
		case 2:
			flush();
			clearscr();
			break;
		}
		break;
	case 'K': /* clear line */
		switch (p[0]) {
		default:
		case 0:
			j = col;
			k = cols;
			break;
		case 1:
			j = 0;
			k = col;
			break;
		case 2:
			j = 0;
			k = cols;
			break;
		}
		if (verbose) {
			for (i = j; i < k; i++) {
				if (screen[lin][i] != SPACE) {
					flush();
					break;
				}
			}
		}
		for (i = j; i < k; i++)
			screen[lin][i] = SPACE;
		break;
	case 'L': /* insert line */
		n = p[0]? p[0]: 1;
		if (lin < scrtop || lin >= scrend)
			break;
		if (verbose) {
			tmp = screen[scrend - 1];
			for (i = 0; i < cols; i++)
				if (tmp[i] != SPACE)
					break;
			if (i < cols)
				flush();
		}
		for (i = 0; i < n; i++) {
			tmp = screen[scrend - 1];
			for (j = scrend - 1; j > lin; j--)
				screen[j] = screen[j - 1];
			for (j = 0; j < cols; j++)
				tmp[j] = SPACE;
			screen[lin] = tmp;
		}
		break;
	case 'M': /* delete line */
		n = p[0]? p[0]: 1;
		if (lin < scrtop || lin >= scrend)
			break;
		if (verbose) {
			tmp = screen[lin];
			for (i = 0; i < cols; i++)
				if (tmp[i] != SPACE)
					break;
			if (i < cols)
				flush();
		}
		for (i = 0; i < n; i++) {
			tmp = screen[lin];
			for (j = lin; j < scrend - 1; j++)
				screen[j] = screen[j + 1];
			for (j = 0; j < cols; j++)
				tmp[j] = SPACE;
			screen[scrend - 1] = tmp;
		}
		break;
	case 'P': /* delete character */
		n = p[0]? p[0]: 1;
		if (verbose) {
			j = col + n;
			if (j >= cols)
				j = cols;
			for (i = col; i < j; i++)
				if (screen[lin][i] != SPACE)
					break;
			if (i < j)
				flush();
		}
		for (i = 0; i < n; i++) {
			for (j = col; j < cols - 1; j++)
				screen[lin][j] = screen[lin][j + 1];
			screen[lin][cols - 1] = SPACE;
		}
		break;
	case 'r': /* change scroll region */
		if (p[0] < p[1])
			break;
		if (p[0] == 0 && p[1] == 0) {
			scrtop = 0;
			scrend = lines;
			break;
		}
		scrtop = p[0]? p[0] - 1: 0;
		scrend = p[1]? p[1] - 1: 0;
		break;
	case 'm': /* set graphics rendition */
		if (np == 0)
			p[np++] = 0;
		for (i = 0; i < np; i++) {
			switch (p[i]) {
			case 0:
				attr &= ~(A_REV|A_UL);
				break;
			case 4:
				attr |= A_UL;
				break;
			case 7:
				attr |= A_REV;
				break;
			default:
				break;
			}
		}
		break;
	case 's': /* save cursor (ANSI) */
		savelin = lin;
		savecol = col;
		saveattr = attr;
		break;
	case 'u': /* restore cursor (ANSI) */
		lin = savelin;
		col = savecol;
		attr = saveattr;
		break;
	case 'h': /* set mode */
	case 'l': /* reset mode */
		break;
	default:
		break;
	}
	return 0;

}

void
scrollup(n)
int	n;
{
	int	i, j;
	cell	*tmp;

	if (verbose && (scrtop > 0 || scrend < lines))
		flush();
	for (i = 0; i < n; i++) {
		if (scrtop == 0 && scrend == lines)
			flushline(0);
		tmp = screen[scrtop];
		for (j = scrtop; j < scrend - 1; j++)
			screen[j] = screen[j + 1];
		for (j = 0; j < cols; j++)
			tmp[j] = SPACE;
		screen[scrend - 1] = tmp;
	}

}

void
scrolldown(n)
int	n;
{
	int	i, j;
	cell	*tmp;

	if (verbose)
		flush();
	for (i = 0; i < n; i++) {
		tmp = screen[scrend - 1];
		for (j = scrend - 1; j > scrtop; j--)
			screen[j] = screen[j - 1];
		for (j = 0; j < cols; j++)
			tmp[j] = SPACE;
		screen[scrtop] = tmp;
	}

}

void
clearscr()
{
	int	i, j;

	for (i = 0; i < lines; i++)
		for (j = 0; j < cols; j++)
			screen[i][j] = SPACE;

}

void
sepline()
{
	int	i;

	for (i = 0; i < cols - 1; i++)
		putchar('-');
	putchar('\n');

}

void
flush()
{
	int	i;

	for (i = 0; i < lines; i++)
		flushline(i);
	sepline();

}

void
flushline(n)
int	n;
{
	int	c, j, k, knj;
	int	rev, newrev, ul, newul;
	cell	cl;

	rev = ul = FALSE;
	knj = M_ASCII;
	k = cols;
	while (k > 0 && screen[n][k - 1] == SPACE)
		k--;
	for (j = 0; j < k; j++) {
		cl = screen[n][j];
		c = cl & C_MASK;
		if (keepgr) {
			newrev = (cl & A_REV) != 0;
			newul = (cl & A_UL) != 0;
			if (rev && !newrev || ul && !newul)
				printf("\033[m");
			if (!rev && newrev)
				printf("\033[7m");
			if (!ul && newul)
				printf("\033[4m");
			rev = newrev;
			ul = newul;
		}
		if (cl & A_GRAPH) {
			if (kcode == KC_JIS && knj != M_ASCII) {
				printf("\033(%c", ksela);
				knj = M_ASCII;
			}
			switch (c) {
			case 'j': /* bottom right corner */
			case 'k': /* top right corner */
			case 'l': /* top left corner */
			case 'm': /* bottom left corner */
			case 'n': /* cross */
			case 't': /* left head T */
			case 'u': /* right head T */
			case 'v': /* bottom head T */
			case 'w': /* top head T */
				c = '+';
				break;
			case 'q': /* horizontal line */
				c = '-';
				break;
			case 'x': /* vertical line */
				c = '|';
				break;
			case ' ': /* space */
				c = ' ';
				break;
			default:
				c = '?';
				break;
			}
			putchar(c);
		} else if (cl & A_KNJ1) {
			switch (kcode) {
			case KC_JIS:
				if (knj != M_KANJI) {
					printf("\033$%c", kselk);
					knj = M_KANJI;
				}
				putchar((c >> 8) & 0x7f);
				putchar(c & 0x7f);
				break;
			case KC_NONE:
			case KC_EUC:
				putchar((c >> 8) & 0x7f | 0x80);
				putchar(c & 0x7f | 0x80);
				break;
			case KC_SJIS:
				putchar((c >> 8) & 0xff);
				putchar(c & 0xff);
				break;
			}
		} else if (cl & A_KNJ2) {
			/* do nothing */
		} else if (c & 0x80) { /* kana */
			switch (kcode) {
			case KC_JIS:
				if (knj != M_KANA) {
					printf("\033(I");
					knj = M_KANA;
				}
				putchar(c & 0x7f);
				break;
			case KC_NONE:
			case KC_EUC:
				putchar(SS2);
				putchar(c & 0x7f | 0x80);
			case KC_SJIS:
				putchar(c & 0xff);
				break;
			}
		} else { /* ascii */
			if (kcode == KC_JIS && knj != M_ASCII) {
				printf("\033(%c", ksela);
				knj = M_ASCII;
			}
			putchar(c & 0x7f);
		}
	}
	if (knj != M_ASCII)
		printf("\033(%c", ksela);
	if (keepgr && (rev || ul))
		printf("\033[m");
	putchar('\n');
}
