/*
 * wmmp3 - An mpg123 frontend designed for WindowMaker
 *
 * wmmp3 is Copyright (c) 1999 by Munechika SUMIKAWA and licensed
 * through the GNU General Public License.  Read the COPYING file for
 * the complete GNU license.
 *
 * $Id$
 */

#define NORMSIZE    64
#define ASTEPSIZE   56
#define NAME        "wmmp3"
#define CLASS       "WMMP3"

#define PLAYLIST    ".wmmp3"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <net/if.h>
#include <err.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>

/* obsolete define for joing multicast group */
#ifndef IPV6_JOIN_GROUP
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif

/* pixmapas */
Pixmap pm_main;
Pixmap pm_tile;
Pixmap pm_disp;
Pixmap pm_mask;
Pixmap pm_icon;
Pixmap pm_nrec;
Pixmap pm_alnm;

/* xpm image */
#include "XPM/wmmp3.xpm"
#include "XPM/tile.xpm"
#include "XPM/norec.xpm"
#include "XPM/alpnum.xpm"	/* alphabet & number */

/* options */
int wmaker = 0;
int ushape = 0;
int astep = 0;
int debug = 0;

/*  Variables for command-line arguments - standard */
char mcastif[256] = "";
char servaddr[256] = "";
char servport[256] = "";
char titleport[256] = "";
char display[256] = "";
char position[256] = "";
int winsize;

/* X-Windows */
Atom _XA_GNUSTEP_WM_FUNC;
Atom deleteWin;
Display *d_display;
Window w_icon;
Window w_main;
Window w_root;
Window w_activewin;

GC gc_gc;

#define MAXPLAYNUM 1024
char *namelist[MAXPLAYNUM];
int playlist[MAXPLAYNUM];
int playnum = 0;
int curplay = 1;
pid_t child_pid;
char curname[8];
char title[30];

#define BUFSIZE 8192
char buf[BUFSIZE];
int fdmp3 = -1, fdtitle = -1;
int pipefds[2];

#define NEXT	1
#define STOP	2
#define PLAY	4
#define PREV	8
#define REPEAT	16
#define SHUFFLE	32
#define UDP	64		/* file or network */
#define TITLE	128
#define TCP	256
#define ERROR	512

int btnstate = STOP;		/* button state */
int status = STOP;		/* playing status */

/* functions */
void initXWin(int, char **);
void freeXWin();
void createWin(Window *, int, int);
unsigned long getColor(char *);
unsigned long mixColor(char *, int, char *, int);

void scanArgs(int, char **);
void readFile();
void pressEvent(XButtonEvent *);
void releaseEvent(XButtonEvent *);
void repaint();
void update();
void drawBtns(int);
void drawBtn(int, int, int, int, int);

int open_mpg123(void);
void open_music(int, int);
int open_socket(char *, char *, int);
void makecurname(int);
int get_title(void);
void recv_title(void);
void sig_child(int);
int get_url(char *, char *, char *, char *);
void get_head(void);
void next_frame(void);
void stop2play(void);
void play2stop(void);

int 
main(int argc, char **argv)
{
	int count = 0;
	XGCValues gcv;
	unsigned long gcm;
	XpmAttributes xpmattr;
	XEvent xev;
	int done, n;

	scanArgs(argc, argv);
	initXWin(argc, argv);

	gcm = GCGraphicsExposures;
	gcv.graphics_exposures = 0;
	gc_gc = XCreateGC(d_display, w_root, gcm, &gcv);

	xpmattr.numsymbols = 4;
	xpmattr.exactColors = 0;
	xpmattr.closeness = 40000;
	xpmattr.valuemask = XpmExactColors | XpmCloseness;
	XpmCreatePixmapFromData(d_display, w_root, wmmp3_xpm, &pm_main,
				&pm_mask, &xpmattr);
	XpmCreatePixmapFromData(d_display, w_root, tile_xpm, &pm_tile, NULL,
				&xpmattr);
	XpmCreatePixmapFromData(d_display, w_root, norec_xpm, &pm_nrec, NULL,
				&xpmattr);
	XpmCreatePixmapFromData(d_display, w_root, alpnum_xpm, &pm_alnm, NULL,
				&xpmattr);
	pm_disp = XCreatePixmap(d_display, w_root, 64, 64,
				DefaultDepth(d_display,
					     DefaultScreen(d_display)));

	if (wmaker || ushape || astep)
		XShapeCombineMask(d_display, w_activewin, ShapeBounding,
				  winsize / 2 - 32, winsize / 2 - 32, pm_mask,
				  ShapeSet);
	else
		XCopyArea(d_display, pm_tile, pm_disp, gc_gc, 0, 0, 64, 64,
			  0, 0);

	XSetClipMask(d_display, gc_gc, pm_mask);
	XCopyArea(d_display, pm_main, pm_disp, gc_gc, 0, 0, 64, 64, 0, 0);
	XSetClipMask(d_display, gc_gc, None);

	drawBtns(STOP);
	srandomdev();
	readFile();

	open_music(1, 0);
	makecurname(1);

	XSelectInput(d_display, w_activewin, ExposureMask | ButtonPressMask |
		     ButtonReleaseMask);
	XMapWindow(d_display, w_main);

	done = 0;
	while (!done) {
		while (XPending(d_display)) {
			XNextEvent(d_display, &xev);
			switch (xev.type) {
			case Expose:
				repaint();
				break;
			case ButtonPress:
				pressEvent(&xev.xbutton);
				break;
			case ButtonRelease:
				releaseEvent(&xev.xbutton);
				break;
			case ClientMessage:
				if (xev.xclient.data.l[0] == deleteWin)
					done = 1;
				break;
			}
		}

		if ((status & STOP) || fdmp3 == -1)
			goto jump;

		/* read data */
		if (status & (UDP | TCP)) {
			struct timeval tv;
			fd_set readfds;
			int select_val;
			int nfds = fdmp3;
			
			tv.tv_sec = 0;
			tv.tv_usec = 50000;

			FD_ZERO(&readfds);
			FD_SET(fdmp3, &readfds);
			if (status & TITLE && fdtitle != -1) {
				FD_SET(fdtitle, &readfds);
				if (fdtitle > nfds)
					nfds = fdtitle;
			}
			nfds++;
			select_val = select(nfds, &readfds, NULL, NULL, &tv);
			switch (select_val) {
			case -1:
				/* error */
				if ((errno != EAGAIN) && (errno != EINTR)) {
					strcpy(title, "Error in read");
					makecurname(1);
					play2stop();
				}
				goto jump;
			case 0:
				/* time out */
				goto alreadywait;
			default:
				if (status & TITLE &&
				    fdtitle != -1 &&
				    FD_ISSET(fdtitle, &readfds))
					recv_title();
				if (fdmp3 != -1 &&
				    FD_ISSET(fdmp3, &readfds))
					n = recv(fdmp3, buf, sizeof(buf), 0);
				break;
			}
		} else {
			n = read(fdmp3, buf, sizeof(buf));
		}

		/* write data */
		switch (n) {
		case -1:
			if ((errno != EAGAIN) && (errno != EINTR)) {
				strcpy(title, "Error in read");
				makecurname(1);
				play2stop();
			}
			break;
		case 0:
			/* EOF */
			curplay++;
			if (curplay >= playnum)
				curplay = 1;
			open_music(curplay, 1);
			makecurname(1);
			break;
		default:
			write(pipefds[1], buf, n);
		}

	jump:
		usleep(50000);

	alreadywait:
		if (++count > 4) {
			makecurname(0);
			count = 0;
		}

		XFlush(d_display);
	}
	XFreeGC(d_display, gc_gc);
	XFreePixmap(d_display, pm_main);
	XFreePixmap(d_display, pm_tile);
	XFreePixmap(d_display, pm_disp);
	XFreePixmap(d_display, pm_mask);
	XFreePixmap(d_display, pm_nrec);
	XFreePixmap(d_display, pm_alnm);
	freeXWin();
	return 0;
}

void 
initXWin(int argc, char **argv)
{
	XWMHints wmhints;
	XSizeHints shints;
	int pos;

	winsize = astep ? ASTEPSIZE : NORMSIZE;

	if ((d_display = XOpenDisplay(display)) == NULL) {
		fprintf(stderr, "%s : Unable to open X display '%s'.\n", NAME,
			XDisplayName(display));
		exit(1);
	}
	_XA_GNUSTEP_WM_FUNC = XInternAtom(d_display, "_GNUSTEP_WM_FUNCTION",
					  0);
	deleteWin = XInternAtom(d_display, "WM_DELETE_WINDOW", 0);

	w_root = DefaultRootWindow(d_display);

	shints.x = 0;
	shints.y = 0;
	shints.flags = 0;
	pos = (XWMGeometry(d_display, DefaultScreen(d_display), position,
			   NULL, 0, &shints, &shints.x, &shints.y,
			   &shints.width, &shints.height,
			   &shints.win_gravity) & (XValue | YValue));
	shints.min_width = winsize;
	shints.min_height = winsize;
	shints.max_width = winsize;
	shints.max_height = winsize;
	shints.base_width = winsize;
	shints.base_height = winsize;
	shints.flags = PMinSize | PMaxSize | PBaseSize;

	createWin(&w_main, shints.x, shints.y);

	if (wmaker || astep || pos)
		shints.flags |= USPosition;
	if (wmaker) {
		wmhints.initial_state = WithdrawnState;
		wmhints.flags = WindowGroupHint | StateHint | IconWindowHint;
		createWin(&w_icon, shints.x, shints.y);
		w_activewin = w_icon;
		wmhints.icon_window = w_icon;
	} else {
		wmhints.initial_state = NormalState;
		wmhints.flags = WindowGroupHint | StateHint;
		w_activewin = w_main;
	}
	wmhints.window_group = w_main;
	XSetWMHints(d_display, w_main, &wmhints);
	XSetWMNormalHints(d_display, w_main, &shints);
	XSetCommand(d_display, w_main, argv, argc);
	XStoreName(d_display, w_main, NAME);
	XSetIconName(d_display, w_main, NAME);
	XSetWMProtocols(d_display, w_activewin, &deleteWin, 1);
}

void 
freeXWin()
{
	XDestroyWindow(d_display, w_main);
	if (wmaker)
		XDestroyWindow(d_display, w_icon);
	XCloseDisplay(d_display);
}

void 
createWin(Window * win, int x, int y)
{
	XClassHint classHint;
	*win = XCreateSimpleWindow(d_display, w_root, x, y, winsize,
				   winsize, 0, 0, 0);
	classHint.res_name = NAME;
	classHint.res_class = CLASS;
	XSetClassHint(d_display, *win, &classHint);
}

unsigned long 
getColor(char *colorname)
{
	XColor color;
	XWindowAttributes winattr;

	XGetWindowAttributes(d_display, w_root, &winattr);
	color.pixel = 0;
	XParseColor(d_display, winattr.colormap, colorname, &color);
	color.flags = DoRed | DoGreen | DoBlue;
	XAllocColor(d_display, winattr.colormap, &color);

	return color.pixel;
}

unsigned long 
mixColor(char *colorname1, int prop1, char *colorname2, int prop2)
{
	XColor color, color1, color2;
	XWindowAttributes winattr;

	XGetWindowAttributes(d_display, w_root, &winattr);
	XParseColor(d_display, winattr.colormap, colorname1, &color1);
	XParseColor(d_display, winattr.colormap, colorname2, &color2);
	color.pixel = 0;
	color.red = (color1.red * prop1 + color2.red * prop2) /
		(prop1 + prop2);
	color.green = (color1.green * prop1 + color2.green * prop2) /
		(prop1 + prop2);
	color.blue = (color1.blue * prop1 + color2.blue * prop2) /
		(prop1 + prop2);
	color.flags = DoRed | DoGreen | DoBlue;
	XAllocColor(d_display, winattr.colormap, &color);

	return color.pixel;
}

void usage(char *prog)
{
#define F(x) fprintf(stderr, x)
#define G(x, y) fprintf(stderr, x, y)
	F("wmmp3 - An mpg123 frontend designed for WindowMaker\n");
	F("   Version 0.1 on $Date$\n");
	F("   Copyright (c) 1999 by Munechika SUMIKAWA <sumikawa@kame.net>\n");
	G("usage:\n   %s [options]\noptions:\n", prog);
	F("   -h | -help | --help  display this help screen\n");
	F("   -w                   use WithdrawnState    (for WindowMaker)\n");
	F("   -s                   shaped window\n");
	F("   -a                   use smaller window    (for AfterStep Wharf)\n");
	F("   -position position   set window position   (see X manual pages)\n");
	F("   -display display     select target display (see X manual pages)\n");
	F("   -D                   debug mode\n");
#undef F
#undef G
}

void 
scanArgs(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "-help") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage(rindex(argv[0], '/') + 1);
			exit(0);
		}
		if (strcmp(argv[i], "-w") == 0) wmaker = 1;
		if (strcmp(argv[i], "-s") == 0)	ushape = 1;
		if (strcmp(argv[i], "-a") == 0)	astep = 1;
		if (strcmp(argv[i], "-D") == 0)	debug = 1;
		if (strcmp(argv[i], "-u") == 0) {
			if (i < argc - 1) {
				i++;
				sprintf(mcastif, "%s", argv[i]);
			}
			continue;
		}
		if (strcmp(argv[i], "-position") == 0) {
			if (i < argc - 1) {
				i++;
				sprintf(position, "%s", argv[i]);
			}
			continue;
		}
		if (strcmp(argv[i], "-display") == 0) {
			if (i < argc - 1) {
				i++;
				sprintf(display, "%s", argv[i]);
			}
			continue;
		}
	}
}

void 
readFile()
{
	FILE *rcfile;
	char rcfilen[256];
	char buf[256];
	int current = 1;
	int i;

	sprintf(rcfilen, "%s/%s", getenv("HOME"), PLAYLIST);

	if ((rcfile = fopen(rcfilen, "r")) == NULL)
		exit(-1);

	while(1) {
		fgets(buf, 250, rcfile);
		if (buf[0] == '\0')
			continue;
		if (feof(rcfile))
			break;
		if (buf[0] == '#')
			continue;
		namelist[current] = (char *)malloc(strlen(buf));
		memcpy(buf + strlen(buf) - 1, "\0", 1);
		strcpy(namelist[current], buf);
		current++;
	}
	fclose(rcfile);
	playnum = current;

	if (status & SHUFFLE) {
		for (i = 1; i < playnum; i++)
			playlist[i] = 0;

		for (i = 1; i < playnum; i++) {
			int j;

			j = (random() % (playnum - 1)) + 1;
			while (playlist[j] != 0) {
				j++;
				if (j >= playnum)
					j = 1;
			}
			playlist[j] = i;

			if ((status & PLAY) && (curplay == i))
				curplay = j;
		}
		if (status & STOP)
			curplay = 1;
	} else {
		for (i = 1; i < playnum; i++)
			playlist[i] = i;
		if (status & PLAY)
			curplay = playlist[curplay];
		else
			curplay = 1;
	}
}

void 
pressEvent(XButtonEvent * xev)
{
	int oldstatus = status;
	
	int x = xev->x - (winsize / 2 - 32);
	int y = xev->y - (winsize / 2 - 32);
	if (x >= 6 && y >= 33 && x <= 18 && y <= 43) {
		/* prev */
		curplay--;
		if (curplay <= 0)
			curplay = playnum - 1;
		makecurname(1);
		open_music(curplay, 1);
		if (((oldstatus & (UDP | TCP)) && !(status & (UDP | TCP))) ||
		    (!(oldstatus & (UDP | TCP)) && (status & (UDP | TCP)))) {
			/* change buffer size */
			play2stop();
			stop2play();
		}
		btnstate |= PREV;
		drawBtns(PREV);
		btnstate &= ~PREV;
		return;
	}
	if (x >= 19 && y >= 33 && x <= 31 && y <= 43) {
		/* stop */
		if (status & PLAY)
			play2stop();
		if (status & TITLE) {
			strcpy(title, namelist[playlist[curplay]]);
			makecurname(1);
		}
		btnstate |= STOP;
		drawBtns(STOP);
		return;
	}
	if (x >= 32 && y >= 33 && x <= 44 && y <= 43) {
		/* play */
		stop2play();
		btnstate |= PLAY;
		drawBtns(PLAY);
		if (oldstatus & STOP && status & TCP)
			get_head();
		return;
	}
	if (x >= 45 && y >= 33 && x <= 57 && y <= 43) {
		/* next */
		curplay++;
		if (curplay >= playnum)
			curplay = 1;
		makecurname(1);
		open_music(curplay, 1);
		if (((oldstatus & (UDP | TCP)) && !(status & (UDP | TCP))) ||
		    (!(oldstatus & (UDP | TCP)) && (status & (UDP | TCP)))) {
			/* change buffer size */
			play2stop();
			stop2play();
		}
		btnstate |= NEXT;
		drawBtns(NEXT);
		btnstate &= ~NEXT;
		return;
	}
	if (x >=  6 && y >= 47 && x <= 29 && y <= 57) {
		/* repeat */
		if (status & REPEAT) {
			status &= ~REPEAT;
			btnstate &= ~REPEAT;
		} else {
			status |= REPEAT;
			btnstate |= REPEAT;
		}
		readFile();
		update();
		drawBtns(REPEAT);
		return;
	}
	if (x >= 35 && y >= 47 && x <= 58 && y <= 57) {
		/* shuffle */
		if (status & SHUFFLE) {
			status &= ~SHUFFLE;
			btnstate &= ~SHUFFLE;
		} else {
			status |= SHUFFLE;
			btnstate |= SHUFFLE;
		}
		readFile();
		update();
		drawBtns(SHUFFLE);
		return;
	}
}

void 
releaseEvent(XButtonEvent *xev)
{
	if (status & STOP) {
		btnstate &= ~PLAY;
		btnstate |= STOP;
	}
	if (status & PLAY) {
		btnstate &= ~STOP;
		btnstate |= PLAY;
	}
	drawBtns(PREV | STOP | PLAY | NEXT);
	repaint();
}

void 
repaint()
{
	XEvent xev;
	XCopyArea(d_display, pm_disp, w_activewin, gc_gc,
		  0, 0, 64, 64, winsize / 2 - 32, winsize / 2 - 32);
	while (XCheckTypedEvent(d_display, Expose, &xev));
}

void 
update()
{
	int i, x;

	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  ((playlist[curplay] / 1000) % 10) * 6, 0, 7, 9, 6, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  ((playlist[curplay] / 100) % 10) * 6,  0, 7, 9, 13, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  ((playlist[curplay] / 10) % 10) * 6,   0, 7, 9, 20, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  (playlist[curplay] % 10) * 6,          0, 7, 9, 27, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  10 * 6,                                0, 7, 9, 34, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  10 * 6,                                0, 7, 9, 41, 5);
	XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
		  10 * 6,                                0, 7, 9, 48, 5);

	for (i = 0; i < 7; i++) {
		x = i * 7 + 6;
		if (curname[i] >= '0' && curname[i] <= '9')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  (curname[i] - '0') * 6, 0,  7, 9, x, 13);
		else if (curname[i] >= 'a' && curname[i] <= 'z')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  (curname[i] - 'a') * 6, 10, 7, 9, x, 13);
		else if (curname[i] >= 'A' && curname[i] <= 'Z')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  (curname[i] - 'A') * 6, 10, 7, 9, x, 13);
		else if (curname[i] == '.')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  11 * 6,  0, 7, 9, x, 13);
		else if (curname[i] == ':')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  12 * 6,  0, 7, 9, x, 13);
		else if (curname[i] == '/')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  13 * 6,  0, 7, 9, x, 13);
		else if (curname[i] == '_')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  14 * 6,  0, 7, 9, x, 13);
		else if (curname[i] == '-')
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  15 * 6,  0, 7, 9, x, 13);
		else
			XCopyArea(d_display, pm_alnm, pm_disp, gc_gc,
				  10 * 6,  0,  7, 9, x, 13);
	}
}

void 
drawBtns(int btns)
{
	if (btns & PREV)
		drawBtn( 6, 33, 13, 11, (btnstate & PREV));
	if (btns & STOP)
		drawBtn(19, 33, 13, 11, (btnstate & STOP));
	if (btns & PLAY)
		drawBtn(32, 33, 13, 11, (btnstate & PLAY));
	if (btns & NEXT)
		drawBtn(45, 33, 13, 11, (btnstate & NEXT));
	if (btns & REPEAT)
		drawBtn( 5, 47, 24, 11, (btnstate & REPEAT));
	if (btns & SHUFFLE)
		drawBtn(35, 47, 24, 11, (btnstate & SHUFFLE));
}

void 
drawBtn(int x, int y, int w, int h, int down)
{
	if (!down)
		XCopyArea(d_display, pm_main, pm_disp, gc_gc,
			  x, y, w, h, x, y);
	else {
		XCopyArea(d_display, pm_main, pm_disp, gc_gc,
			  x, y, 1, h - 1, x + w - 1, y + 1);
		XCopyArea(d_display, pm_main, pm_disp, gc_gc,
			  x + w - 1, y + 1, 1, h - 1, x, y);
		XCopyArea(d_display, pm_main, pm_disp, gc_gc,
			  x, y, w - 1, 1, x + 1, y + h - 1);
		XCopyArea(d_display, pm_main, pm_disp, gc_gc,
			  x + 1, y + h - 1, w - 1, 1, x, y);
	}
	repaint();
}

void open_music(int num, int cl)
{
	int re;
	if (cl)
		close(fdmp3);

	if (strncmp(namelist[playlist[curplay]], "http://", 7) == 0) {
		status &= ~(UDP | TITLE);
		status |= TCP;
		strcpy(title, namelist[playlist[curplay]]);
		get_url(namelist[playlist[curplay]] + 7, servaddr, servport,
			NULL);
		fdmp3 = open_socket(servaddr, servport, SOCK_STREAM);
		if (fdmp3 == -1) {
			strcpy(title, "file not found");
			play2stop();
		}
	} else if (strncmp(namelist[playlist[curplay]], "udp://", 6) == 0) {
		status &= ~(TCP | TITLE);
		status |= UDP;
		strcpy(title, namelist[playlist[curplay]]);
		get_url(namelist[playlist[curplay]] + 6, servaddr, servport,
			NULL);
		fdmp3 = open_socket(servaddr, servport, SOCK_DGRAM);
		if (fdmp3 == -1)
			play2stop();
	} else if (strncmp(namelist[playlist[curplay]], "title://", 8) == 0) {
		status &= ~TCP;
		status |= (UDP | TITLE);
		strcpy(title, namelist[playlist[curplay]]);
		get_url(namelist[playlist[curplay]] + 8, servaddr, servport,
			titleport);
		fdmp3 = open_socket(servaddr, servport, SOCK_DGRAM);
		fdtitle = open_socket(servaddr, titleport, SOCK_DGRAM);
		if (fdmp3 == -1 || fdtitle == -1)
			play2stop();
	} else {
		/* file */
		status &= ~(UDP | TCP | TITLE);
		fdmp3 = open(namelist[playlist[curplay]], O_RDONLY, 0);
		if (fdmp3 == -1) {
			strcpy(title, "file not found");
			play2stop();
		} else
			get_title();
	}
	makecurname(1);
}

void
sig_child(int sig)
{
	int status;
	pid_t pid;
	int dummy;

	if (debug)
		fprintf(stderr, "sig_child()\n");

	pid = wait3(&status, WNOHANG, (struct rusage *)0);
	if (debug)
		fprintf(stderr, "*** status = %x\n", status);
	if (!WEXITSTATUS(status) && !WTERMSIG(status))
		return;
	fdmp3 = -1;
	sleep(2);
	open_mpg123();
	if (!(status & (UDP | TCP)) && fdmp3 != -1)
		lseek(fdmp3, 0, SEEK_SET);
}

int open_mpg123(void)
{
	if (pipe(pipefds) < 0) {
		perror("pipe");
		exit(1);
	}

	signal(SIGCHLD, SIG_DFL);
	child_pid = fork();
	if (child_pid == 0) {
		/* child process */
		close(0);
		if (!debug) {
			close(1);
			close(2);
		}
		dup(pipefds[0]);
		close(pipefds[0]);
		close(pipefds[1]);
		if (status & (UDP | TCP))
			execlp("mpg123", "mpg123", "-b", "100", "-", NULL);
		else
			execlp("mpg123", "mpg123", "-", NULL);
		perror("exec");
		exit(1);
	}

	/* parent process */
	if (child_pid == -1) {
		fprintf(stderr, "can't fork");
		exit(1);
	}
	close(pipefds[0]);
	signal(SIGCHLD, sig_child);

	if ((fdmp3 != -1) && (status & UDP))
		next_frame();

	return(0);
}

void makecurname(int newp)
{
	static int offset = 0;
	static int zerotimer = 0;
	strcpy(curname, "       ");

	if (newp) {
		offset = 0;
		zerotimer = 0;
	} else {
		if (offset == 0) {
			if (++zerotimer > 4) {
				offset++;
				zerotimer = 0;
			}
		} else
			offset++;
		if (offset >= strlen(title) || 7 >= strlen(title))
			offset = 0;
	}

	strncpy(curname, title + offset, 7);
	update();
	repaint();
}

int
get_title(void)
{
	FILE *file;
	int temp;
	struct {
		char tag[3];
		char title[30];
		char artist[30];
		char album[30];
		char year[4];
		char comment[30];
		unsigned char genre;
	} song;

	file = fopen(namelist[playlist[curplay]], "r");
	if (file == NULL)
		return(-1);
	fseek(file, -128, SEEK_END);
	temp = fread(&song, 128, 1, file);
	fclose(file);

	if (!strncmp(song.tag, "TAG", 3)) {
		int i = 0;
		for (i = 29; i >= 0; i--) {
			/* remove tail space */
			if (song.title[i] == ' ')
				song.title[i] = 0;
			else
				break;
		}
		if (song.title[i] == 0)
			goto filename;
		strncpy(title, song.title, 30);
		for (i = 0; i < 30; i++) {
			/* non-english? */
			if (song.title[i] == 0) {
				if (i == 0)
					goto filename;
				else
					break;
			}
			if (!isprint(song.title[i]))
				goto filename;
		}
		return (0);
	}

	filename:		
	strcpy(title, rindex(namelist[playlist[curplay]], '/') + 1);
	return (0);
}

void
recv_title(void)
{
	int temp;
	struct {
		char tag[3];
		char title[30];
		char artist[30];
		char album[30];
		char year[4];
		char comment[30];
		unsigned char genre;
	} song;
	char newtitle[256];
	int n;

	if (fdtitle == -1)
		return;

	n = recv(fdtitle, buf, sizeof(buf), 0);
	if (n < 128)
		return;
	memcpy(&song, buf, 128);
	
	{
		int i = 0;
		for (i = 29; i >= 0; i--) {
			/* remove tail space */
			if (song.title[i] == ' ')
				song.title[i] = 0;
			else
				break;
		}
		if (song.title[i] == 0)
			strcpy(title, "Unknown");
		strncpy(newtitle, song.title, 30);
		for (i = 0; i < 30; i++) {
			/* non-english? */
			if (song.title[i] == 0) {
				if (i == 0)
					strcpy(newtitle, "Unknown");
				else
					break;
			}
			if (!isprint(song.title[i]))
				strcpy(newtitle, "non-english");
		}
	}

	if (strcmp(newtitle, title) != 0) {
		strcpy(title, newtitle);
		makecurname(1);
	}
}

int
open_socket(char *addr, char *port, int socktype) {
	struct addrinfo hints, *res0, *res;
	int error;
	struct ipv6_mreq mreq;
	int intface;
	int recvsock;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = socktype;
	error = getaddrinfo(addr, port, &hints, &res0);
	if (error) {
		strcpy(title, "can't get address");
		return(-1);
	}

	for (res = res0; res; res = res->ai_next) {
		/* Create socket */
		if ((recvsock = socket(res->ai_family, res->ai_socktype,
				    res->ai_protocol)) < 0)
			continue;

		/* Connect to the server */
		switch (res->ai_socktype) {
		case SOCK_DGRAM:
			error = bind(recvsock, res->ai_addr, res->ai_addrlen);
			break;
		case SOCK_STREAM:
			error = connect(recvsock, res->ai_addr,
					res->ai_addrlen);
			break;
		default:
			strcpy(title, "unknown service");
			return(-1);
		}
		if (error < 0) {
			close(recvsock);
			recvsock = -1;
			continue;
		}

		if (res->ai_socktype == SOCK_DGRAM) {
			if (mcastif[0] == '\0') {
				strcpy(title,
				       "IF is not specified, use -u option");
				play2stop();
				return(-1);
			}

			intface = if_nametoindex(mcastif);
			mreq.ipv6mr_multiaddr =
				((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
			mreq.ipv6mr_interface = intface;
			if (setsockopt(recvsock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
				       &mreq, sizeof(mreq)) < 0) {
				strcpy(title, "can't setsockopt");
				return(-1);
			}
		}
		return(recvsock);
	}
	strcpy(title, "can't connect/bind");
	return(-1);
}


int
get_url(char *string, char *addr, char *port, char *port2)
{
	char tmp[256];
	char *mark;

	strcpy(tmp, string);

	if ((mark = strchr(tmp, '/')) != NULL)
		*mark = '\0';
		
	if ((mark = strchr(tmp, ':')) == NULL) {
		strcpy(port, "8000");
		if (port2 != NULL) {
			strcpy(port2, "8001");
		}
	} else {
		*mark = '\0';
		mark++;
		strcpy(port, mark);
		if (port2 != NULL) {
			if ((mark = strchr(mark, ':')) == NULL) {
				strcpy(port2, "8001");
			} else {
				*mark = '\0';
				mark++;
				strcpy(port2, mark);
			}
		}
	}
	strcpy(addr, tmp);

	if (debug)
		fprintf(stderr, "addr=%s, port1=%s, port2=%s\n",
			addr, port, port2);
	
	return(0);
}

int
strip_shout_header (char *head, int n)
{
	int i;
	for (i = 0; i < (n - 2); i++) {
		if (head[i] == 10 && head[i + 1] == 13)
			break;
		if (head[i] == '\n' && head[i + 1] == '\n')
			break;
	}
	head[i + 1] = '\0';
	memcpy(buf, head, n - (i + 1));
	return n - (i + 1);
}

int
strip_ice_header (char *head, int n)
{
	int i;
	for (i = 0; i < (n - 2); i++) {
		if ((head[i] == '\n') && (head[i + 1] == '\n'))
			break;
	}
	head[i + 1] = '\0';
	memcpy(buf, head, n - (i + 1));
	return n - (i + 1);
}

void get_head(void)
{
	int n = 0;
	char header[BUFSIZE];
	
	if (0)
		snprintf(buf, BUFSIZE, "GET http://%s:%s/ HTTP/1.0\r\nHOST: %s\r\nAccept: */*\r\n\r\n", servaddr, servport, servaddr);
	else {
		snprintf(buf, BUFSIZE, 
			 "GET / HTTP/1.0\r\nHost: %s\r\nAccept: */*\r\n\r\n", 
			 servaddr);
	}
	send(fdmp3, buf, strlen(buf), 0);

	/* Make sure we read LBUFSIZE before we go on */
	while (n != BUFSIZE) {
		int len;
		errno = 0;

		len = recv(fdmp3, header + n, BUFSIZE - n, 0);
		if (len <= 0 && errno != EAGAIN) {
			strcpy(title, "Error in read");
			makecurname(1);
			play2stop();
		}
		if (len > 0)
			n += len;
	}

	/*
	 * The server should reply with
	 * HTTP/1.0 200 OK\nServer: whatever/VERSION\n
	 * Content-type: audio/x-mpeg\n\n
	 */
	/*
	 * or, if it's shoutcast, it says:
	 * ICY 200 OK^M
	 * icy-notice1:<BR>This stream requires
	 * <a href="http://www.winamp.com/"> Winamp</a><BR>^M
	 * icy-notice2:SHOUTcast Distributed Network Audio Server/posix v1.0b<BR>^M
	 * icy-name:whatever^M
	 * icy-genre:whatever^M
	 * icy-url:whatever^M
	 * icy-pub:1^M
	 * icy-br:128^M
	 * ^M
	 */
	if (header[0] == 'H')
		n = strip_ice_header(header, n);
	else
		n = strip_shout_header(header, n);
	
	write(pipefds[1], buf, n);
}

void next_frame(void)
{
	int n = 0, pos = 0;
	
	while (n != BUFSIZE) {
		int len;
		errno = 0;

		len = recv(fdmp3, buf + n, BUFSIZE - n, 0);
		if (len <= 0 && errno != EAGAIN) {
			strcpy(title, "Error in read");
			makecurname(1);
			play2stop();
		}
		if (len > 0)
			n += len;
	}

	while (pos < BUFSIZE) {
		if ((buf[pos] & 0xff) == 0xff &&
		    (buf[pos + 1] & 0xf0) == 0xf0)
			break;
		pos++;
	}
  
	if (pos != BUFSIZE)
		write(pipefds[1], buf + pos, n - pos);
}

void
play2stop(void)
{
	status &= ~PLAY;
	status |= STOP;
	signal(SIGCHLD, SIG_DFL);
	close(pipefds[1]);
}
	
void
stop2play(void)
{
	status &= ~STOP;
	status |= PLAY;
	if (debug)
		fprintf(stderr, "fdtitle = %d, fdmp3 = %d\n", fdtitle, fdmp3);
	open_mpg123();
}
