/* $Id$ */

/* (non thread safe) helper to determine the terminal size */

/* Author: Till Straumann <strauman@slac.stanford.edu>, 2003/3 */

/* We first do a TIOCGWINSZ ioctl and if that fails,
 * we try ANSI escape sequences.
 * If any of these methods succeeds, we set the COLUMNS and LINES
 * environment variables (which are interpreted by Cexp/TECLA)
 */

/*
 * Copyright 2002,2003, Stanford University and
 * 		Till Straumann <strauman@@slac.stanford.edu>
 * 
 * Stanford Notice
 * ***************
 * 
 * Acknowledgement of sponsorship
 * * * * * * * * * * * * * * * * *
 * This software was produced by the Stanford Linear Accelerator Center,
 * Stanford University, under Contract DE-AC03-76SFO0515 with the Department
 * of Energy.
 * 
 * Government disclaimer of liability
 * - - - - - - - - - - - - - - - - -
 * Neither the United States nor the United States Department of Energy,
 * nor any of their employees, makes any warranty, express or implied,
 * or assumes any legal liability or responsibility for the accuracy,
 * completeness, or usefulness of any data, apparatus, product, or process
 * disclosed, or represents that its use would not infringe privately
 * owned rights.
 * 
 * Stanford disclaimer of liability
 * - - - - - - - - - - - - - - - - -
 * Stanford University makes no representations or warranties, express or
 * implied, nor assumes any liability for the use of this software.
 * 
 * This product is subject to the EPICS open license
 * - - - - - - - - - - - - - - - - - - - - - - - - - 
 * Consult the LICENSE file or http://www.aps.anl.gov/epics/license/open.php
 * for more information.
 * 
 * Maintenance of notice
 * - - - - - - - - - - -
 * In the interest of clarity regarding the origin and status of this
 * software, Stanford University requests that any recipient of it maintain
 * this notice affixed to any distribution by the recipient that contains a
 * copy or derivative of this software.
 */
#include <rtems.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

#define  ESC 27
#define  MAXSTR 20

#define PERROR(arg) if (infoLevel>0) perror(arg)

static int
chat(int fdi, int fdo, char *str, char *reply, int quiet)
{
int i,l;

	for (i=0, l=strlen(str); l > 0; ) {
		int put = write(fdo, str+i, l);
		if (put <=0 ) {
			if (!quiet)
				perror ("Unable to write ESC sequence");
			return -2;
		}
		i+=put;
		l-=put;
	}

	if (!reply)
		return 0;

	for (i=0; i < MAXSTR - 1 && 1==read(fdi,reply+i,1); i++) {
		if ('R' == reply[i]) {
			reply[++i] = 0;
			return 0;
		}
	}

	*reply = 0;
	return -1;
}

/* infoLevel: 0 suppress all error and success messages
 *            1 success messages and only error messages for fallback method (ANSI messages)
 *            2 all messages
 */


int
queryTerminalSize(int infoLevel)
{
struct termios	tios, tion;
struct winsize	win;
int				fdi,fdo;
char			cbuf[MAXSTR];
char			here[MAXSTR];
int				rval = 0xdeadbeef;
int				x,y;

	if ( (fdi=fileno(stdin))<0 || (fdo=fileno(stdout))<0 ) {
		PERROR("Unable to determine file descriptor");
		return -1;
	}

	win.ws_row = win.ws_col = -1;
	here[0]    = 0;

	if ( ioctl(fdi, TIOCGWINSZ, &win) ) {
		if (infoLevel>1) {
			perror("TIOCGWINSZ failed");
			fprintf(stderr,"Trying ANSI VT100 Escapes instead\n");
		}

		if ( tcgetattr(fdi, &tios) ) {
			PERROR("Unable to get terminal attributes");
			return -1;
		}

		tion = tios;
		tion.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
		                 |INLCR|IGNCR|ICRNL|IXON);
		tion.c_oflag &= ~OPOST;
		tion.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
		tion.c_cflag &= ~(CSIZE|PARENB);
		tion.c_cflag |= CS8;
		tion.c_cc[VMIN]  = 0;/* use timeout even when reading single chars */
		tion.c_cc[VTIME] = 3;/* .3 seconds */

		if ( tcsetattr(fdi, TCSADRAIN, &tion) ) {
			PERROR("Unable to make raw terminal");
			return -1;
		}

		sprintf(cbuf,"%c[6n",ESC);

		if ( chat(fdi,fdo,cbuf,here,infoLevel<1) || 'R'!=here[strlen(here)-1] ) {
			if (infoLevel>0)
				fprintf(stderr,"Invalid answer from terminal\n");
			here[0] = 0;
			goto restore;
		}
		here[strlen(here)-1] = 'H';

		/* try to move into the desert */
		sprintf(cbuf,"%c[998;998H",ESC);

		if (chat(fdi,fdo,cbuf,0,infoLevel<1))
			goto restore;

		/* see where we effectively are */
		sprintf(cbuf,"%c[6n",ESC);

		if (0==chat(fdi,fdo,cbuf,cbuf,infoLevel<1)) {
			if (2!=sscanf(cbuf+1,"[%d;%dR",&y,&x)) {
				if (infoLevel>0)
					fprintf(stderr,"Unable to parse anser: '%s'\n",cbuf+1);
				goto restore;
			}
			rval = 0;
		}


restore:
		/* restore the cursor */
		if (here[0])
			chat(fdi,fdo,here,0,infoLevel<1);

		if ( tcsetattr(fdi, TCSADRAIN, &tios) ) {
			PERROR("Oops, unable to restore terminal attributes");
			return -1;
		}
		if (rval)
			return rval;
	} else {
		x = win.ws_col;
		y = win.ws_row;
	}

	if (infoLevel>0)
		printf("Terminal size: %dx%d (COLUMNS x LINES; environment vars set)\n",
				x, y);

	sprintf(cbuf,"COLUMNS=%d",x);
	putenv(cbuf);
	sprintf(cbuf,"LINES=%d",y);
	putenv(cbuf);

	return 0;
}
