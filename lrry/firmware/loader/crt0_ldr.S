/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "FELUCCA-LOADER-1"	/* 16 bytes: the firmware recognises its loader in a package by this (ota.c). TRANSITION (2026-11-07): the
					   firmware on the units still checks for this old marker, so the FIRST build after 2026-11-07 keeps it here;
					   once that build is installed, change this line to "L/RRY LOADER 1.0" (ota.c, fm1_install.py, make_site.py
					   and web/install already accept both). */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .
