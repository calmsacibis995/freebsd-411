/*-
 * Copyright (c) 1999 Andrzej Bialecki <abial@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/kget/kget.c,v 1.4.2.2 2001/08/01 08:19:51 obrien Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <machine/uc_device.h>

struct uc_device *id;
char *p;

int
main(int argc, char *argv[])
{
	int len,i;
	char *buf;
	char *mib1="machdep.uc_devlist";
	char name[9];
	FILE *fout;

	if(argc<2) {
		fout=stdout;
	} else {
		if(strcmp(argv[1],"-")==0) {
			fout=stdout;
		} else {
			fout=fopen(argv[1],"w");
			if(fout==NULL) {
				perror("opening output file");
				exit(1);
			}
		}
	}

	/* We use sysctlbyname, because the oid is unknown (OID_AUTO) */

	/* Print the changes made to ISA devices */
	/* get the buffer size */
	i=sysctlbyname(mib1,NULL,&len,NULL,NULL);
	if(i) {
		perror("buffer sizing");
		exit(-1);
	}
	buf=(char *)malloc(len*sizeof(char));
	if (buf==NULL) {
		perror("malloc");
		exit(-1);
	}
	i=sysctlbyname(mib1,buf,&len,NULL,NULL);
	if(i) {
		perror("retrieving data");
		exit(-1);
	}
	i=0;
	while(i<len) {
		id=(struct uc_device *)(buf+i);
		p=(buf+i+sizeof(struct uc_device));
		strncpy(name,p,8);
		if(!id->id_enabled) {
			fprintf(fout,"disable %s%d\n",name,id->id_unit);
		} else {
			fprintf(fout,"enable %s%d\n",name,id->id_unit);
			if(id->id_iobase>0) {
				fprintf(fout,"port %s%d %#x\n",name,id->id_unit,
					id->id_iobase);
			}
			if(id->id_irq>0) {
				fprintf(fout,"irq %s%d %d\n",name,id->id_unit,
					ffs(id->id_irq)-1);
			}
			if(id->id_drq>0) {
				fprintf(fout,"drq %s%d %d\n",name,id->id_unit,
					id->id_drq);
			}
			if(id->id_maddr>0) {
				fprintf(fout,"iomem %s%d %#x\n",name,id->id_unit,
					id->id_maddr);
			}
			if(id->id_msize>0) {
				fprintf(fout,"iosize %s%d %d\n",name,id->id_unit,
					id->id_msize);
			}
			fprintf(fout,"flags %s%d %#x\n",name,id->id_unit,
				id->id_flags);
		}
		i+=sizeof(struct uc_device)+8;
	}
	free(buf);
	fprintf(fout,"quit\n");
	fclose(fout);
	exit(0);
}
