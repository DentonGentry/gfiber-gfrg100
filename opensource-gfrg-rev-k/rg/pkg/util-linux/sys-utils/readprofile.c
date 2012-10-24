/*
 *  readprofile.c - used to read /proc/profile
 *
 *  Copyright (C) 1994,1996 Alessandro Rubini (rubini@ipvvis.unipv.it)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * 1999-02-22 Arkadiusz Mi�kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 * 1999-09-01 Stephane Eranian <eranian@cello.hpl.hp.com>
 * - 64bit clean patch
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "nls.h"

#define RELEASE "2.0, May 1996"

#define S_LEN 128

static char *prgname;

/* These are the defaults */
static char defaultmap[]="/usr/src/linux/System.map";
static char defaultpro[]="/proc/profile";
static char optstring[]="m:p:itvarV";

static void
usage(void) {
  fprintf(stderr,
		  _("%s: Usage: \"%s [options]\n"
		  "\t -m <mapfile>  (default = \"%s\")\n"
		  "\t -p <pro-file> (default = \"%s\")\n"
		  "\t -i            print only info about the sampling step\n"
		  "\t -v            print verbose data\n"
		  "\t -a            print all symbols, even if count is 0\n"
		  "\t -r            reset all the counters (root only)\n"
		  "\t -V            print version and exit\n")
		  ,prgname,prgname,defaultmap,defaultpro);
  exit(1);
}

static FILE *
myopen(char *name, char *mode, int *flag) {
  static char cmdline[S_LEN];

  if (!strcmp(name+strlen(name)-3,".gz"))
    {
    *flag=1;
    sprintf(cmdline,"zcat %s", name);
    return popen(cmdline,mode);
    }
  *flag=0;
  return fopen(name,mode);
}

int
main (int argc, char **argv) {
  FILE *pro;
  FILE *map;
  int proFd;
  char *mapFile, *proFile;
  unsigned long len=0, add0=0, index=0;
  unsigned int step;
  unsigned int *buf, total, fn_len;
  unsigned long fn_add, next_add;           /* current and next address */
  char fn_name[S_LEN], next_name[S_LEN];   /* current and next name */
  char mode[8];
  int c;
  int optAll=0, optInfo=0, optReset=0, optVerbose=0;
  char mapline[S_LEN];
  int maplineno=1;
  int popenMap;   /* flag to tell if popen() has been used */

#define next (current^1)

#ifdef CONFIG_GLIBC
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif

  prgname=argv[0];
  proFile=defaultpro;
  mapFile=defaultmap;

  while ((c=getopt(argc,argv,optstring))!=-1)
    {
    switch(c)
      {
      case 'm': mapFile=optarg; break;
      case 'p': proFile=optarg; break;
      case 'a': optAll++;       break;
      case 'i': optInfo++;      break;
      case 'r': optReset++;     break;
      case 'v': optVerbose++;   break;
      case 'V': printf(_("%s Version %s\n"),prgname,RELEASE); exit(0);
      default: usage();
      }
    }

  if (optReset)
    {
    /* try to become root, just in case */
    setuid(0);
    pro=fopen(defaultpro,"w");
    if (!pro)
      {perror(proFile); exit(1);}
    fprintf(pro,_("anything\n"));
    fclose(pro);
    exit(0);
    }

  /*
   * Use an fd for the profiling buffer, to skip stdio overhead
   */
  if ( ((proFd=open(proFile,O_RDONLY)) < 0)
      || ((int)(len=lseek(proFd,0,SEEK_END)) < 0)
      || (lseek(proFd,0,SEEK_SET)<0) )
    {
    fprintf(stderr,"%s: %s: %s\n",prgname,proFile,strerror(errno));
    exit(1);
    }

  if ( !(buf=malloc(len)) )
    { fprintf(stderr,"%s: malloc(): %s\n",prgname, strerror(errno)); exit(1); }

  if (read(proFd,buf,len) != len)
    {
    fprintf(stderr,"%s: %s: %s\n",prgname,proFile,strerror(errno));
    exit(1);
    }
  close(proFd);

  step=buf[0];
  if (optInfo)
    {
    printf(_("Sampling_step: %i\n"),step);
    exit(0);
    } 

  total=0;

  if (!(map=myopen(mapFile,"r",&popenMap)))
    {fprintf(stderr,"%s: ",prgname);perror(mapFile);exit(1);}

  while(fgets(mapline,S_LEN,map))
    {
    if (sscanf(mapline,"%lx %s %s",&fn_add,mode,fn_name)!=3)
      {
      fprintf(stderr,_("%s: %s(%i): wrong map line\n"),
	      prgname,mapFile, maplineno);
      exit(1);
      }
    if (!strcmp(fn_name,"_stext")) /* only elf works like this */
      {
      add0=fn_add;
      break;
      }
    }

  if (!add0)
    {
    fprintf(stderr,_("%s: can't find \"_stext\" in %s\n"),prgname, mapFile);
    exit(1);
    }

  /*
   * Main loop.
   */
  while(fgets(mapline,S_LEN,map))
    {
    unsigned int this=0;

    if (sscanf(mapline,"%lx %s %s",&next_add,mode,next_name)!=3)
      {
      fprintf(stderr,_("%s: %s(%i): wrong map line\n"),
	      prgname,mapFile, maplineno);
      exit(1);
      }
    /* ignore any LEADING (before a '[tT]' symbol is found) Absolute symbols */
    if (*mode == 'A' && total == 0) continue;
    if (*mode!='T' && *mode!='t') break; /* only text is profiled */

    while (index < (next_add-add0)/step)
      this += buf[index++];
    total += this;

    fn_len = next_add-fn_add;
    if (fn_len && (this || optAll))
      {
      if (optVerbose)
	printf("%08lx %-40s %6i %8.4f\n",
	       fn_add,fn_name,this,this/(double)fn_len);
      else
	printf("%6i %-40s %8.4f\n",
	       this,fn_name,this/(double)fn_len);
      }
    fn_add=next_add; strcpy(fn_name,next_name);
    }
  /* trailer */
  if (optVerbose)
    printf("%08x %-40s %6i %8.4f\n",
	   0,"total",total,total/(double)(fn_add-add0));
  else
    printf("%6i %-40s %8.4f\n",
	   total,_("total"),total/(double)(fn_add-add0));
	
  popenMap ? pclose(map) : fclose(map);
  exit(0);
}



