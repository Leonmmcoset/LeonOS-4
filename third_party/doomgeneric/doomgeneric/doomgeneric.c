#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);

#if defined(__GNUC__) || defined(__clang__)
#define DG_WEAK __attribute__((weak))
#else
#define DG_WEAK
#endif

// Platforms can replace this hook with a native loading screen.
DG_WEAK void DG_StartupProgress(uint32_t progress, const char *message)
{
    (void)progress;
    (void)message;
}

void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	if (DG_ScreenBuffer == NULL)
	{
		exit(1);
	}

	DG_Init();
	DG_StartupProgress(8, "Preparing DOOM");

	D_DoomMain ();
}
