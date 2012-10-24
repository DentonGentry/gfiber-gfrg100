/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Modified Fri Mar 10 20:27:19 1995, faith@cs.unc.edu, for Linux
 * Modified Mon Jul  1 18:14:10 1996, janl@ifi.uio.no, writing to stdout
 *	as suggested by Michael Meskes <meskes@Informatik.RWTH-Aachen.DE>
 *
 * 1999-02-22 Arkadiusz Mi�kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 * 
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "errs.h"
#include "nls.h"

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct stat sb;
	char *tty;
	int ch;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);


	while ((ch = getopt(argc, argv, "")) != EOF)
		switch (ch) {
		case '?':
		default:
			goto usage;
		}
	argc -= optind;
	argv += optind;

	if ((tty = ttyname(STDERR_FILENO)) == NULL)
		err(1, "ttyname");
	if (stat(tty, &sb) < 0)
		err(1, "%s", tty);

	if (*argv == NULL) {
		if (sb.st_mode & (S_IWGRP | S_IWOTH)) {
			(void)fprintf(stdout, _("is y\n"));
			exit(0);
		}
		(void)fprintf(stdout, _("is n\n"));
		exit(1);
	}

	switch (*argv[0]) {
	case 'y':
#ifdef USE_TTY_GROUP
		if (chmod(tty, sb.st_mode | S_IWGRP) < 0)
			err(1, "%s", tty);
#else
		if (chmod(tty, sb.st_mode | S_IWGRP | S_IWOTH) < 0)
			err(1, "%s", tty);
#endif
		exit(0);
	case 'n':
		if (chmod(tty, sb.st_mode & ~(S_IWGRP|S_IWOTH)) < 0)
			err(1, "%s", tty);
		exit(1);
	}

usage:	(void)fprintf(stderr, _("usage: mesg [y | n]\n"));
	exit(2);
}