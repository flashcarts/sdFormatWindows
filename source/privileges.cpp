// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <cstdlib>
#include <unistd.h>
#include "verbose_printf.h"



void dropPrivileges(void)
{
	/* FCNET CHANGE START - stub function that does not work in Windows. Run program as administrator. */
#if 0
	const int uid = getuid();
	if(uid == -1) abort();
	else if(uid != 0) // Drop privilges when running as set-user-ID program.
	{
		verbosePuts("Dropping privileges...");
		if(setgid(getgid()) == -1) abort();
		if(setuid(uid) == -1) abort();

		// Extra paranoid check.
		if(setuid(0) == 0) abort();
	}
#endif
	/* FCNET CHANGE END - stub function that does not work in Windows. Run program as administrator. */
}
