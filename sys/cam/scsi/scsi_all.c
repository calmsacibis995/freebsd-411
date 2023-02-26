/*
 * Implementation of Utility functions for all SCSI device types.
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * Copyright (c) 1997, 1998 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_all.c,v 1.14.2.11 2003/10/30 15:06:35 thomas Exp $
 */

#include <sys/param.h>

#ifdef _KERNEL
#include <opt_scsi.h>

#include <sys/systm.h>
#else
#include <errno.h>
#include <stdio.h>
#include <string.h>
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_periph.h>
#include <cam/scsi/scsi_all.h>
#ifndef _KERNEL
#include <camlib.h>

#ifndef FALSE
#define FALSE   0
#endif /* FALSE */
#ifndef TRUE
#define TRUE    1
#endif /* TRUE */
#define ERESTART        -1              /* restart syscall */
#define EJUSTRETURN     -2              /* don't modify regs, just return */
#endif /* !_KERNEL */

const char *scsi_sense_key_text[] = 
{
	"NO SENSE",
	"RECOVERED ERROR",
	"NOT READY",
	"MEDIUM ERROR",
	"HARDWARE FAILURE",
	"ILLEGAL REQUEST",
	"UNIT ATTENTION",
	"DATA PROTECT",
	"BLANK CHECK",
	"Vendor Specific",
	"COPY ABORTED",
	"ABORTED COMMAND",
	"EQUAL",
	"VOLUME OVERFLOW",
	"MISCOMPARE",
	"RESERVED"
};

#if !defined(SCSI_NO_OP_STRINGS)

#define D 0x001
#define T 0x002
#define L 0x004
#define P 0x008
#define W 0x010
#define R 0x020
#define S 0x040
#define O 0x080
#define M 0x100
#define C 0x200
#define A 0x400
#define E 0x800

#define ALL 0xFFF

/*
 * WARNING:  You must update the num_ops field below for this quirk table 
 * entry if you add more entries.
 */
static struct op_table_entry plextor_cd_ops[] = {
	{0xD8, R, "CD-DA READ"}
};

static struct scsi_op_quirk_entry scsi_op_quirk_table[] = {
	{
		/*
		 * I believe that 0xD8 is the Plextor proprietary command
		 * to read CD-DA data.  I'm not sure which Plextor CDROM
		 * models support the command, though.  I know for sure
		 * that the 4X, 8X, and 12X models do, and presumably the
		 * 12-20X does.  I don't know about any earlier models,
		 * though.  If anyone has any more complete information,
		 * feel free to change this quirk entry.
		 */
		{T_CDROM, SIP_MEDIA_REMOVABLE, "PLEXTOR", "CD-ROM PX*", "*"},
		1, /* number of vendor-specific opcodes for this entry */
		plextor_cd_ops
	}
};

static struct op_table_entry scsi_op_codes[] = {
/*
 * From: ftp://ftp.symbios.com/pub/standards/io/t10/drafts/spc/op-num.txt
 * Modifications by Kenneth Merry (ken@FreeBSD.ORG)
 *
 * Note:  order is important in this table, scsi_op_desc() currently
 * depends on the opcodes in the table being in order to save search time.
 */
/*  
 * File: OP-NUM.TXT
 *
 * SCSI Operation Codes
 * Numeric Sorted Listing
 * as of 11/13/96
 * 
 *     D - DIRECT ACCESS DEVICE (SBC)                    device column key
 *     .T - SEQUENTIAL ACCESS DEVICE (SSC)              -------------------
 *     . L - PRINTER DEVICE (SSC)                       M = Mandatory
 *     .  P - PROCESSOR DEVICE (SPC)                    O = Optional
 *     .  .W - WRITE ONCE READ MULTIPLE DEVICE (SBC)    V = Vendor specific
 *     .  . R - CD DEVICE (MMC)                         R = Reserved
 *     .  .  S - SCANNER DEVICE (SGC)                   Z = Obsolete
 *     .  .  .O - OPTICAL MEMORY DEVICE (SBC)
 *     .  .  . M - MEDIA CHANGER DEVICE (SMC)
 *     .  .  .  C - COMMUNICATION DEVICE (SSC)
 *     .  .  .  .A - STORAGE ARRAY DEVICE (SCC)
 *     .  .  .  . E - ENCLOSURE SERVICES DEVICE (SES)
 * OP  DTLPWRSOMCAE  Description
 * --  ------------  ---------------------------------------------------- */
/* 00  MMMMMMMMMMMM  TEST UNIT READY */
{0x00, ALL, 		"TEST UNIT READY"},

/* 01   M            REWIND */
{0x01, T,           "REWIND"},
/* 01  Z V ZO ZO     REZERO UNIT */
{0x01, D|L|W|O|M,   "REZERO UNIT"},

/* 02  VVVVVV  V   */

/* 03  MMMMMMMMMMMM  REQUEST SENSE */
{0x03, ALL,         "REQUEST SENSE"},

/* 04  M    O O      FORMAT UNIT */
{0x04, D|R|O,       "FORMAT UNIT"},
/* 04   O            FORMAT MEDIUM */
{0x04, T,           "FORMAT MEDIUM"},
/* 04    O           FORMAT */
{0x04, L,           "FORMAT"},

/* 05  VMVVVV  V     READ BLOCK LIMITS */
{0x05, T,           "READ BLOCK LIMITS"},

/* 06  VVVVVV  V   */

/* 07  OVV O  OV     REASSIGN BLOCKS */
{0x07, D|W|O,       "REASSIGN BLOCKS"},
/* 07          O     INITIALIZE ELEMENT STATUS */
{0x07, M,           "INITIALIZE ELEMENT STATUS"},

/* 08  OMV OO OV     READ(06) */
{0x08, D|T|W|R|O,   "READ(06)"},
/* 08     O          RECEIVE */
{0x08, P,           "RECEIVE"},
/* 08           M    GET MESSAGE(06) */
{0x08, C,           "GET MESSAGE(06)"},

/* 09  VVVVVV  V   */

/* 0A  OM  O  OV     WRITE(06) */
{0x0A, D|T|W|O, "WRITE(06)"},
/* 0A     M          SEND(06) */
{0x0A, P,           "SEND(06)"},
/* 0A           M    SEND MESSAGE(06) */
{0x0A, C,           "SEND MESSAGE(06)"},
/* 0A    M           PRINT */
{0x0A, L,           "PRINT"},

/* 0B  Z   ZO ZV     SEEK(06) */
{0x0B, D|W|R|O,     "SEEK(06)"},
/* 0B    O           SLEW AND PRINT */
{0x0B, L,           "SLEW AND PRINT"},

/* 0C  VVVVVV  V   */
/* 0D  VVVVVV  V   */
/* 0E  VVVVVV  V   */
/* 0F  VOVVVV  V     READ REVERSE */
{0x0F, T,           "READ REVERSE"},

/* 10  VM VVV        WRITE FILEMARKS */
{0x10, T,           "WRITE FILEMARKS"},
/* 10    O O         SYNCHRONIZE BUFFER */
{0x10, L|W,         "SYNCHRONIZE BUFFER"},

/* 11  VMVVVV        SPACE */
{0x11, T,           "SPACE"},

/* 12  MMMMMMMMMMMM  INQUIRY */
{0x12, ALL,         "INQUIRY"},

/* 13  VOVVVV        VERIFY(06) */
{0x13, T,           "VERIFY(06)"},

/* 14  VOOVVV        RECOVER BUFFERED DATA */
{0x14, T|L,         "RECOVER BUFFERED DATA"},

/* 15  OMO OOOOOOOO  MODE SELECT(06) */
{0x15, ALL & ~(P),    "MODE SELECT(06)"},

/* 16  MMMOMMMM   O  RESERVE(06) */
{0x16, D|T|L|P|W|R|S|O|E, "RESERVE(06)"},
/* 16          M     RESERVE ELEMENT(06) */
{0x16, M,           "RESERVE ELEMENT(06)"},

/* 17  MMMOMMMM   O  RELEASE(06) */
{0x17, ALL & ~(M|C|A), "RELEASE(06)"},
/* 17          M     RELEASE ELEMENT(06) */
{0x17, M,           "RELEASE ELEMENT(06)"},

/* 18  OOOOOOOO      COPY */
{0x18, ALL & ~(M|C|A|E), "COPY"},

/* 19  VMVVVV        ERASE */
{0x19, T,           "ERASE"},

/* 1A  OMO OOOOOOOO  MODE SENSE(06) */
{0x1A, ALL & ~(P),  "MODE SENSE(06)"},

/* 1B  O   OM O      STOP START UNIT */
{0x1B, D|W|R|O,     "STOP START UNIT"},
/* 1B   O            LOAD UNLOAD */
{0x1B, T,           "LOAD UNLOAD"},
/* 1B        O       SCAN */
{0x1B, S,           "SCAN"},
/* 1B    O           STOP PRINT */
{0x1B, L,           "STOP PRINT"},

/* 1C  OOOOOOOOOO M  RECEIVE DIAGNOSTIC RESULTS */
{0x1C, ALL & ~(A),  "RECEIVE DIAGNOSTIC RESULTS"},

/* 1D  MMMMMMMMMMMM  SEND DIAGNOSTIC */
{0x1D, ALL,         "SEND DIAGNOSTIC"},

/* 1E  OO  OM OO     PREVENT ALLOW MEDIUM REMOVAL */
{0x1E, D|T|W|R|O|M, "PREVENT ALLOW MEDIUM REMOVAL"},

/* 1F */
/* 20  V   VV V */
/* 21  V   VV V */
/* 22  V   VV V */
/* 23  V   VV V */

/* 24  V   VVM       SET WINDOW */
{0x24, S,           "SET WINDOW"},

/* 25  M   M  M      READ CAPACITY */
{0x25, D|W|O,       "READ CAPACITY"},
/* 25       M        READ CD RECORDED CAPACITY */
{0x25, R,           "READ CD RECORDED CAPACITY"},
/* 25        O       GET WINDOW */
{0x25, S,           "GET WINDOW"},

/* 26  V   VV */
/* 27  V   VV */

/* 28  M   MMMM      READ(10) */
{0x28, D|W|R|S|O,   "READ(10)"},
/* 28           O    GET MESSAGE(10) */
{0x28, C,           "GET MESSAGE(10)"},

/* 29  V   VV O      READ GENERATION */
{0x29, O,           "READ GENERATION"},

/* 2A  M   MM M      WRITE(10) */
{0x2A, D|W|R|O,     "WRITE(10)"},
/* 2A        O       SEND(10) */
{0x2A, S,           "SEND(10)"},
/* 2A           O    SEND MESSAGE(10) */
{0x2A, C,           "SEND MESSAGE(10)"},

/* 2B  O   OM O      SEEK(10) */
{0x2B, D|W|R|O,     "SEEK(10)"},
/* 2B   O            LOCATE */
{0x2B, T,           "LOCATE"},
/* 2B          O     POSITION TO ELEMENT */
{0x2B, M,           "POSITION TO ELEMENT"},

/* 2C  V      O      ERASE(10) */
{0x2C, O,           "ERASE(10)"},

/* 2D  V   O  O      READ UPDATED BLOCK */
{0x2D, W|O,         "READ UPDATED BLOCK"},

/* 2E  O   O  O      WRITE AND VERIFY(10) */
{0x2E, D|W|O,       "WRITE AND VERIFY(10)"},

/* 2F  O   OO O      VERIFY(10) */
{0x2F, D|W|R|O,     "VERIFY(10)"},

/* 30  Z   ZO Z      SEARCH DATA HIGH(10) */
{0x30, D|W|R|O,     "SEARCH DATA HIGH(10)"},

/* 31  Z   ZO Z      SEARCH DATA EQUAL(10) */
{0x31, D|W|R|O,     "SEARCH DATA EQUAL(10)"},
/* 31        O       OBJECT POSITION */
{0x31, S,           "OBJECT POSITION"},

/* 32  Z   ZO Z      SEARCH DATA LOW(10) */
{0x32, D|W|R|O,     "SEARCH DATA LOW(10"},

/* 33  O   OO O      SET LIMITS(10) */
{0x33, D|W|R|O,     "SET LIMITS(10)"},

/* 34  O   OO O      PRE-FETCH */
{0x34, D|W|R|O,     "PRE-FETCH"},
/* 34   O            READ POSITION */
{0x34, T,           "READ POSITION"},
/* 34        O       GET DATA BUFFER STATUS */
{0x34, S,           "GET DATA BUFFER STATUS"},

/* 35  O   OM O      SYNCHRONIZE CACHE */
{0x35, D|W|R|O,     "SYNCHRONIZE CACHE"},

/* 36  O   OO O      LOCK UNLOCK CACHE */
{0x36, D|W|R|O,     "LOCK UNLOCK CACHE"},

/* 37  O      O      READ DEFECT DATA(10) */
{0x37, D|O,         "READ DEFECT DATA(10)"},

/* 38      O  O      MEDIUM SCAN */
{0x38, W|O,         "MEDIUM SCAN"},

/* 39  OOOOOOOO      COMPARE */
{0x39, ALL & ~(M|C|A|E), "COMPARE"},

/* 3A  OOOOOOOO      COPY AND VERIFY */
{0x3A, ALL & ~(M|C|A|E), "COPY AND VERIFY"},

/* 3B  OOOOOOOOOO O  WRITE BUFFER */
{0x3B, ALL & ~(A),  "WRITE BUFFER"},

/* 3C  OOOOOOOOOO    READ BUFFER */
{0x3C, ALL & ~(A|E),"READ BUFFER"},

/* 3D      O  O      UPDATE BLOCK */
{0x3D, W|O,         "UPDATE BLOCK"},

/* 3E  O   OO O      READ LONG */
{0x3E, D|W|R|O,     "READ LONG"},

/* 3F  O   O  O      WRITE LONG */
{0x3F, D|W|O,       "WRITE LONG"},

/* 40  OOOOOOOOOO    CHANGE DEFINITION */
{0x40, ALL & ~(A|E),"CHANGE DEFINITION"},

/* 41  O             WRITE SAME */
{0x41, D,           "WRITE SAME"},

/* 42       M        READ SUB-CHANNEL */
{0x42, R,           "READ SUB-CHANNEL"}, 

/* 43       M        READ TOC/PMA/ATIP {MMC Proposed} */
{0x43, R,           "READ TOC/PMA/ATIP {MMC Proposed}"},

/* 44   M            REPORT DENSITY SUPPORT */
{0x44, T,           "REPORT DENSITY SUPPORT"},
/* 44       M        READ HEADER */
{0x44, R,           "READ HEADER"},

/* 45       O        PLAY AUDIO(10) */
{0x45, R,           "PLAY AUDIO(10)"},

/* 46 */

/* 47       O        PLAY AUDIO MSF */
{0x47, R,           "PLAY AUDIO MSF"},

/* 48       O        PLAY AUDIO TRACK INDEX */
{0x48, R,           "PLAY AUDIO TRACK INDEX"},

/* 49       O        PLAY TRACK RELATIVE(10) */
{0x49, R,           "PLAY TRACK RELATIVE(10)"},

/* 4A */

/* 4B       O        PAUSE/RESUME */
{0x4B, R,           "PAUSE/RESUME"},

/* 4C  OOOOOOOOOOO   LOG SELECT */
{0x4C, ALL & ~(E),  "LOG SELECT"},

/* 4D  OOOOOOOOOOO   LOG SENSE */
{0x4D, ALL & ~(E),  "LOG SENSE"},

/* 4E       O        STOP PLAY/SCAN {MMC Proposed} */
{0x4E, R,           "STOP PLAY/SCAN {MMC Proposed}"},

/* 4F */

/* 50  O             XDWRITE(10) */
{0x50, D,           "XDWRITE(10)"},

/* 51  O             XPWRITE(10) */
{0x51, D,           "XPWRITE(10)"},
/* 51       M        READ DISC INFORMATION {MMC Proposed} */
{0x51, R,           "READ DISC INFORMATION {MMC Proposed}"},

/* 52  O             XDREAD(10) */
{0x52, D,           "XDREAD(10)"},
/* 52       M        READ TRACK INFORMATION {MMC Proposed} */
{0x52, R,           "READ TRACK INFORMATION {MMC Proposed}"},

/* 53       M        RESERVE TRACK {MMC Proposed} */
{0x53, R,           "RESERVE TRACK {MMC Proposed}"},

/* 54       O        SEND OPC INFORMATION {MMC Proposed} */
{0x54, R,           "SEND OPC INFORMATION {MMC Proposed}"},

/* 55  OOO OOOOOOOO  MODE SELECT(10) */
{0x55, ALL & ~(P),  "MODE SELECT(10)"},

/* 56  MMMOMMMM   O  RESERVE(10) */
{0x56, ALL & ~(M|C|A), "RESERVE(10)"},
/* 56          M     RESERVE ELEMENT(10) */
{0x56, M,           "RESERVE ELEMENT(10)"},

/* 57  MMMOMMMM   O  RELEASE(10) */
{0x57, ALL & ~(M|C|A), "RELEASE(10"},
/* 57          M     RELEASE ELEMENT(10) */
{0x57, M,           "RELEASE ELEMENT(10)"},

/* 58       O        REPAIR TRACK {MMC Proposed} */
{0x58, R,           "REPAIR TRACK {MMC Proposed}"},

/* 59       O        READ MASTER CUE {MMC Proposed} */
{0x59, R,           "READ MASTER CUE {MMC Proposed}"},

/* 5A  OOO OOOOOOOO  MODE SENSE(10) */
{0x5A, ALL & ~(P),  "MODE SENSE(10)"},

/* 5B       M        CLOSE TRACK/SESSION {MMC Proposed} */
{0x5B, R,           "CLOSE TRACK/SESSION {MMC Proposed}"},

/* 5C       O        READ BUFFER CAPACITY {MMC Proposed} */
{0x5C, R,           "READ BUFFER CAPACITY {MMC Proposed}"},

/* 5D       O        SEND CUE SHEET {MMC Proposed} */
{0x5D, R,           "SEND CUE SHEET {MMC Proposed}"},

/* 5E  OOOOOOOOO  O  PERSISTENT RESERVE IN */
{0x5E, ALL & ~(C|A),"PERSISTENT RESERVE IN"},

/* 5F  OOOOOOOOO  O  PERSISTENT RESERVE OUT */
{0x5F, ALL & ~(C|A),"PERSISTENT RESERVE OUT"},

/* 80  O             XDWRITE EXTENDED(16) */
{0x80, D,           "XDWRITE EXTENDED(16)"},

/* 81  O             REBUILD(16) */
{0x81, D,           "REBUILD(16)"},

/* 82  O             REGENERATE(16) */
{0x82, D,           "REGENERATE(16)"},

/* 83 */
/* 84 */
/* 85 */
/* 86 */
/* 87 */
/* 88 */
/* 89 */
/* 8A */
/* 8B */
/* 8C */
/* 8D */
/* 8E */
/* 8F */
/* 90 */
/* 91 */
/* 92 */
/* 93 */
/* 94 */
/* 95 */
/* 96 */
/* 97 */
/* 98 */
/* 99 */
/* 9A */
/* 9B */
/* 9C */
/* 9D */
/* 9E */
/* 9F */

/* A0  OOOOOOOOOOO   REPORT LUNS */
{0xA0, ALL & ~(E),  "REPORT LUNS"},

/* A1       O        BLANK {MMC Proposed} */
{0xA1, R,           "BLANK {MMC Proposed}"},

/* A2       O        WRITE CD MSF {MMC Proposed} */
{0xA2, R,           "WRITE CD MSF {MMC Proposed}"},

/* A3            M   MAINTENANCE (IN) */
{0xA3, A,           "MAINTENANCE (IN)"},

/* A4            O   MAINTENANCE (OUT) */
{0xA4, A,           "MAINTENANCE (OUT)"},

/* A5   O      M     MOVE MEDIUM */
{0xA5, T|M,         "MOVE MEDIUM"},
/* A5       O        PLAY AUDIO(12) */
{0xA5, R,           "PLAY AUDIO(12)"},

/* A6          O     EXCHANGE MEDIUM */
{0xA6, M,           "EXCHANGE MEDIUM"},
/* A6       O        LOAD/UNLOAD CD {MMC Proposed} */
{0xA6, R,           "LOAD/UNLOAD CD {MMC Proposed}"},

/* A7  OO  OO OO     MOVE MEDIUM ATTACHED */
{0xA7, D|T|W|R|O|M, "MOVE MEDIUM ATTACHED"},

/* A8      OM O      READ(12) */
{0xA8, W|R|O,       "READ(12)"},
/* A8           O    GET MESSAGE(12) */
{0xA8, C,           "GET MESSAGE(12)"},

/* A9       O        PLAY TRACK RELATIVE(12) */
{0xA9, R,           "PLAY TRACK RELATIVE(12)"},

/* AA      O  O      WRITE(12) */
{0xAA, W|O,         "WRITE(12)"},
/* AA       O        WRITE CD(12) {MMC Proposed} */
{0xAA, R,           "WRITE CD(12) {MMC Proposed}"},
/* AA           O    SEND MESSAGE(12) */
{0xAA, C,           "SEND MESSAGE(12)"},

/* AB */

/* AC         O      ERASE(12) */
{0xAC, O,           "ERASE(12)"},

/* AD */

/* AE      O  O      WRITE AND VERIFY(12) */
{0xAE, W|O,         "WRITE AND VERIFY(12)"},

/* AF      OO O      VERIFY(12) */
{0xAF, W|R|O,       "VERIFY(12)"},

/* B0      ZO Z      SEARCH DATA HIGH(12) */
{0xB0, W|R|O,       "SEARCH DATA HIGH(12)"},

/* B1      ZO Z      SEARCH DATA EQUAL(12) */
{0xB1, W|R|O,       "SEARCH DATA EQUAL(12)"},

/* B2      ZO Z      SEARCH DATA LOW(12) */
{0xB2, W|R|O,       "SEARCH DATA LOW(12)"},

/* B3      OO O      SET LIMITS(12) */
{0xB3, W|R|O,       "SET LIMITS(12)"},

/* B4  OO  OO OO     READ ELEMENT STATUS ATTACHED */
{0xB4, D|T|W|R|O|M, "READ ELEMENT STATUS ATTACHED"},

/* B5          O     REQUEST VOLUME ELEMENT ADDRESS */
{0xB5, M,           "REQUEST VOLUME ELEMENT ADDRESS"},

/* B6          O     SEND VOLUME TAG */
{0xB6, M,           "SEND VOLUME TAG"},

/* B7         O      READ DEFECT DATA(12) */
{0xB7, O,           "READ DEFECT DATA(12)"},

/* B8   O      M     READ ELEMENT STATUS */
{0xB8, T|M,         "READ ELEMENT STATUS"},
/* B8       O        SET CD SPEED {MMC Proposed} */
{0xB8, R,           "SET CD SPEED {MMC Proposed}"},

/* B9       M        READ CD MSF {MMC Proposed} */
{0xB9, R,           "READ CD MSF {MMC Proposed}"},

/* BA       O        SCAN {MMC Proposed} */
{0xBA, R,           "SCAN {MMC Proposed}"},
/* BA            M   REDUNDANCY GROUP (IN) */
{0xBA, A,           "REDUNDANCY GROUP (IN)"},

/* BB       O        SET CD-ROM SPEED {proposed} */
{0xBB, R,           "SET CD-ROM SPEED {proposed}"},
/* BB            O   REDUNDANCY GROUP (OUT) */
{0xBB, A,           "REDUNDANCY GROUP (OUT)"},

/* BC       O        PLAY CD {MMC Proposed} */
{0xBC, R,           "PLAY CD {MMC Proposed}"},
/* BC            M   SPARE (IN) */
{0xBC, A,           "SPARE (IN)"},

/* BD       M        MECHANISM STATUS {MMC Proposed} */
{0xBD, R,           "MECHANISM STATUS {MMC Proposed}"},
/* BD            O   SPARE (OUT) */
{0xBD, A,           "SPARE (OUT)"},

/* BE       O        READ CD {MMC Proposed} */
{0xBE, R,           "READ CD {MMC Proposed}"},
/* BE            M   VOLUME SET (IN) */
{0xBE, A,           "VOLUME SET (IN)"},

/* BF            O   VOLUME SET (OUT) */
{0xBF, A,           "VOLUME SET (OUT)"}
};

const char *
scsi_op_desc(u_int16_t opcode, struct scsi_inquiry_data *inq_data)
{
	caddr_t match;
	int i, j;
	u_int16_t opmask;
	u_int16_t pd_type;
	int       num_ops[2];
	struct op_table_entry *table[2];
	int num_tables;

	pd_type = SID_TYPE(inq_data);

	match = cam_quirkmatch((caddr_t)inq_data,
			       (caddr_t)scsi_op_quirk_table,
			       sizeof(scsi_op_quirk_table)/
			       sizeof(*scsi_op_quirk_table),
			       sizeof(*scsi_op_quirk_table),
			       scsi_inquiry_match);

	if (match != NULL) {
		table[0] = ((struct scsi_op_quirk_entry *)match)->op_table;
		num_ops[0] = ((struct scsi_op_quirk_entry *)match)->num_ops;
		table[1] = scsi_op_codes;
		num_ops[1] = sizeof(scsi_op_codes)/sizeof(scsi_op_codes[0]);
		num_tables = 2;
	} else {
		/*	
		 * If this is true, we have a vendor specific opcode that
		 * wasn't covered in the quirk table.
		 */
		if ((opcode > 0xBF) || ((opcode > 0x5F) && (opcode < 0x80)))
			return("Vendor Specific Command");

		table[0] = scsi_op_codes;
		num_ops[0] = sizeof(scsi_op_codes)/sizeof(scsi_op_codes[0]);
		num_tables = 1;
	}

	/* RBC is 'Simplified' Direct Access Device */
	if (pd_type == T_RBC)
		pd_type = T_DIRECT;

	opmask = 1 << pd_type;

	for (j = 0; j < num_tables; j++) {
		for (i = 0;i < num_ops[j] && table[j][i].opcode <= opcode; i++){
			if ((table[j][i].opcode == opcode) 
			 && ((table[j][i].opmask & opmask) != 0))
				return(table[j][i].desc);
		}
	}
	
	/*
	 * If we can't find a match for the command in the table, we just
	 * assume it's a vendor specifc command.
	 */
	return("Vendor Specific Command");

}

#else /* SCSI_NO_OP_STRINGS */

const char *
scsi_op_desc(u_int16_t opcode, struct scsi_inquiry_data *inq_data)
{
	return("");
}

#endif


#include <sys/param.h>


#if !defined(SCSI_NO_SENSE_STRINGS)
#define SST(asc, ascq, action, desc) \
	asc, ascq, action, desc
#else 
#define SST(asc, ascq, action, desc) \
	asc, ascq, action
#endif 

static const char quantum[] = "QUANTUM";

/*
 * WARNING:  You must update the num_ascs field below for this quirk table 
 * entry if you add more entries.
 */
static struct asc_table_entry quantum_fireball_entries[] = {
	{SST(0x04, 0x0b, SS_START|SSQ_DECREMENT_COUNT|ENXIO, 
	     "Logical unit not ready, initializing cmd. required")}
};

static struct scsi_sense_quirk_entry asc_quirk_table[] = {
	{
		/*
		 * The Quantum Fireball ST and SE like to return 0x04 0x0b when
		 * they really should return 0x04 0x02.  0x04,0x0b isn't
		 * defined in any SCSI spec, and it isn't mentioned in the
		 * hardware manual for these drives.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "QUANTUM", "FIREBALL S*", "*"},
		1, /* number of vendor-specific sense codes for this entry */
		quantum_fireball_entries
	}
};

static struct asc_table_entry asc_text[] = {
/*
 * From File: ASC-NUM.TXT
 * SCSI ASC/ASCQ Assignments
 * Numeric Sorted Listing
 * as of  5/12/97
 *
 * D - DIRECT ACCESS DEVICE (SBC)                     device column key
 * .T - SEQUENTIAL ACCESS DEVICE (SSC)               -------------------
 * . L - PRINTER DEVICE (SSC)                           blank = reserved
 * .  P - PROCESSOR DEVICE (SPC)                     not blank = allowed
 * .  .W - WRITE ONCE READ MULTIPLE DEVICE (SBC)
 * .  . R - CD DEVICE (MMC)
 * .  .  S - SCANNER DEVICE (SGC)
 * .  .  .O - OPTICAL MEMORY DEVICE (SBC)
 * .  .  . M - MEDIA CHANGER DEVICE (SMC)
 * .  .  .  C - COMMUNICATION DEVICE (SSC)
 * .  .  .  .A - STORAGE ARRAY DEVICE (SCC)
 * .  .  .  . E - ENCLOSURE SERVICES DEVICE (SES)
 * DTLPWRSOMCAE        ASC   ASCQ  Action  Description
 * ------------        ----  ----  ------  -----------------------------------*/
/* DTLPWRSOMCAE */{SST(0x00, 0x00, SS_NEPDEF,
			"No additional sense information") },
/*  T    S      */{SST(0x00, 0x01, SS_DEF,
			"Filemark detected") },
/*  T    S      */{SST(0x00, 0x02, SS_DEF,
			"End-of-partition/medium detected") },
/*  T           */{SST(0x00, 0x03, SS_DEF,
			"Setmark detected") },
/*  T    S      */{SST(0x00, 0x04, SS_DEF,
			"Beginning-of-partition/medium detected") },
/*  T    S      */{SST(0x00, 0x05, SS_DEF,
			"End-of-data detected") },
/* DTLPWRSOMCAE */{SST(0x00, 0x06, SS_DEF,
			"I/O process terminated") },
/*      R       */{SST(0x00, 0x11, SS_NEDEF|EBUSY,
			"Audio play operation in progress") },
/*      R       */{SST(0x00, 0x12, SS_NEDEF,
			"Audio play operation paused") },
/*      R       */{SST(0x00, 0x13, SS_NEDEF,
			"Audio play operation successfully completed") },
/*      R       */{SST(0x00, 0x14, SS_DEF,
			"Audio play operation stopped due to error") },
/*      R       */{SST(0x00, 0x15, SS_DEF,
			"No current audio status to return") },
/* DTLPWRSOMCAE */{SST(0x00, 0x16, SS_NEDEF|EBUSY,
			"Operation in progress") },
/* DTL WRSOM AE */{SST(0x00, 0x17, SS_DEF,
			"Cleaning requested") },
/* D   W  O     */{SST(0x01, 0x00, SS_DEF,
			"No index/sector signal") },
/* D   WR OM    */{SST(0x02, 0x00, SS_DEF,
			"No seek complete") },
/* DTL W SO     */{SST(0x03, 0x00, SS_DEF,
			"Peripheral device write fault") },
/*  T           */{SST(0x03, 0x01, SS_DEF,
			"No write current") },
/*  T           */{SST(0x03, 0x02, SS_DEF,
			"Excessive write errors") },
/* DTLPWRSOMCAE */{SST(0x04, 0x00, SS_TUR|SSQ_MANY|SSQ_DECREMENT_COUNT|EIO,
			"Logical unit not ready, cause not reportable") },
/* DTLPWRSOMCAE */{SST(0x04, 0x01, SS_TUR|SSQ_MANY|SSQ_DECREMENT_COUNT|EBUSY,
			"Logical unit is in process of becoming ready") },
/* DTLPWRSOMCAE */{SST(0x04, 0x02, SS_START|SSQ_DECREMENT_COUNT|ENXIO,
			"Logical unit not ready, initializing cmd. required") },
/* DTLPWRSOMCAE */{SST(0x04, 0x03, SS_NEDEF|ENXIO,
			"Logical unit not ready, manual intervention required")},
/* DTL    O     */{SST(0x04, 0x04, SS_NEDEF|EBUSY,
			"Logical unit not ready, format in progress") },
/* DT  W  OMCA  */{SST(0x04, 0x05, SS_NEDEF|EBUSY,
			"Logical unit not ready, rebuild in progress") },
/* DT  W  OMCA  */{SST(0x04, 0x06, SS_NEDEF|EBUSY,
			"Logical unit not ready, recalculation in progress") },
/* DTLPWRSOMCAE */{SST(0x04, 0x07, SS_NEDEF|EBUSY,
			"Logical unit not ready, operation in progress") },
/*      R       */{SST(0x04, 0x08, SS_NEDEF|EBUSY,
			"Logical unit not ready, long write in progress") },
/* DTL WRSOMCAE */{SST(0x05, 0x00, SS_DEF,
			"Logical unit does not respond to selection") },
/* D   WR OM    */{SST(0x06, 0x00, SS_DEF,
			"No reference position found") },
/* DTL WRSOM    */{SST(0x07, 0x00, SS_DEF,
			"Multiple peripheral devices selected") },
/* DTL WRSOMCAE */{SST(0x08, 0x00, SS_DEF,
			"Logical unit communication failure") },
/* DTL WRSOMCAE */{SST(0x08, 0x01, SS_DEF,
			"Logical unit communication time-out") },
/* DTL WRSOMCAE */{SST(0x08, 0x02, SS_DEF,
			"Logical unit communication parity error") },
/* DT   R OM    */{SST(0x08, 0x03, SS_DEF,
			"Logical unit communication crc error (ultra-dma/32)")},
/* DT  WR O     */{SST(0x09, 0x00, SS_DEF,
			"Track following error") },
/*     WR O     */{SST(0x09, 0x01, SS_DEF,
			"Tracking servo failure") },
/*     WR O     */{SST(0x09, 0x02, SS_DEF,
			"Focus servo failure") },
/*     WR O     */{SST(0x09, 0x03, SS_DEF,
			"Spindle servo failure") },
/* DT  WR O     */{SST(0x09, 0x04, SS_DEF,
			"Head select fault") },
/* DTLPWRSOMCAE */{SST(0x0A, 0x00, SS_NEDEF|ENOSPC,
			"Error log overflow") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x00, SS_DEF,
			"Warning") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x01, SS_DEF,
			"Specified temperature exceeded") },
/* DTLPWRSOMCAE */{SST(0x0B, 0x02, SS_DEF,
			"Enclosure degraded") },
/*  T   RS      */{SST(0x0C, 0x00, SS_DEF,
			"Write error") },
/* D   W  O     */{SST(0x0C, 0x01, SS_NEDEF,
			"Write error - recovered with auto reallocation") },
/* D   W  O     */{SST(0x0C, 0x02, SS_DEF,
			"Write error - auto reallocation failed") },
/* D   W  O     */{SST(0x0C, 0x03, SS_DEF,
			"Write error - recommend reassignment") },
/* DT  W  O     */{SST(0x0C, 0x04, SS_NEPDEF,
			"Compression check miscompare error") },
/* DT  W  O     */{SST(0x0C, 0x05, SS_DEF,
			"Data expansion occurred during compression") },
/* DT  W  O     */{SST(0x0C, 0x06, SS_DEF,
			"Block not compressible") },
/*      R       */{SST(0x0C, 0x07, SS_DEF,
			"Write error - recovery needed") },
/*      R       */{SST(0x0C, 0x08, SS_DEF,
			"Write error - recovery failed") },
/*      R       */{SST(0x0C, 0x09, SS_DEF,
			"Write error - loss of streaming") },
/*      R       */{SST(0x0C, 0x0A, SS_DEF,
			"Write error - padding blocks added") },
/* D   W  O     */{SST(0x10, 0x00, SS_DEF,
			"ID CRC or ECC error") },
/* DT  WRSO     */{SST(0x11, 0x00, SS_DEF,
			"Unrecovered read error") },
/* DT  W SO     */{SST(0x11, 0x01, SS_DEF,
			"Read retries exhausted") },
/* DT  W SO     */{SST(0x11, 0x02, SS_DEF,
			"Error too long to correct") },
/* DT  W SO     */{SST(0x11, 0x03, SS_DEF,
			"Multiple read errors") },
/* D   W  O     */{SST(0x11, 0x04, SS_DEF,
			"Unrecovered read error - auto reallocate failed") },
/*     WR O     */{SST(0x11, 0x05, SS_DEF,
			"L-EC uncorrectable error") },
/*     WR O     */{SST(0x11, 0x06, SS_DEF,
			"CIRC unrecovered error") },
/*     W  O     */{SST(0x11, 0x07, SS_DEF,
			"Data re-synchronization error") },
/*  T           */{SST(0x11, 0x08, SS_DEF,
			"Incomplete block read") },
/*  T           */{SST(0x11, 0x09, SS_DEF,
			"No gap found") },
/* DT     O     */{SST(0x11, 0x0A, SS_DEF,
			"Miscorrected error") },
/* D   W  O     */{SST(0x11, 0x0B, SS_DEF,
			"Unrecovered read error - recommend reassignment") },
/* D   W  O     */{SST(0x11, 0x0C, SS_DEF,
			"Unrecovered read error - recommend rewrite the data")},
/* DT  WR O     */{SST(0x11, 0x0D, SS_DEF,
			"De-compression CRC error") },
/* DT  WR O     */{SST(0x11, 0x0E, SS_DEF,
			"Cannot decompress using declared algorithm") },
/*      R       */{SST(0x11, 0x0F, SS_DEF,
			"Error reading UPC/EAN number") },
/*      R       */{SST(0x11, 0x10, SS_DEF,
			"Error reading ISRC number") },
/*      R       */{SST(0x11, 0x11, SS_DEF,
			"Read error - loss of streaming") },
/* D   W  O     */{SST(0x12, 0x00, SS_DEF,
			"Address mark not found for id field") },
/* D   W  O     */{SST(0x13, 0x00, SS_DEF,
			"Address mark not found for data field") },
/* DTL WRSO     */{SST(0x14, 0x00, SS_DEF,
			"Recorded entity not found") },
/* DT  WR O     */{SST(0x14, 0x01, SS_DEF,
			"Record not found") },
/*  T           */{SST(0x14, 0x02, SS_DEF,
			"Filemark or setmark not found") },
/*  T           */{SST(0x14, 0x03, SS_DEF,
			"End-of-data not found") },
/*  T           */{SST(0x14, 0x04, SS_DEF,
			"Block sequence error") },
/* DT  W  O     */{SST(0x14, 0x05, SS_DEF,
			"Record not found - recommend reassignment") },
/* DT  W  O     */{SST(0x14, 0x06, SS_DEF,
			"Record not found - data auto-reallocated") },
/* DTL WRSOM    */{SST(0x15, 0x00, SS_DEF,
			"Random positioning error") },
/* DTL WRSOM    */{SST(0x15, 0x01, SS_DEF,
			"Mechanical positioning error") },
/* DT  WR O     */{SST(0x15, 0x02, SS_DEF,
			"Positioning error detected by read of medium") },
/* D   W  O     */{SST(0x16, 0x00, SS_DEF,
			"Data synchronization mark error") },
/* D   W  O     */{SST(0x16, 0x01, SS_DEF,
			"Data sync error - data rewritten") },
/* D   W  O     */{SST(0x16, 0x02, SS_DEF,
			"Data sync error - recommend rewrite") },
/* D   W  O     */{SST(0x16, 0x03, SS_NEDEF,
			"Data sync error - data auto-reallocated") },
/* D   W  O     */{SST(0x16, 0x04, SS_DEF,
			"Data sync error - recommend reassignment") },
/* DT  WRSO     */{SST(0x17, 0x00, SS_NEDEF,
			"Recovered data with no error correction applied") },
/* DT  WRSO     */{SST(0x17, 0x01, SS_NEDEF,
			"Recovered data with retries") },
/* DT  WR O     */{SST(0x17, 0x02, SS_NEDEF,
			"Recovered data with positive head offset") },
/* DT  WR O     */{SST(0x17, 0x03, SS_NEDEF,
			"Recovered data with negative head offset") },
/*     WR O     */{SST(0x17, 0x04, SS_NEDEF,
			"Recovered data with retries and/or CIRC applied") },
/* D   WR O     */{SST(0x17, 0x05, SS_NEDEF,
			"Recovered data using previous sector id") },
/* D   W  O     */{SST(0x17, 0x06, SS_NEDEF,
			"Recovered data without ECC - data auto-reallocated") },
/* D   W  O     */{SST(0x17, 0x07, SS_NEDEF,
			"Recovered data without ECC - recommend reassignment")},
/* D   W  O     */{SST(0x17, 0x08, SS_NEDEF,
			"Recovered data without ECC - recommend rewrite") },
/* D   W  O     */{SST(0x17, 0x09, SS_NEDEF,
			"Recovered data without ECC - data rewritten") },
/* D   W  O     */{SST(0x18, 0x00, SS_NEDEF,
			"Recovered data with error correction applied") },
/* D   WR O     */{SST(0x18, 0x01, SS_NEDEF,
			"Recovered data with error corr. & retries applied") },
/* D   WR O     */{SST(0x18, 0x02, SS_NEDEF,
			"Recovered data - data auto-reallocated") },
/*      R       */{SST(0x18, 0x03, SS_NEDEF,
			"Recovered data with CIRC") },
/*      R       */{SST(0x18, 0x04, SS_NEDEF,
			"Recovered data with L-EC") },
/* D   WR O     */{SST(0x18, 0x05, SS_NEDEF,
			"Recovered data - recommend reassignment") },
/* D   WR O     */{SST(0x18, 0x06, SS_NEDEF,
			"Recovered data - recommend rewrite") },
/* D   W  O     */{SST(0x18, 0x07, SS_NEDEF,
			"Recovered data with ECC - data rewritten") },
/* D      O     */{SST(0x19, 0x00, SS_DEF,
			"Defect list error") },
/* D      O     */{SST(0x19, 0x01, SS_DEF,
			"Defect list not available") },
/* D      O     */{SST(0x19, 0x02, SS_DEF,
			"Defect list error in primary list") },
/* D      O     */{SST(0x19, 0x03, SS_DEF,
			"Defect list error in grown list") },
/* DTLPWRSOMCAE */{SST(0x1A, 0x00, SS_DEF,
			"Parameter list length error") },
/* DTLPWRSOMCAE */{SST(0x1B, 0x00, SS_DEF,
			"Synchronous data transfer error") },
/* D      O     */{SST(0x1C, 0x00, SS_DEF,
			"Defect list not found") },
/* D      O     */{SST(0x1C, 0x01, SS_DEF,
			"Primary defect list not found") },
/* D      O     */{SST(0x1C, 0x02, SS_DEF,
			"Grown defect list not found") },
/* D   W  O     */{SST(0x1D, 0x00, SS_NEPDEF,
			"Miscompare during verify operation" )},
/* D   W  O     */{SST(0x1E, 0x00, SS_NEDEF,
			"Recovered id with ecc correction") },
/* D      O     */{SST(0x1F, 0x00, SS_DEF,
			"Partial defect list transfer") },
/* DTLPWRSOMCAE */{SST(0x20, 0x00, SS_DEF,
			"Invalid command operation code") },
/* DT  WR OM    */{SST(0x21, 0x00, SS_DEF,
			"Logical block address out of range" )},
/* DT  WR OM    */{SST(0x21, 0x01, SS_DEF,
			"Invalid element address") },
/* D            */{SST(0x22, 0x00, SS_DEF,
			"Illegal function") }, /* Deprecated. Use 20 00, 24 00, or 26 00 instead */
/* DTLPWRSOMCAE */{SST(0x24, 0x00, SS_NEDEF|EINVAL,
			"Invalid field in CDB") },
/* DTLPWRSOMCAE */{SST(0x25, 0x00, SS_NEDEF|ENXIO,
			"Logical unit not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x00, SS_NEDEF|EINVAL,
			"Invalid field in parameter list") },
/* DTLPWRSOMCAE */{SST(0x26, 0x01, SS_NEDEF|EINVAL,
			"Parameter not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x02, SS_NEDEF|EINVAL,
			"Parameter value invalid") },
/* DTLPWRSOMCAE */{SST(0x26, 0x03, SS_DEF,
			"Threshold parameters not supported") },
/* DTLPWRSOMCAE */{SST(0x26, 0x04, SS_DEF,
			"Invalid release of active persistent reservation") },
/* DT  W  O     */{SST(0x27, 0x00, SS_NEDEF|EACCES,
			"Write protected") },
/* DT  W  O     */{SST(0x27, 0x01, SS_NEDEF|EACCES,
			"Hardware write protected") },
/* DT  W  O     */{SST(0x27, 0x02, SS_NEDEF|EACCES,
			"Logical unit software write protected") },
/*  T           */{SST(0x27, 0x03, SS_NEDEF|EACCES,
			"Associated write protect") },
/*  T           */{SST(0x27, 0x04, SS_NEDEF|EACCES,
			"Persistent write protect") },
/*  T           */{SST(0x27, 0x05, SS_NEDEF|EACCES,
			"Permanent write protect") },
/* DTLPWRSOMCAE */{SST(0x28, 0x00, SS_NEDEF|ENXIO,
			"Not ready to ready change, medium may have changed") },
/* DT  WR OM    */{SST(0x28, 0x01, SS_DEF,
			"Import or export element accessed") },
/* DTLPWRSOMCAE */{SST(0x29, 0x00, SS_NEDEF|ENXIO,
			"Power on, reset, or bus device reset occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x01, SS_DEF,
			"Power on occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x02, SS_DEF,
			"Scsi bus reset occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x03, SS_DEF,
			"Bus device reset function occurred") },
/* DTLPWRSOMCAE */{SST(0x29, 0x04, SS_DEF,
			"Device internal reset") },
/* DTLPWRSOMCAE */{SST(0x29, 0x05, SS_DEF,
			"Transceiver mode changed to single-ended") },
/* DTLPWRSOMCAE */{SST(0x29, 0x06, SS_DEF,
			"Transceiver mode changed to LVD") },
/* DTL WRSOMCAE */{SST(0x2A, 0x00, SS_DEF,
			"Parameters changed") },
/* DTL WRSOMCAE */{SST(0x2A, 0x01, SS_DEF,
			"Mode parameters changed") },
/* DTL WRSOMCAE */{SST(0x2A, 0x02, SS_DEF,
			"Log parameters changed") },
/* DTLPWRSOMCAE */{SST(0x2A, 0x03, SS_DEF,
			"Reservations preempted") },
/* DTLPWRSO C   */{SST(0x2B, 0x00, SS_DEF,
			"Copy cannot execute since host cannot disconnect") },
/* DTLPWRSOMCAE */{SST(0x2C, 0x00, SS_DEF,
			"Command sequence error") },
/*       S      */{SST(0x2C, 0x01, SS_DEF,
			"Too many windows specified") },
/*       S      */{SST(0x2C, 0x02, SS_DEF,
			"Invalid combination of windows specified") },
/*      R       */{SST(0x2C, 0x03, SS_DEF,
			"Current program area is not empty") },
/*      R       */{SST(0x2C, 0x04, SS_DEF,
			"Current program area is empty") },
/*  T           */{SST(0x2D, 0x00, SS_DEF,
			"Overwrite error on update in place") },
/* DTLPWRSOMCAE */{SST(0x2F, 0x00, SS_DEF,
			"Commands cleared by another initiator") },
/* DT  WR OM    */{SST(0x30, 0x00, SS_DEF,
			"Incompatible medium installed") },
/* DT  WR O     */{SST(0x30, 0x01, SS_DEF,
			"Cannot read medium - unknown format") },
/* DT  WR O     */{SST(0x30, 0x02, SS_DEF,
			"Cannot read medium - incompatible format") },
/* DT           */{SST(0x30, 0x03, SS_DEF,
			"Cleaning cartridge installed") },
/* DT  WR O     */{SST(0x30, 0x04, SS_DEF,
			"Cannot write medium - unknown format") },
/* DT  WR O     */{SST(0x30, 0x05, SS_DEF,
			"Cannot write medium - incompatible format") },
/* DT  W  O     */{SST(0x30, 0x06, SS_DEF,
			"Cannot format medium - incompatible medium") },
/* DTL WRSOM AE */{SST(0x30, 0x07, SS_DEF,
			"Cleaning failure") },
/*      R       */{SST(0x30, 0x08, SS_DEF,
			"Cannot write - application code mismatch") },
/*      R       */{SST(0x30, 0x09, SS_DEF,
			"Current session not fixated for append") },
/* DT  WR O     */{SST(0x31, 0x00, SS_DEF,
			"Medium format corrupted") },
/* D L  R O     */{SST(0x31, 0x01, SS_DEF,
			"Format command failed") },
/* D   W  O     */{SST(0x32, 0x00, SS_DEF,
			"No defect spare location available") },
/* D   W  O     */{SST(0x32, 0x01, SS_DEF,
			"Defect list update failure") },
/*  T           */{SST(0x33, 0x00, SS_DEF,
			"Tape length error") },
/* DTLPWRSOMCAE */{SST(0x34, 0x00, SS_DEF,
			"Enclosure failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x00, SS_DEF,
			"Enclosure services failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x01, SS_DEF,
			"Unsupported enclosure function") },
/* DTLPWRSOMCAE */{SST(0x35, 0x02, SS_DEF,
			"Enclosure services unavailable") },
/* DTLPWRSOMCAE */{SST(0x35, 0x03, SS_DEF,
			"Enclosure services transfer failure") },
/* DTLPWRSOMCAE */{SST(0x35, 0x04, SS_DEF,
			"Enclosure services transfer refused") },
/*   L          */{SST(0x36, 0x00, SS_DEF,
			"Ribbon, ink, or toner failure") },
/* DTL WRSOMCAE */{SST(0x37, 0x00, SS_DEF,
			"Rounded parameter") },
/* DTL WRSOMCAE */{SST(0x39, 0x00, SS_DEF,
			"Saving parameters not supported") },
/* DTL WRSOM    */{SST(0x3A, 0x00, SS_NEDEF|ENXIO,
			"Medium not present") },
/* DT  WR OM    */{SST(0x3A, 0x01, SS_NEDEF|ENXIO,
			"Medium not present - tray closed") },
/* DT  WR OM    */{SST(0x3A, 0x02, SS_NEDEF|ENXIO,
			"Medium not present - tray open") },
/*  TL          */{SST(0x3B, 0x00, SS_DEF,
			"Sequential positioning error") },
/*  T           */{SST(0x3B, 0x01, SS_DEF,
			"Tape position error at beginning-of-medium") },
/*  T           */{SST(0x3B, 0x02, SS_DEF,
			"Tape position error at end-of-medium") },
/*   L          */{SST(0x3B, 0x03, SS_DEF,
			"Tape or electronic vertical forms unit not ready") },
/*   L          */{SST(0x3B, 0x04, SS_DEF,
			"Slew failure") },
/*   L          */{SST(0x3B, 0x05, SS_DEF,
			"Paper jam") },
/*   L          */{SST(0x3B, 0x06, SS_DEF,
			"Failed to sense top-of-form") },
/*   L          */{SST(0x3B, 0x07, SS_DEF,
			"Failed to sense bottom-of-form") },
/*  T           */{SST(0x3B, 0x08, SS_DEF,
			"Reposition error") },
/*       S      */{SST(0x3B, 0x09, SS_DEF,
			"Read past end of medium") },
/*       S      */{SST(0x3B, 0x0A, SS_DEF,
			"Read past beginning of medium") },
/*       S      */{SST(0x3B, 0x0B, SS_DEF,
			"Position past end of medium") },
/*  T    S      */{SST(0x3B, 0x0C, SS_DEF,
			"Position past beginning of medium") },
/* DT  WR OM    */{SST(0x3B, 0x0D, SS_NEDEF|ENOSPC,
			"Medium destination element full") },
/* DT  WR OM    */{SST(0x3B, 0x0E, SS_DEF,
			"Medium source element empty") },
/*      R       */{SST(0x3B, 0x0F, SS_DEF,
			"End of medium reached") },
/* DT  WR OM    */{SST(0x3B, 0x11, SS_DEF,
			"Medium magazine not accessible") },
/* DT  WR OM    */{SST(0x3B, 0x12, SS_DEF,
			"Medium magazine removed") },
/* DT  WR OM    */{SST(0x3B, 0x13, SS_DEF,
			"Medium magazine inserted") },
/* DT  WR OM    */{SST(0x3B, 0x14, SS_DEF,
			"Medium magazine locked") },
/* DT  WR OM    */{SST(0x3B, 0x15, SS_DEF,
			"Medium magazine unlocked") },
/* DTLPWRSOMCAE */{SST(0x3D, 0x00, SS_DEF,
			"Invalid bits in identify message") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x00, SS_DEF,
			"Logical unit has not self-configured yet") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x01, SS_DEF,
			"Logical unit failure") },
/* DTLPWRSOMCAE */{SST(0x3E, 0x02, SS_DEF,
			"Timeout on logical unit") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x00, SS_DEF,
			"Target operating conditions have changed") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x01, SS_DEF,
			"Microcode has been changed") },
/* DTLPWRSOMC   */{SST(0x3F, 0x02, SS_DEF,
			"Changed operating definition") },
/* DTLPWRSOMCAE */{SST(0x3F, 0x03, SS_DEF,
			"Inquiry data has changed") },
/* DT  WR OMCAE */{SST(0x3F, 0x04, SS_DEF,
			"Component device attached") },
/* DT  WR OMCAE */{SST(0x3F, 0x05, SS_DEF,
			"Device identifier changed") },
/* DT  WR OMCAE */{SST(0x3F, 0x06, SS_DEF,
			"Redundancy group created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x07, SS_DEF,
			"Redundancy group deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x08, SS_DEF,
			"Spare created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x09, SS_DEF,
			"Spare deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x0A, SS_DEF,
			"Volume set created or modified") },
/* DT  WR OMCAE */{SST(0x3F, 0x0B, SS_DEF,
			"Volume set deleted") },
/* DT  WR OMCAE */{SST(0x3F, 0x0C, SS_DEF,
			"Volume set deassigned") },
/* DT  WR OMCAE */{SST(0x3F, 0x0D, SS_DEF,
			"Volume set reassigned") },
/* D            */{SST(0x40, 0x00, SS_DEF,
			"Ram failure") }, /* deprecated - use 40 NN instead */
/* DTLPWRSOMCAE */{SST(0x40, 0x80, SS_DEF,
			"Diagnostic failure: ASCQ = Component ID") },
/* DTLPWRSOMCAE */{SST(0x40, 0xFF, SS_DEF|SSQ_RANGE,
			NULL) },/* Range 0x80->0xFF */
/* D            */{SST(0x41, 0x00, SS_DEF,
			"Data path failure") }, /* deprecated - use 40 NN instead */
/* D            */{SST(0x42, 0x00, SS_DEF,
			"Power-on or self-test failure") }, /* deprecated - use 40 NN instead */
/* DTLPWRSOMCAE */{SST(0x43, 0x00, SS_DEF,
			"Message error") },
/* DTLPWRSOMCAE */{SST(0x44, 0x00, SS_DEF,
			"Internal target failure") },
/* DTLPWRSOMCAE */{SST(0x45, 0x00, SS_DEF,
			"Select or reselect failure") },
/* DTLPWRSOMC   */{SST(0x46, 0x00, SS_DEF,
			"Unsuccessful soft reset") },
/* DTLPWRSOMCAE */{SST(0x47, 0x00, SS_DEF,
			"SCSI parity error") },
/* DTLPWRSOMCAE */{SST(0x48, 0x00, SS_DEF,
			"Initiator detected error message received") },
/* DTLPWRSOMCAE */{SST(0x49, 0x00, SS_DEF,
			"Invalid message error") },
/* DTLPWRSOMCAE */{SST(0x4A, 0x00, SS_DEF,
			"Command phase error") },
/* DTLPWRSOMCAE */{SST(0x4B, 0x00, SS_DEF,
			"Data phase error") },
/* DTLPWRSOMCAE */{SST(0x4C, 0x00, SS_DEF,
			"Logical unit failed self-configuration") },
/* DTLPWRSOMCAE */{SST(0x4D, 0x00, SS_DEF,
			"Tagged overlapped commands: ASCQ = Queue tag ID") },
/* DTLPWRSOMCAE */{SST(0x4D, 0xFF, SS_DEF|SSQ_RANGE,
			NULL)}, /* Range 0x00->0xFF */
/* DTLPWRSOMCAE */{SST(0x4E, 0x00, SS_DEF,
			"Overlapped commands attempted") },
/*  T           */{SST(0x50, 0x00, SS_DEF,
			"Write append error") },
/*  T           */{SST(0x50, 0x01, SS_DEF,
			"Write append position error") },
/*  T           */{SST(0x50, 0x02, SS_DEF,
			"Position error related to timing") },
/*  T     O     */{SST(0x51, 0x00, SS_DEF,
			"Erase failure") },
/*  T           */{SST(0x52, 0x00, SS_DEF,
			"Cartridge fault") },
/* DTL WRSOM    */{SST(0x53, 0x00, SS_DEF,
			"Media load or eject failed") },
/*  T           */{SST(0x53, 0x01, SS_DEF,
			"Unload tape failure") },
/* DT  WR OM    */{SST(0x53, 0x02, SS_DEF,
			"Medium removal prevented") },
/*    P         */{SST(0x54, 0x00, SS_DEF,
			"Scsi to host system interface failure") },
/*    P         */{SST(0x55, 0x00, SS_DEF,
			"System resource failure") },
/* D      O     */{SST(0x55, 0x01, SS_NEDEF|ENOSPC,
			"System buffer full") },
/*      R       */{SST(0x57, 0x00, SS_DEF,
			"Unable to recover table-of-contents") },
/*        O     */{SST(0x58, 0x00, SS_DEF,
			"Generation does not exist") },
/*        O     */{SST(0x59, 0x00, SS_DEF,
			"Updated block read") },
/* DTLPWRSOM    */{SST(0x5A, 0x00, SS_DEF,
			"Operator request or state change input") },
/* DT  WR OM    */{SST(0x5A, 0x01, SS_DEF,
			"Operator medium removal request") },
/* DT  W  O     */{SST(0x5A, 0x02, SS_DEF,
			"Operator selected write protect") },
/* DT  W  O     */{SST(0x5A, 0x03, SS_DEF,
			"Operator selected write permit") },
/* DTLPWRSOM    */{SST(0x5B, 0x00, SS_DEF,
			"Log exception") },
/* DTLPWRSOM    */{SST(0x5B, 0x01, SS_DEF,
			"Threshold condition met") },
/* DTLPWRSOM    */{SST(0x5B, 0x02, SS_DEF,
			"Log counter at maximum") },
/* DTLPWRSOM    */{SST(0x5B, 0x03, SS_DEF,
			"Log list codes exhausted") },
/* D      O     */{SST(0x5C, 0x00, SS_DEF,
			"RPL status change") },
/* D      O     */{SST(0x5C, 0x01, SS_NEDEF,
			"Spindles synchronized") },
/* D      O     */{SST(0x5C, 0x02, SS_DEF,
			"Spindles not synchronized") },
/* DTLPWRSOMCAE */{SST(0x5D, 0x00, SS_DEF,
			"Failure prediction threshold exceeded") },
/* DTLPWRSOMCAE */{SST(0x5D, 0xFF, SS_DEF,
			"Failure prediction threshold exceeded (false)") },
/* DTLPWRSO CA  */{SST(0x5E, 0x00, SS_DEF,
			"Low power condition on") },
/* DTLPWRSO CA  */{SST(0x5E, 0x01, SS_DEF,
			"Idle condition activated by timer") },
/* DTLPWRSO CA  */{SST(0x5E, 0x02, SS_DEF,
			"Standby condition activated by timer") },
/* DTLPWRSO CA  */{SST(0x5E, 0x03, SS_DEF,
			"Idle condition activated by command") },
/* DTLPWRSO CA  */{SST(0x5E, 0x04, SS_DEF,
			"Standby condition activated by command") },
/*       S      */{SST(0x60, 0x00, SS_DEF,
			"Lamp failure") },
/*       S      */{SST(0x61, 0x00, SS_DEF,
			"Video acquisition error") },
/*       S      */{SST(0x61, 0x01, SS_DEF,
			"Unable to acquire video") },
/*       S      */{SST(0x61, 0x02, SS_DEF,
			"Out of focus") },
/*       S      */{SST(0x62, 0x00, SS_DEF,
			"Scan head positioning error") },
/*      R       */{SST(0x63, 0x00, SS_DEF,
			"End of user area encountered on this track") },
/*      R       */{SST(0x63, 0x01, SS_NEDEF|ENOSPC,
			"Packet does not fit in available space") },
/*      R       */{SST(0x64, 0x00, SS_DEF,
			"Illegal mode for this track") },
/*      R       */{SST(0x64, 0x01, SS_DEF,
			"Invalid packet size") },
/* DTLPWRSOMCAE */{SST(0x65, 0x00, SS_DEF,
			"Voltage fault") },
/*       S      */{SST(0x66, 0x00, SS_DEF,
			"Automatic document feeder cover up") },
/*       S      */{SST(0x66, 0x01, SS_DEF,
			"Automatic document feeder lift up") },
/*       S      */{SST(0x66, 0x02, SS_DEF,
			"Document jam in automatic document feeder") },
/*       S      */{SST(0x66, 0x03, SS_DEF,
			"Document miss feed automatic in document feeder") },
/*           A  */{SST(0x67, 0x00, SS_DEF,
			"Configuration failure") },
/*           A  */{SST(0x67, 0x01, SS_DEF,
			"Configuration of incapable logical units failed") },
/*           A  */{SST(0x67, 0x02, SS_DEF,
			"Add logical unit failed") },
/*           A  */{SST(0x67, 0x03, SS_DEF,
			"Modification of logical unit failed") },
/*           A  */{SST(0x67, 0x04, SS_DEF,
			"Exchange of logical unit failed") },
/*           A  */{SST(0x67, 0x05, SS_DEF,
			"Remove of logical unit failed") },
/*           A  */{SST(0x67, 0x06, SS_DEF,
			"Attachment of logical unit failed") },
/*           A  */{SST(0x67, 0x07, SS_DEF,
			"Creation of logical unit failed") },
/*           A  */{SST(0x68, 0x00, SS_DEF,
			"Logical unit not configured") },
/*           A  */{SST(0x69, 0x00, SS_DEF,
			"Data loss on logical unit") },
/*           A  */{SST(0x69, 0x01, SS_DEF,
			"Multiple logical unit failures") },
/*           A  */{SST(0x69, 0x02, SS_DEF,
			"Parity/data mismatch") },
/*           A  */{SST(0x6A, 0x00, SS_DEF,
			"Informational, refer to log") },
/*           A  */{SST(0x6B, 0x00, SS_DEF,
			"State change has occurred") },
/*           A  */{SST(0x6B, 0x01, SS_DEF,
			"Redundancy level got better") },
/*           A  */{SST(0x6B, 0x02, SS_DEF,
			"Redundancy level got worse") },
/*           A  */{SST(0x6C, 0x00, SS_DEF,
			"Rebuild failure occurred") },
/*           A  */{SST(0x6D, 0x00, SS_DEF,
			"Recalculate failure occurred") },
/*           A  */{SST(0x6E, 0x00, SS_DEF,
			"Command to logical unit failed") },
/*  T           */{SST(0x70, 0x00, SS_DEF,
			"Decompression exception short: ASCQ = Algorithm ID") },
/*  T           */{SST(0x70, 0xFF, SS_DEF|SSQ_RANGE,
			NULL) }, /* Range 0x00 -> 0xFF */
/*  T           */{SST(0x71, 0x00, SS_DEF,
			"Decompression exception long: ASCQ = Algorithm ID") },
/*  T           */{SST(0x71, 0xFF, SS_DEF|SSQ_RANGE,
			NULL) }, /* Range 0x00 -> 0xFF */	
/*      R       */{SST(0x72, 0x00, SS_DEF,
			"Session fixation error") },
/*      R       */{SST(0x72, 0x01, SS_DEF,
			"Session fixation error writing lead-in") },
/*      R       */{SST(0x72, 0x02, SS_DEF,
			"Session fixation error writing lead-out") },
/*      R       */{SST(0x72, 0x03, SS_DEF,
			"Session fixation error - incomplete track in session") },
/*      R       */{SST(0x72, 0x04, SS_DEF,
			"Empty or partially written reserved track") },
/*      R       */{SST(0x73, 0x00, SS_DEF,
			"CD control error") },
/*      R       */{SST(0x73, 0x01, SS_DEF,
			"Power calibration area almost full") },
/*      R       */{SST(0x73, 0x02, SS_NEDEF|ENOSPC,
			"Power calibration area is full") },
/*      R       */{SST(0x73, 0x03, SS_DEF,
			"Power calibration area error") },
/*      R       */{SST(0x73, 0x04, SS_DEF,
			"Program memory area update failure") },
/*      R       */{SST(0x73, 0x05, SS_DEF,
			"program memory area is full") }
};

#if !defined(SCSI_NO_SENSE_STRINGS)
const char *
scsi_sense_desc(int asc, int ascq, struct scsi_inquiry_data *inq_data)
{
	int i, j;
	caddr_t match;
	struct asc_table_entry *table[2];
	int table_size[2];
	int num_tables;

	if (inq_data == NULL)
		return(NULL);

	match = cam_quirkmatch((caddr_t)inq_data,
			       (caddr_t)asc_quirk_table,
			       sizeof(asc_quirk_table)/sizeof(*asc_quirk_table),
			       sizeof(*asc_quirk_table), scsi_inquiry_match);

	if (match != NULL) {
		table[0] = ((struct scsi_sense_quirk_entry *)match)->asc_info;
		table_size[0] = 
			((struct scsi_sense_quirk_entry *)match)->num_ascs;
		table[1] = asc_text;
		table_size[1] = sizeof(asc_text)/sizeof(asc_text[0]);
		num_tables = 2;
	} else {
		table[0] = asc_text;
		table_size[0] = sizeof(asc_text)/sizeof(asc_text[0]);
		num_tables = 1;
	}

	for (j = 0; j < num_tables; j++) {
		for (i = 0; i < table_size[j]; i++) {
			if (table[j][i].asc == asc) {

				/* Check for ranges */
				if ((table[j][i].action & SSQ_RANGE) != 0) {
				
					if (table[j][i].ascq >= ascq
					 && table[j][i-1].ascq <= ascq)
						return table[j][i-1].desc;

					continue;
				}
			
				if (table[j][i].ascq == ascq)
					return table[j][i].desc;
			} 
		}
	}

	if (asc >= 0x80 && asc <= 0xff)
		return "Vendor Specific ASC";

	if (ascq >= 0x80 && ascq <= 0xff)
		return "Vendor Specific ASCQ";

	return "Reserved ASC/ASCQ pair";
}

#else /* SCSI_NO_SENSE_STRINGS */
const char *
scsi_sense_desc(int asc, int ascq, struct scsi_inquiry_data *inq_data)
{
	return ("");
}
#endif

/*
 * Given a particular failed CCB and its device type information, return
 * the appropriate action from either the sense code quirk table or the
 * sense code table.
 */
scsi_sense_action
scsi_error_action(int asc, int ascq, struct scsi_inquiry_data *inq_data)
{
	caddr_t match;
	struct asc_table_entry *table[2];
	int table_size[2];
	int num_tables;
	int i, j;

	/*
	 * If we don't have inquiry data, we can't match against any quirk
	 * entries.
	 */
	if (inq_data != NULL) {
		match = cam_quirkmatch((caddr_t)inq_data,
				       (caddr_t)asc_quirk_table,
				       sizeof(asc_quirk_table) /
					 sizeof(*asc_quirk_table),
				       sizeof(*asc_quirk_table),
				       scsi_inquiry_match);
	} else
		match = NULL;

	if (match != NULL) {
		table[0] = ((struct scsi_sense_quirk_entry *)match)->asc_info;
		table_size[0] = 
			((struct scsi_sense_quirk_entry *)match)->num_ascs;
		table[1] = asc_text;
		table_size[1] = sizeof(asc_text)/sizeof(asc_text[0]);
		num_tables = 2;
	} else {
		table[0] = asc_text;
		table_size[0] = sizeof(asc_text)/sizeof(asc_text[0]);
		num_tables = 1;
	}

	for (j = 0; j < num_tables; j++) {
		for (i = 0; i < table_size[j]; i++) {
			if (table[j][i].asc == asc) {

				/* Check for ranges */
				if ((table[j][i].action & SSQ_RANGE) != 0){
				
					if (table[j][i].ascq >= ascq
					 && table[j][i-1].ascq <= ascq)
						return table[j][i].action;

					continue;
				}

				/*
				 * Check to see if we have a match.  If the
				 * current ascq in the table is greater
				 * than our ascq, and there aren't any more
				 * tables to search, just return the
				 * default action.
				 */
				if (table[j][i].ascq == ascq)
					return(table[j][i].action);
				else if ((j == (num_tables - 1)) &&
					(table[j][i].ascq > ascq))
					return(SS_DEF);
			}
		}
	}
	/* 
	 * If we get to this point, it's most likely a vendor specific
	 * ASC and we don't have a quirk entry for it.  Oh well, we just
	 * tell the error handling code to take the default action.
	 */
	return(SS_DEF);
}

char *
scsi_cdb_string(u_int8_t *cdb_ptr, char *cdb_string, size_t len)
{
	u_int8_t cdb_len;
	int i;

	if (cdb_ptr == NULL)
		return("");

	/* Silence warnings */
	cdb_len = 0;

	/*
	 * This is taken from the SCSI-3 draft spec.
	 * (T10/1157D revision 0.3)
	 * The top 3 bits of an opcode are the group code.  The next 5 bits
	 * are the command code.
	 * Group 0:  six byte commands
	 * Group 1:  ten byte commands
	 * Group 2:  ten byte commands
	 * Group 3:  reserved
	 * Group 4:  sixteen byte commands
	 * Group 5:  twelve byte commands
	 * Group 6:  vendor specific
	 * Group 7:  vendor specific
	 */
	switch((*cdb_ptr >> 5) & 0x7) {
		case 0:
			cdb_len = 6;
			break;
		case 1:
		case 2:
			cdb_len = 10;
			break;
		case 3:
		case 6:
		case 7:
			/* in this case, just print out the opcode */
			cdb_len = 1;
			break;
		case 4:
			cdb_len = 16;
			break;
		case 5:
			cdb_len = 12;
			break;
	}
	*cdb_string = '\0';
	for (i = 0; i < cdb_len; i++)
		snprintf(cdb_string + strlen(cdb_string),
		    len - strlen(cdb_string), "%x ", cdb_ptr[i]);

	return(cdb_string);
}
/*
 * scsi_sense_print will decode the sense data into human
 * readable form.  Sense handlers can use this to generate
 * a report.
 */
/*
 * Because scsi_sense_print() utilizes transport layer functions, it will
 * only work in the kernel.
 */
#ifdef _KERNEL

void 
scsi_sense_print(struct ccb_scsiio *csio)
{
	struct	  scsi_sense_data *sense;
	u_int32_t info;
	int	  error_code;
	int	  sense_key;
	int	  asc, ascq;
	struct ccb_getdev cgd;
	u_int8_t  command_print;

	sense = &csio->sense_data;
	
	/*
	 * If the CDB is a physical address, we can't deal with it..
	 */
	if ((csio->ccb_h.flags & CAM_CDB_PHYS) != 0)
		command_print = 0;
	else
		command_print = 1;

	/*
	 * Get the device information.
	 */
	xpt_setup_ccb(&cgd.ccb_h,
		      csio->ccb_h.path,
		      /*priority*/ 1);
	cgd.ccb_h.func_code = XPT_GDEV_TYPE;
	xpt_action((union ccb *)&cgd);

	/*
	 * If the device is unconfigured, just pretend that it is a hard
	 * drive.  scsi_op_desc() needs this.
	 */
	if (cgd.ccb_h.status == CAM_DEV_NOT_THERE)
		cgd.inq_data.device = T_DIRECT;

	if (command_print != 0) {
		char cdb_str[(SCSI_MAX_CDBLEN * 3) + 1];

		xpt_print_path(csio->ccb_h.path);

		if ((csio->ccb_h.flags & CAM_CDB_POINTER) != 0) {
			printf("%s. CDB: %s\n", 
				scsi_op_desc(csio->cdb_io.cdb_ptr[0],
				&cgd.inq_data),
				scsi_cdb_string(csio->cdb_io.cdb_ptr, cdb_str,
						sizeof(cdb_str)));
		} else {
			printf("%s. CDB: %s\n",
				scsi_op_desc(csio->cdb_io.cdb_bytes[0], 
				&cgd.inq_data), scsi_cdb_string(
				csio->cdb_io.cdb_bytes, cdb_str,
				sizeof(cdb_str)));
		}
	}

	/*
	 * If the sense data is a physical pointer, forget it.
	 */
	if (csio->ccb_h.flags & CAM_SENSE_PTR) {
		if (csio->ccb_h.flags & CAM_SENSE_PHYS)
			return;
		else {
			/* 
			 * XXX KDM this is stupid, but casting the
			 * structure doesn't work...
			 */
			bcopy(&csio->sense_data, sense, 
			      sizeof(struct scsi_sense_data *));
		}
	} else {
		/*
		 * If the physical sense flag is set, but the sense pointer
		 * is not also set, we assume that the user is an idiot and
		 * return.  (Well, okay, it could be that somehow, the
		 * entire csio is physical, but we would have probably core
		 * dumped on one of the bogus pointer deferences above
		 * already.)
		 */
		if (csio->ccb_h.flags & CAM_SENSE_PHYS) 
			return;
		else
			sense = &csio->sense_data;
	}

	xpt_print_path(csio->ccb_h.path);
	error_code = sense->error_code & SSD_ERRCODE;
	sense_key = sense->flags & SSD_KEY;

	switch (error_code) {
	case SSD_DEFERRED_ERROR:
		printf("Deferred Error: ");
		/* FALLTHROUGH */
	case SSD_CURRENT_ERROR:

		printf("%s", scsi_sense_key_text[sense_key]);
		info = scsi_4btoul(sense->info);
		
		if (sense->error_code & SSD_ERRCODE_VALID) {

			switch (sense_key) {
			case SSD_KEY_NOT_READY:
			case SSD_KEY_ILLEGAL_REQUEST:
			case SSD_KEY_UNIT_ATTENTION:
			case SSD_KEY_DATA_PROTECT:
				break;
			case SSD_KEY_BLANK_CHECK:
				printf(" req sz: %d (decimal)",
				    info);
				break;
			default:
				if (info) {
					if (sense->flags & SSD_ILI) {
						printf(" ILI (length mismatch):"
						       " %d", info);
					} else {
						printf(" info:%x", info);
					}
				}
			}
		} else if (info)
			printf(" info?:%x", info);

		if (sense->extra_len >= 4) {
			if (bcmp(sense->cmd_spec_info, "\0\0\0\0", 4)) {
				printf(" csi:%x,%x,%x,%x",
				       sense->cmd_spec_info[0],
				       sense->cmd_spec_info[1],
				       sense->cmd_spec_info[2],
				       sense->cmd_spec_info[3]);
			}
		}

		asc = (sense->extra_len >= 5) ? sense->add_sense_code : 0;
		ascq = (sense->extra_len >= 6) ? sense->add_sense_code_qual : 0;

		if (asc || ascq) {
			const char *desc = scsi_sense_desc(asc, ascq,
							   &cgd.inq_data);
			printf(" asc:%x,%x\n", asc, ascq);

			xpt_print_path(csio->ccb_h.path);
			printf("%s", desc);
		}

		if (sense->extra_len >= 7 && sense->fru) {
			printf(" field replaceable unit: %x", sense->fru);
		}

		if ((sense->extra_len >= 10)
		 && (sense->sense_key_spec[0] & SSD_SCS_VALID) != 0) {
			printf(" sks:%x,%x", sense->sense_key_spec[0],
			       scsi_2btoul(&sense->sense_key_spec[1]));
		}
		break;

	default:
		printf("error code %d",
		    sense->error_code & SSD_ERRCODE);
		if (sense->error_code & SSD_ERRCODE_VALID) {
			printf(" at block no. %d (decimal)",
			       info = scsi_4btoul(sense->info));
		}
	}

	printf("\n");
}

#else /* !_KERNEL */


char *
scsi_sense_string(struct cam_device *device, struct ccb_scsiio *csio, 
		  char *str, int str_len)
{
	struct	  scsi_sense_data *sense;
	u_int32_t info;
	int	  error_code;
	int	  sense_key;
	int	  asc, ascq;
	u_int8_t  command_print;
	char	  path_str[64];
	char	  tmpstr[2048];
	int	  tmpstrlen = 2048;
	int	  cur_len = 0, tmplen = 0, retlen;

	if ((device == NULL) || (csio == NULL) || (str == NULL))
		return(NULL);

	if (str_len <= 0)
		return(NULL);

	/*
	 * If the CDB is a physical address, we can't deal with it..
	 */
	if ((csio->ccb_h.flags & CAM_CDB_PHYS) != 0)
		command_print = 0;
	else
		command_print = 1;

	cam_path_string(device, path_str, 64);

	str[0] = '\0';

	sense = NULL;

	if (command_print != 0) {
		char cdb_str[(SCSI_MAX_CDBLEN * 3) + 1];

		retlen = snprintf(tmpstr, tmpstrlen, "%s", path_str);

		if ((tmplen = str_len - cur_len - 1) < 0)
			goto sst_bailout;

		strncat(str, tmpstr, tmplen);
		cur_len += retlen;
		str[str_len - 1] = '\0';

		if ((csio->ccb_h.flags & CAM_CDB_POINTER) != 0) {
			retlen = snprintf(tmpstr, tmpstrlen, "%s. CDB: %s\n", 
					  scsi_op_desc(csio->cdb_io.cdb_ptr[0], 
						       &device->inq_data),
					  scsi_cdb_string(csio->cdb_io.cdb_ptr, 
							  cdb_str,
							  sizeof(cdb_str)));
		} else {
			retlen = snprintf(tmpstr, tmpstrlen, "%s. CDB: %s\n",
					 scsi_op_desc(csio->cdb_io.cdb_bytes[0],
					  &device->inq_data), scsi_cdb_string(
					  csio->cdb_io.cdb_bytes, cdb_str,
					  sizeof(cdb_str)));
		}

		if ((tmplen = str_len - cur_len - 1) < 0)
			goto sst_bailout;

		strncat(str, tmpstr, tmplen);
		cur_len += retlen;
		str[str_len - 1] = '\0';
	}

	/*
	 * If the sense data is a physical pointer, forget it.
	 */
	if (csio->ccb_h.flags & CAM_SENSE_PTR) {
		if (csio->ccb_h.flags & CAM_SENSE_PHYS)
			return(NULL);
		else {
			/* 
			 * XXX KDM this is stupid, but casting the
			 * structure doesn't work...
			 */
			bcopy(&csio->sense_data, sense, 
			      sizeof(struct scsi_sense_data *));
		}
	} else {
		/*
		 * If the physical sense flag is set, but the sense pointer
		 * is not also set, we assume that the user is an idiot and
		 * return.  (Well, okay, it could be that somehow, the
		 * entire csio is physical, but we would have probably core
		 * dumped on one of the bogus pointer deferences above
		 * already.)
		 */
		if (csio->ccb_h.flags & CAM_SENSE_PHYS) 
			return(NULL);
		else
			sense = &csio->sense_data;
	}


	retlen = snprintf(tmpstr, tmpstrlen, "%s", path_str);

	if ((tmplen = str_len - cur_len - 1) < 0)
		goto sst_bailout;

	strncat(str, tmpstr, tmplen);
	cur_len += retlen;
	str[str_len - 1] = '\0';

	error_code = sense->error_code & SSD_ERRCODE;
	sense_key = sense->flags & SSD_KEY;

	switch (error_code) {
	case SSD_DEFERRED_ERROR:
		retlen = snprintf(tmpstr, tmpstrlen, "Deferred Error: ");

		if ((tmplen = str_len - cur_len - 1) < 0)
			goto sst_bailout;

		strncat(str, tmpstr, tmplen);
		cur_len += retlen;
		str[str_len - 1] = '\0';
		/* FALLTHROUGH */
	case SSD_CURRENT_ERROR:

		retlen = snprintf(tmpstr, tmpstrlen, "%s", 
				  scsi_sense_key_text[sense_key]);

		if ((tmplen = str_len - cur_len - 1) < 0)
			goto sst_bailout;

		strncat(str, tmpstr, tmplen);
		cur_len += retlen;
		str[str_len - 1] = '\0';

		info = scsi_4btoul(sense->info);
		
		if (sense->error_code & SSD_ERRCODE_VALID) {

			switch (sense_key) {
			case SSD_KEY_NOT_READY:
			case SSD_KEY_ILLEGAL_REQUEST:
			case SSD_KEY_UNIT_ATTENTION:
			case SSD_KEY_DATA_PROTECT:
				break;
			case SSD_KEY_BLANK_CHECK:
				retlen = snprintf(tmpstr, tmpstrlen, 
						  " req sz: %d (decimal)", 
						  info);

				if ((tmplen = str_len - cur_len - 1) < 0)
					goto sst_bailout;

				strncat(str, tmpstr, tmplen);
				cur_len += retlen;
				str[str_len - 1] = '\0';
				break;
			default:
				if (info) {
					if (sense->flags & SSD_ILI) {
						retlen = snprintf (tmpstr,
								   tmpstrlen, 
								" ILI (length "
							"mismatch): %d", info);
			
					} else {
						retlen = snprintf(tmpstr,
								  tmpstrlen, 
								  " info:%x", 
								  info);
					}

					if ((tmplen = str_len - cur_len -1) < 0)
						goto sst_bailout;

					strncat(str, tmpstr, tmplen);
					cur_len += retlen;
 					str[str_len - 1] = '\0';
				}
			}
		} else if (info) {
			retlen = snprintf(tmpstr, tmpstrlen," info?:%x", info);

			if ((tmplen = str_len - cur_len -1) < 0)
				goto sst_bailout;

			strncat(str, tmpstr, tmplen);
			cur_len += retlen;
 			str[str_len - 1] = '\0';
		}

		if (sense->extra_len >= 4) {
			if (bcmp(sense->cmd_spec_info, "\0\0\0\0", 4)) {
				retlen = snprintf(tmpstr, tmpstrlen, 
						  " csi:%x,%x,%x,%x",
						  sense->cmd_spec_info[0],
						  sense->cmd_spec_info[1],
						  sense->cmd_spec_info[2],
						  sense->cmd_spec_info[3]);

				if ((tmplen = str_len - cur_len -1) < 0)
					goto sst_bailout;

				strncat(str, tmpstr, tmplen);
				cur_len += retlen;
				str[str_len - 1] = '\0';
			}
		}

		asc = (sense->extra_len >= 5) ? sense->add_sense_code : 0;
		ascq = (sense->extra_len >= 6) ? sense->add_sense_code_qual : 0;

		if (asc || ascq) {
			const char *desc = scsi_sense_desc(asc, ascq,
							   &device->inq_data);
			retlen = snprintf(tmpstr, tmpstrlen, 
					  " asc:%x,%x\n%s%s", asc, ascq, 
					  path_str, desc);

			if ((tmplen = str_len - cur_len -1) < 0)
				goto sst_bailout;

			strncat(str, tmpstr, tmplen);
			cur_len += retlen;
			str[str_len - 1] = '\0';
		}

		if (sense->extra_len >= 7 && sense->fru) {
			retlen = snprintf(tmpstr, tmpstrlen, 
					  " field replaceable unit: %x", 
					  sense->fru);

			if ((tmplen = str_len - cur_len -1) < 0)
				goto sst_bailout;

			strncat(str, tmpstr, tmplen);
			str[str_len - 1] = '\0';
			cur_len += retlen;
		}

		if ((sense->extra_len >= 10)
		 && (sense->sense_key_spec[0] & SSD_SCS_VALID) != 0) {
			retlen = snprintf(tmpstr, tmpstrlen, " sks:%x,%x", 
					sense->sense_key_spec[0],
			       		scsi_2btoul(&sense->sense_key_spec[1]));

			if ((tmplen = str_len - cur_len -1) < 0)
				goto sst_bailout;

			strncat(str, tmpstr, tmplen);
			str[str_len - 1] = '\0';
			cur_len += retlen;
		}
		break;

	default:
		retlen = snprintf(tmpstr, tmpstrlen, "error code %d",
				  sense->error_code & SSD_ERRCODE);

		if ((tmplen = str_len - cur_len -1) < 0)
			goto sst_bailout;

		strncat(str, tmpstr, tmplen);
		cur_len += retlen;
 		str[str_len - 1] = '\0';

		if (sense->error_code & SSD_ERRCODE_VALID) {
			retlen = snprintf(tmpstr, tmpstrlen, 
					  " at block no. %d (decimal)",
					  info = scsi_4btoul(sense->info));

			if ((tmplen = str_len - cur_len -1) < 0)
				goto sst_bailout;

			strncat(str, tmpstr, tmplen);
			cur_len += retlen;
 			str[str_len - 1] = '\0';
		}
	}

	retlen = snprintf(tmpstr, tmpstrlen, "\n");

	if ((tmplen = str_len - cur_len -1) < 0)
		goto sst_bailout;

	strncat(str, tmpstr, tmplen);
	cur_len += retlen;
 	str[str_len - 1] = '\0';

sst_bailout:

	return(str);
}

void
scsi_sense_print(struct cam_device *device, struct ccb_scsiio *csio, 
		 FILE *ofile)
{
	char str[2048];

	if ((device == NULL) || (csio == NULL) || (ofile == NULL))
		return;

	fprintf(ofile, "%s", scsi_sense_string(device, csio, str, 2048));
}

#endif /* _KERNEL/!_KERNEL */

#ifdef _KERNEL
int
scsi_interpret_sense(union ccb *ccb, u_int32_t sense_flags,
		     u_int32_t *relsim_flags, u_int32_t *openings,
		     u_int32_t *timeout, scsi_sense_action error_action)
#else
int
scsi_interpret_sense(struct cam_device *device, union ccb *ccb, 
		     u_int32_t sense_flags, u_int32_t *relsim_flags,
		     u_int32_t *openings, u_int32_t *timeout,
		     scsi_sense_action error_action)
#endif
{
	struct	   scsi_sense_data *sense;
	int	   error_code, sense_key, asc, ascq;
	int	   error;
	int	   print_sense;
	struct     ccb_scsiio *csio;
	int        retry;

	csio = &ccb->csio;
	sense = &csio->sense_data;
	scsi_extract_sense(sense, &error_code, &sense_key, &asc, &ascq);

#ifdef _KERNEL
	if (bootverbose) {
		sense_flags |= SF_PRINT_ALWAYS;
		print_sense = TRUE;
	} else if ((sense_flags & SF_NO_PRINT) == 0)
#else
	if ((sense_flags & SF_NO_PRINT) == 0)
#endif
		print_sense = TRUE;
	else
		print_sense = FALSE;

	switch (error_code) {
	case SSD_DEFERRED_ERROR:
	{
		/*
		 * XXX dufault@FreeBSD.org
		 * This error doesn't relate to the command associated
		 * with this request sense.  A deferred error is an error
		 * for a command that has already returned GOOD status
		 * (see 8.2.14.2).
		 *
		 * By my reading of that section, it looks like the current
		 * command has been cancelled, we should now clean things up
		 * (hopefully recovering any lost data) and then retry the
		 * current command.  There are two easy choices, both wrong:
		 *
		 * 1. Drop through (like we had been doing), thus treating
		 *    this as if the error were for the current command and
		 *    return and stop the current command.
		 * 
		 * 2. Issue a retry (like I made it do) thus hopefully
		 *    recovering the current transfer, and ignoring the
		 *    fact that we've dropped a command.
		 *
		 * These should probably be handled in a device specific
		 * sense handler or punted back up to a user mode daemon
		 */

		/* decrement the number of retries */
		retry = ccb->ccb_h.retry_count > 0;
		if (retry)
			ccb->ccb_h.retry_count--;

		error = ERESTART;
		break;
	}
	case SSD_CURRENT_ERROR:
	{
		
		switch (sense_key) {
		case SSD_KEY_NO_SENSE:
			/* Why were we called then? Well don't bail now */
			/* FALLTHROUGH */
		case SSD_KEY_EQUAL:
			/* These should be filtered by the peripheral drivers */
			print_sense = FALSE;
			/* FALLTHROUGH */
		case SSD_KEY_MISCOMPARE:
			/* decrement the number of retries */
			retry = ccb->ccb_h.retry_count > 0;
			if (retry) {
				error = ERESTART;
				ccb->ccb_h.retry_count--;
			} else {
				error = EIO;
			}
		case SSD_KEY_RECOVERED_ERROR:
			error = 0;	/* not an error */
			break;
		case SSD_KEY_ILLEGAL_REQUEST:
			if (((sense_flags & SF_QUIET_IR) != 0)
			 && ((sense_flags & SF_PRINT_ALWAYS) == 0))
				print_sense = FALSE;
			error = EINVAL;
			break;
		case SSD_KEY_NOT_READY:
		case SSD_KEY_DATA_PROTECT:
		case SSD_KEY_VOLUME_OVERFLOW:
		case SSD_KEY_BLANK_CHECK: /* should be filtered out by
					     peripheral drivers */
			retry = ccb->ccb_h.retry_count > 0;
			if (retry) {
				ccb->ccb_h.retry_count--;
				error = ERESTART;
				print_sense = FALSE;
			} else {
				if (((error_action & SSQ_PRINT_SENSE) == 0)
				 && ((sense_flags & SF_PRINT_ALWAYS) == 0))
					print_sense = FALSE;

				error = error_action & SS_ERRMASK;
			}

			break;
		case SSD_KEY_UNIT_ATTENTION:
			/*
			 * This should also be filtered out by
			 * peripheral drivers since each has a different
			 * concept of what it means to invalidate the media.
			 */
			if ((sense_flags & SF_RETRY_UA) != 0) {
				/* don't decrement retry count */
				error = ERESTART;
				print_sense = FALSE;
			} else {
				/* decrement the number of retries */
				retry = ccb->ccb_h.retry_count > 0;
				if (retry) {
					ccb->ccb_h.retry_count--;
					error = ERESTART;
					print_sense = FALSE;
				} else {
					if (((error_action &
					      SSQ_PRINT_SENSE) == 0)
					 && ((sense_flags &
					      SF_PRINT_ALWAYS) == 0))
						print_sense = FALSE;

					error = error_action & SS_ERRMASK;
				}
			}
			break;
		case SSD_KEY_ABORTED_COMMAND:
		default:
			/* decrement the number of retries */
			retry = ccb->ccb_h.retry_count > 0;
			if (retry) {
				ccb->ccb_h.retry_count--;
				error = ERESTART;
				print_sense = FALSE;
			} else {
				if (((error_action & SSQ_PRINT_SENSE) == 0)
				 && ((sense_flags & SF_PRINT_ALWAYS) == 0))
					print_sense = FALSE;

				error = error_action & SS_ERRMASK;
			}
			/*
			 * Make sure ABORTED COMMAND errors get
			 * printed as they're indicative of marginal
			 * SCSI busses that people should address.
			 */
			if (sense_key == SSD_KEY_ABORTED_COMMAND)
				print_sense = TRUE;
		}
		break;
	}
	default:
		/* decrement the number of retries */
		retry = ccb->ccb_h.retry_count > 0;
		if (retry) {
			ccb->ccb_h.retry_count--;
			error = ERESTART;
			print_sense = FALSE;
		} else 
			error = EIO;
		break;
	}

	if (print_sense) {
#ifdef _KERNEL
		scsi_sense_print(csio);
#else
		scsi_sense_print(device, csio, stdout);
#endif
	}
	
	return (error);
}

/*
 * This function currently requires at least 36 bytes, or
 * SHORT_INQUIRY_LENGTH, worth of data to function properly.  If this
 * function needs more or less data in the future, another length should be
 * defined in scsi_all.h to indicate the minimum amount of data necessary
 * for this routine to function properly.
 */
void
scsi_print_inquiry(struct scsi_inquiry_data *inq_data)
{
	u_int8_t type;
	char *dtype, *qtype;
	char vendor[16], product[48], revision[16], rstr[4];

	type = SID_TYPE(inq_data);

	/*
	 * Figure out basic device type and qualifier.
	 */
	if (SID_QUAL_IS_VENDOR_UNIQUE(inq_data)) {
		qtype = "(vendor-unique qualifier)";
	} else {
		switch (SID_QUAL(inq_data)) {
		case SID_QUAL_LU_CONNECTED:
			qtype = "";
			break;

		case SID_QUAL_LU_OFFLINE:
			qtype = "(offline)";
			break;

		case SID_QUAL_RSVD:
			qtype = "(reserved qualifier)";
			break;
		default:
		case SID_QUAL_BAD_LU:
			qtype = "(lun not supported)";
			break;
		}
	}

	switch (type) {
	case T_DIRECT:
		dtype = "Direct Access";
		break;
	case T_SEQUENTIAL:
		dtype = "Sequential Access";
		break;
	case T_PRINTER:
		dtype = "Printer";
		break;
	case T_PROCESSOR:
		dtype = "Processor";
		break;
	case T_CDROM:
		dtype = "CD-ROM";
		break;
	case T_WORM:
		dtype = "Worm";
		break;
	case T_SCANNER:
		dtype = "Scanner";
		break;
	case T_OPTICAL:
		dtype = "Optical";
		break;
	case T_CHANGER:
		dtype = "Changer";
		break;
	case T_COMM:
		dtype = "Communication";
		break;
	case T_STORARRAY:
		dtype = "Storage Array";
		break;
	case T_ENCLOSURE:
		dtype = "Enclosure Services";
		break;
	case T_RBC:
		dtype = "Simplified Direct Access";
		break;
	case T_OCRW:
		dtype = "Optical Card Read/Write";
		break;
	case T_NODEVICE:
		dtype = "Uninstalled";
	default:
		dtype = "unknown";
		break;
	}

	cam_strvis(vendor, inq_data->vendor, sizeof(inq_data->vendor),
		   sizeof(vendor));
	cam_strvis(product, inq_data->product, sizeof(inq_data->product),
		   sizeof(product));
	cam_strvis(revision, inq_data->revision, sizeof(inq_data->revision),
		   sizeof(revision));

	if (SID_ANSI_REV(inq_data) == SCSI_REV_CCS)
		bcopy("CCS", rstr, 4);
	else
		snprintf(rstr, sizeof (rstr), "%d", SID_ANSI_REV(inq_data));
	printf("<%s %s %s> %s %s SCSI-%s device %s\n",
	       vendor, product, revision,
	       SID_IS_REMOVABLE(inq_data) ? "Removable" : "Fixed",
	       dtype, rstr, qtype);
}

/*
 * Table of syncrates that don't follow the "divisible by 4"
 * rule. This table will be expanded in future SCSI specs.
 */
static struct {
	u_int period_factor;
	u_int period;	/* in 100ths of ns */
} scsi_syncrates[] = {
	{ 0x08, 625 },	/* FAST-160 */
	{ 0x09, 1250 },	/* FAST-80 */
	{ 0x0a, 2500 },	/* FAST-40 40MHz */
	{ 0x0b, 3030 },	/* FAST-40 33MHz */
	{ 0x0c, 5000 }	/* FAST-20 */
};

/*
 * Return the frequency in kHz corresponding to the given
 * sync period factor.
 */
u_int
scsi_calc_syncsrate(u_int period_factor)
{
	int i;
	int num_syncrates;

	num_syncrates = sizeof(scsi_syncrates) / sizeof(scsi_syncrates[0]);
	/* See if the period is in the "exception" table */
	for (i = 0; i < num_syncrates; i++) {

		if (period_factor == scsi_syncrates[i].period_factor) {
			/* Period in kHz */
			return (100000000 / scsi_syncrates[i].period);
		}
	}

	/*
	 * Wasn't in the table, so use the standard
	 * 4 times conversion.
	 */
	return (10000000 / (period_factor * 4 * 10));
}

/*
 * Return the SCSI sync parameter that corresponsd to
 * the passed in period in 10ths of ns.
 */
u_int
scsi_calc_syncparam(u_int period)
{
	int i;
	int num_syncrates;

	if (period == 0)
		return (~0);	/* Async */

	/* Adjust for exception table being in 100ths. */
	period *= 10;
	num_syncrates = sizeof(scsi_syncrates) / sizeof(scsi_syncrates[0]);
	/* See if the period is in the "exception" table */
	for (i = 0; i < num_syncrates; i++) {

		if (period <= scsi_syncrates[i].period) {
			/* Period in 100ths of ns */
			return (scsi_syncrates[i].period_factor);
		}
	}

	/*
	 * Wasn't in the table, so use the standard
	 * 1/4 period in ns conversion.
	 */
	return (period/400);
}

void
scsi_test_unit_ready(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_test_unit_ready *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_test_unit_ready *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = TEST_UNIT_READY;
}

void
scsi_request_sense(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   void *data_ptr, u_int8_t dxfer_len, u_int8_t tag_action,
		   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_request_sense *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_IN,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_request_sense *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = REQUEST_SENSE;
	scsi_cmd->length = dxfer_len;
}

void
scsi_inquiry(struct ccb_scsiio *csio, u_int32_t retries,
	     void (*cbfcnp)(struct cam_periph *, union ccb *),
	     u_int8_t tag_action, u_int8_t *inq_buf, u_int32_t inq_len,
	     int evpd, u_int8_t page_code, u_int8_t sense_len,
	     u_int32_t timeout)
{
	struct scsi_inquiry *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/inq_buf,
		      /*dxfer_len*/inq_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_inquiry *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = INQUIRY;
	if (evpd) {
		scsi_cmd->byte2 |= SI_EVPD;
		scsi_cmd->page_code = page_code;		
	}
	/*
	 * A 'transfer units' count of 256 is coded as
	 * zero for all commands with a single byte count
	 * field. 
	 */
	if (inq_len == 256)
		inq_len = 0;
	scsi_cmd->length = inq_len;
}

void
scsi_mode_sense(struct ccb_scsiio *csio, u_int32_t retries,
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		u_int8_t tag_action, int dbd, u_int8_t page_code,
		u_int8_t page, u_int8_t *param_buf, u_int32_t param_len,
		u_int8_t sense_len, u_int32_t timeout)
{
	return(scsi_mode_sense_len(csio, retries, cbfcnp, tag_action, dbd,
				   page_code, page, param_buf, param_len, 0,
				   sense_len, timeout));
}
void
scsi_mode_sense_len(struct ccb_scsiio *csio, u_int32_t retries,
		    void (*cbfcnp)(struct cam_periph *, union ccb *),
		    u_int8_t tag_action, int dbd, u_int8_t page_code,
		    u_int8_t page, u_int8_t *param_buf, u_int32_t param_len,
		    int minimum_cmd_size, u_int8_t sense_len, u_int32_t timeout)
{
	u_int8_t cdb_len;

	/*
	 * Use the smallest possible command to perform the operation.
	 */
	if ((param_len < 256)
	 && (minimum_cmd_size < 10)) {
		/*
		 * We can fit in a 6 byte cdb.
		 */
		struct scsi_mode_sense_6 *scsi_cmd;

		scsi_cmd = (struct scsi_mode_sense_6 *)&csio->cdb_io.cdb_bytes;
		bzero(scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->opcode = MODE_SENSE_6;
		if (dbd != 0)
			scsi_cmd->byte2 |= SMS_DBD;
		scsi_cmd->page = page_code | page;
		scsi_cmd->length = param_len;
		cdb_len = sizeof(*scsi_cmd);
	} else {
		/*
		 * Need a 10 byte cdb.
		 */
		struct scsi_mode_sense_10 *scsi_cmd;

		scsi_cmd = (struct scsi_mode_sense_10 *)&csio->cdb_io.cdb_bytes;
		bzero(scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->opcode = MODE_SENSE_10;
		if (dbd != 0)
			scsi_cmd->byte2 |= SMS_DBD;
		scsi_cmd->page = page_code | page;
		scsi_ulto2b(param_len, scsi_cmd->length);
		cdb_len = sizeof(*scsi_cmd);
	}
	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_IN,
		      tag_action,
		      param_buf,
		      param_len,
		      sense_len,
		      cdb_len,
		      timeout);
}

void
scsi_mode_select(struct ccb_scsiio *csio, u_int32_t retries,
		 void (*cbfcnp)(struct cam_periph *, union ccb *),
		 u_int8_t tag_action, int scsi_page_fmt, int save_pages,
		 u_int8_t *param_buf, u_int32_t param_len, u_int8_t sense_len,
		 u_int32_t timeout)
{
	return(scsi_mode_select_len(csio, retries, cbfcnp, tag_action,
				    scsi_page_fmt, save_pages, param_buf,
				    param_len, 0, sense_len, timeout));
}

void
scsi_mode_select_len(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, int scsi_page_fmt, int save_pages,
		     u_int8_t *param_buf, u_int32_t param_len,
		     int minimum_cmd_size, u_int8_t sense_len,
		     u_int32_t timeout)
{
	u_int8_t cdb_len;

	/*
	 * Use the smallest possible command to perform the operation.
	 */
	if ((param_len < 256)
	 && (minimum_cmd_size < 10)) {
		/*
		 * We can fit in a 6 byte cdb.
		 */
		struct scsi_mode_select_6 *scsi_cmd;

		scsi_cmd = (struct scsi_mode_select_6 *)&csio->cdb_io.cdb_bytes;
		bzero(scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->opcode = MODE_SELECT_6;
		if (scsi_page_fmt != 0)
			scsi_cmd->byte2 |= SMS_PF;
		if (save_pages != 0)
			scsi_cmd->byte2 |= SMS_SP;
		scsi_cmd->length = param_len;
		cdb_len = sizeof(*scsi_cmd);
	} else {
		/*
		 * Need a 10 byte cdb.
		 */
		struct scsi_mode_select_10 *scsi_cmd;

		scsi_cmd =
		    (struct scsi_mode_select_10 *)&csio->cdb_io.cdb_bytes;
		bzero(scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->opcode = MODE_SELECT_10;
		if (scsi_page_fmt != 0)
			scsi_cmd->byte2 |= SMS_PF;
		if (save_pages != 0)
			scsi_cmd->byte2 |= SMS_SP;
		scsi_ulto2b(param_len, scsi_cmd->length);
		cdb_len = sizeof(*scsi_cmd);
	}
	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_OUT,
		      tag_action,
		      param_buf,
		      param_len,
		      sense_len,
		      cdb_len,
		      timeout);
}


/* XXX allow specification of address and PMI bit and LBA */
void
scsi_read_capacity(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action,
		   struct scsi_read_capacity_data *rcap_buf,
		   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_read_capacity *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/(u_int8_t *)rcap_buf,
		      /*dxfer_len*/sizeof(*rcap_buf),
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_read_capacity *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = READ_CAPACITY;
}

/*
 * Prevent or allow the user to remove the media
 */
void
scsi_prevent(struct ccb_scsiio *csio, u_int32_t retries,
	     void (*cbfcnp)(struct cam_periph *, union ccb *),
	     u_int8_t tag_action, u_int8_t action,
	     u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_prevent *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_prevent *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = PREVENT_ALLOW;
	scsi_cmd->how = action;
}

/*
 * Syncronize the media to the contents of the cache for
 * the given lba/count pair.  Specifying 0/0 means sync
 * the whole cache.
 */
void
scsi_synchronize_cache(struct ccb_scsiio *csio, u_int32_t retries,
		       void (*cbfcnp)(struct cam_periph *, union ccb *),
		       u_int8_t tag_action, u_int32_t begin_lba,
		       u_int16_t lb_count, u_int8_t sense_len,
		       u_int32_t timeout)
{
	struct scsi_sync_cache *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_sync_cache *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = SYNCHRONIZE_CACHE;
	scsi_ulto4b(begin_lba, scsi_cmd->begin_lba);
	scsi_ulto2b(lb_count, scsi_cmd->lb_count);
}

void
scsi_read_write(struct ccb_scsiio *csio, u_int32_t retries,
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		u_int8_t tag_action, int readop, u_int8_t byte2,
		int minimum_cmd_size, u_int32_t lba, u_int32_t block_count,
		u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
		u_int32_t timeout)
{
	u_int8_t cdb_len;
	/*
	 * Use the smallest possible command to perform the operation
	 * as some legacy hardware does not support the 10 byte
	 * commands.  If any of the lower 5 bits in byte2 is set, we have
	 * to go with a larger command.
	 *
	 */
	if ((minimum_cmd_size < 10)
	 && ((lba & 0x1fffff) == lba)
	 && ((block_count & 0xff) == block_count)
	 && ((byte2 & 0xe0) == 0)) {
		/*
		 * We can fit in a 6 byte cdb.
		 */
		struct scsi_rw_6 *scsi_cmd;

		scsi_cmd = (struct scsi_rw_6 *)&csio->cdb_io.cdb_bytes;
		scsi_cmd->opcode = readop ? READ_6 : WRITE_6;
		scsi_ulto3b(lba, scsi_cmd->addr);
		scsi_cmd->length = block_count & 0xff;
		scsi_cmd->control = 0;
		cdb_len = sizeof(*scsi_cmd);

		CAM_DEBUG(csio->ccb_h.path, CAM_DEBUG_SUBTRACE,
			  ("6byte: %x%x%x:%d:%d\n", scsi_cmd->addr[0],
			   scsi_cmd->addr[1], scsi_cmd->addr[2],
			   scsi_cmd->length, dxfer_len));
	} else if ((minimum_cmd_size < 12)
		&& ((block_count & 0xffff) == block_count)) {
		/*
		 * Need a 10 byte cdb.
		 */
		struct scsi_rw_10 *scsi_cmd;

		scsi_cmd = (struct scsi_rw_10 *)&csio->cdb_io.cdb_bytes;
		scsi_cmd->opcode = readop ? READ_10 : WRITE_10;
		scsi_cmd->byte2 = byte2;
		scsi_ulto4b(lba, scsi_cmd->addr);
		scsi_cmd->reserved = 0;
		scsi_ulto2b(block_count, scsi_cmd->length);
		scsi_cmd->control = 0;
		cdb_len = sizeof(*scsi_cmd);

		CAM_DEBUG(csio->ccb_h.path, CAM_DEBUG_SUBTRACE,
			  ("10byte: %x%x%x%x:%x%x: %d\n", scsi_cmd->addr[0],
			   scsi_cmd->addr[1], scsi_cmd->addr[2],
			   scsi_cmd->addr[3], scsi_cmd->length[0],
			   scsi_cmd->length[1], dxfer_len));
	} else {
		/* 
		 * The block count is too big for a 10 byte CDB, use a 12
		 * byte CDB.  READ/WRITE(12) are currently only defined for
		 * optical devices.
		 */
		struct scsi_rw_12 *scsi_cmd;

		scsi_cmd = (struct scsi_rw_12 *)&csio->cdb_io.cdb_bytes;
		scsi_cmd->opcode = readop ? READ_12 : WRITE_12;
		scsi_cmd->byte2 = byte2;
		scsi_ulto4b(lba, scsi_cmd->addr);
		scsi_cmd->reserved = 0;
		scsi_ulto4b(block_count, scsi_cmd->length);
		scsi_cmd->control = 0;
		cdb_len = sizeof(*scsi_cmd);

		CAM_DEBUG(csio->ccb_h.path, CAM_DEBUG_SUBTRACE,
			  ("12byte: %x%x%x%x:%x%x%x%x: %d\n", scsi_cmd->addr[0],
			   scsi_cmd->addr[1], scsi_cmd->addr[2],
			   scsi_cmd->addr[3], scsi_cmd->length[0],
			   scsi_cmd->length[1], scsi_cmd->length[2],
			   scsi_cmd->length[3], dxfer_len));
	}
	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/readop ? CAM_DIR_IN : CAM_DIR_OUT,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      cdb_len,
		      timeout);
}

void 
scsi_start_stop(struct ccb_scsiio *csio, u_int32_t retries,
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		u_int8_t tag_action, int start, int load_eject,
		int immediate, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_start_stop_unit *scsi_cmd;
	int extra_flags = 0;

	scsi_cmd = (struct scsi_start_stop_unit *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = START_STOP_UNIT;
	if (start != 0) {
		scsi_cmd->how |= SSS_START;
		/* it takes a lot of power to start a drive */
		extra_flags |= CAM_HIGH_POWER;
	}
	if (load_eject != 0)
		scsi_cmd->how |= SSS_LOEJ;
	if (immediate != 0)
		scsi_cmd->byte2 |= SSS_IMMED;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_NONE | extra_flags,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

}


/*      
 * Try make as good a match as possible with
 * available sub drivers
 */
int
scsi_inquiry_match(caddr_t inqbuffer, caddr_t table_entry)
{
	struct scsi_inquiry_pattern *entry;
	struct scsi_inquiry_data *inq;
 
	entry = (struct scsi_inquiry_pattern *)table_entry;
	inq = (struct scsi_inquiry_data *)inqbuffer;

	if (((SID_TYPE(inq) == entry->type)
	  || (entry->type == T_ANY))
	 && (SID_IS_REMOVABLE(inq) ? entry->media_type & SIP_MEDIA_REMOVABLE
				   : entry->media_type & SIP_MEDIA_FIXED)
	 && (cam_strmatch(inq->vendor, entry->vendor, sizeof(inq->vendor)) == 0)
	 && (cam_strmatch(inq->product, entry->product,
			  sizeof(inq->product)) == 0)
	 && (cam_strmatch(inq->revision, entry->revision,
			  sizeof(inq->revision)) == 0)) {
		return (0);
	}
        return (-1);
}

/*      
 * Try make as good a match as possible with
 * available sub drivers
 */
int
scsi_static_inquiry_match(caddr_t inqbuffer, caddr_t table_entry)
{
	struct scsi_static_inquiry_pattern *entry;
	struct scsi_inquiry_data *inq;
 
	entry = (struct scsi_static_inquiry_pattern *)table_entry;
	inq = (struct scsi_inquiry_data *)inqbuffer;

	if (((SID_TYPE(inq) == entry->type)
	  || (entry->type == T_ANY))
	 && (SID_IS_REMOVABLE(inq) ? entry->media_type & SIP_MEDIA_REMOVABLE
				   : entry->media_type & SIP_MEDIA_FIXED)
	 && (cam_strmatch(inq->vendor, entry->vendor, sizeof(inq->vendor)) == 0)
	 && (cam_strmatch(inq->product, entry->product,
			  sizeof(inq->product)) == 0)
	 && (cam_strmatch(inq->revision, entry->revision,
			  sizeof(inq->revision)) == 0)) {
		return (0);
	}
        return (-1);
}
