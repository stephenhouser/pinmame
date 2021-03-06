#include "PinMAMEdll.h"

//============================================================
// Console Debugging Section (Optional)
//============================================================

#if defined(_WIN32)
#define ENABLE_CONSOLE_DEBUG
#ifdef ENABLE_CONSOLE_DEBUG

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

static bool useConsole = false;

void ShowConsole()
{
	if (useConsole)
		return;
	FILE* pConsole;
	AllocConsole();
	freopen_s(&pConsole, "CONOUT$", "wb", stdout);
	useConsole = true;
}

void CloseConsole()
{
	useConsole = false;
	if (useConsole)
		FreeConsole();
}
#endif
#else
#define MAX_PATH          1024
#endif

#include <thread>
#include <../win32com/Alias.h> //!! move that one to some platform independent section

extern "C"
{
	// MAME headers
	#include "driver.h"
	#include "window.h"
	#include "input.h"
	#include "minimalconfig.h"
	#include "rc.h"
	#include "core.h"
	#include "vpintf.h"
	#include "mame.h"
	#include "dllsound.h"
	#include "cpuexec.h"

	extern unsigned char g_raw_dmdbuffer[DMD_MAXY*DMD_MAXX];
	extern unsigned int g_raw_colordmdbuffer[DMD_MAXY*DMD_MAXX];
	extern unsigned int g_raw_dmdx;
	extern unsigned int g_raw_dmdy;
	extern unsigned int g_needs_DMD_update;

	volatile int g_fHandleKeyboard = 0;
	volatile int g_fHandleMechanics = 0;
	extern int trying_to_quit;

	void OnSolenoid(int nSolenoid, int IsActive);
	void OnStateChange(int nChange);
	extern void win_timer_enable(int enabled);

	UINT8 win_trying_to_quit;
	volatile int g_fPause = 0;
	volatile int g_fDumpFrames = 0;
	volatile char g_fShowWinDMD = 0;
	volatile char g_fShowPinDMD = 0; /* pinDMD */

	char g_szGameName[256] = { 0 }; // String containing requested game name (may be different from ROM if aliased)

	extern int channels;

	struct rc_struct *rc;
}

static char vpmPath[MAX_PATH] = { 0 };
static int sampleRate = 48000;

static volatile bool isGameReady = false;

static std::thread* pRunningGame = nullptr;

static int initialSwitches[CORE_MAXSWCOL*8 * 2]; // for each switch: number and state (0 or 1)
static int initialSwitchesToSet = 0;

//============================================================
// Callback tests Section
//============================================================

void OnSolenoid(int nSolenoid, int IsActive)
{
	printf("Solenoid: %d %s\n", nSolenoid, (IsActive > 0 ? "ON" : "OFF"));
}

void OnStateChange(int nChange)
{
	printf("OnStateChange : %d\n", nChange);

	// if game is ready to go, set the switches that may have been set to an initial state before game startup via SetSwitches
	if (nChange > 0)
	{
		for (int i = 0; i < initialSwitchesToSet; ++i)
			vp_putSwitch(initialSwitches[i*2], initialSwitches[i*2+1] ? 1 : 0);
		initialSwitchesToSet = 0;
	}

	isGameReady = (nChange > 0);
}


//============================================================
//	Game related Section
//============================================================

int GetGameNumFromString(char *name)
{
	int gamenum = 0;
	while (drivers[gamenum]) {
		if (!_stricmp(drivers[gamenum]->name, name))
			break;
		gamenum++;
	}
	if (!drivers[gamenum])
		return -1;
	else
		return gamenum;
}

char* composePath(const char* path, const char* file)
{
	size_t pathl = strlen(path);
	size_t filel = strlen(file);
	char *r = (char*)malloc(pathl + filel + 4);

	strcpy(r, path);
	strcpy(r+pathl, file);
	return r;
}

int set_option(const char *name, const char *arg, int priority)
{
	return rc_set_option(rc, name, arg, priority);
}


//============================================================
//	Running game thread
//============================================================
void gameThread(int game_index=-1)
{
	if (game_index == -1)
		return;
	/*int res =*/ run_game(game_index);
}


//============================================================
//	Dll Exported Functions Section
//============================================================

// Setup Functions
// ---------------

PINMAMEDLL_API void SetVPMPath(char* path)
{
	strcpy_s(vpmPath, path);
}

PINMAMEDLL_API void SetSampleRate(int sampleRate)
{
	sampleRate = sampleRate;
}


// Game related functions
// ---------------------

PINMAMEDLL_API int StartThreadedGame(char* gameNameOrg, bool showConsole)
{
	if (pRunningGame)
		return -1;

#ifdef ENABLE_CONSOLE_DEBUG
	if (showConsole)
		ShowConsole();
#endif
	trying_to_quit = 0;

	rc = cli_rc_create();

	strcpy_s(g_szGameName, gameNameOrg);
	const char* const gameName = checkGameAlias(g_szGameName);

	const int game_index = GetGameNumFromString(const_cast<char*>(gameName));
	if (game_index < 0)
		return -1;

	//options.skip_disclaimer = 1;
	//options.skip_gameinfo = 1;
	options.samplerate = sampleRate;

	win_timer_enable(1);
	g_fPause = 0;

	set_option("throttle", "1", 0);
	set_option("sleep", "1", 0);
	set_option("autoframeskip", "0", 0);
	set_option("skip_gameinfo", "1", 0);
	set_option("skip_disclaimer", "1", 0);

	printf("VPM path: %s\n", vpmPath);
	setPath(FILETYPE_ROM, composePath(vpmPath, "roms"));
	setPath(FILETYPE_NVRAM, composePath(vpmPath, "nvram"));
	setPath(FILETYPE_SAMPLE, composePath(vpmPath, "samples"));
	setPath(FILETYPE_CONFIG, composePath(vpmPath, "cfg"));
	setPath(FILETYPE_HIGHSCORE, composePath(vpmPath, "hi"));
	setPath(FILETYPE_INPUTLOG, composePath(vpmPath, "inp"));
	setPath(FILETYPE_MEMCARD, composePath(vpmPath, "memcard"));
	setPath(FILETYPE_STATE, composePath(vpmPath, "sta"));

	vp_init();

	printf("GameIndex: %d\n", game_index);
	pRunningGame = new std::thread(gameThread, game_index);

	return game_index;
}

void StopThreadedGame(bool locking)
{
	if (pRunningGame == nullptr)
		return;

	trying_to_quit = 1;
	OnStateChange(0);

	if (locking)
	{
		printf("Waiting for clean exit...\n");
		pRunningGame->join();
	}

	delete(pRunningGame);
	pRunningGame = nullptr;

	//rc_unregister(rc, opts);
	rc_destroy(rc);

	g_szGameName[0] = '\0';

#ifdef ENABLE_CONSOLE_DEBUG
	CloseConsole();
#endif
}

PINMAMEDLL_API bool IsGameReady()
{
	return isGameReady;
}

// Pause related functions
// -----------------------
PINMAMEDLL_API void ResetGame()
{
	if (isGameReady)
		machine_reset();
}

PINMAMEDLL_API void Pause()
{
	g_fPause = 1;
}

PINMAMEDLL_API void Continue()
{
	g_fPause = 0;
}

PINMAMEDLL_API bool IsPaused()
{
	return g_fPause > 0;
}


// DMD related functions
// ---------------------

PINMAMEDLL_API bool NeedsDMDUpdate()
{
	return isGameReady && !!g_needs_DMD_update;
}

PINMAMEDLL_API int GetRawDMDWidth()
{
	return isGameReady ? g_raw_dmdx : ~0u;
}

PINMAMEDLL_API int GetRawDMDHeight()
{
	return isGameReady ? g_raw_dmdy : ~0u;
}

PINMAMEDLL_API int GetRawDMDPixels(unsigned char* buffer)
{
	if (!isGameReady || g_raw_dmdx == ~0u || g_raw_dmdy == ~0u)
		return -1;
	memcpy(buffer, g_raw_dmdbuffer, g_raw_dmdx*g_raw_dmdy * sizeof(unsigned char));
	g_needs_DMD_update = 0;
	return g_raw_dmdx*g_raw_dmdy;
}


// Audio related functions
// -----------------------
PINMAMEDLL_API int GetAudioChannels()
{
	return isGameReady ? channels : -1;
}

PINMAMEDLL_API int GetPendingAudioSamples(float* buffer, int outChannels, int maxNumber)
{
	return isGameReady ? fillAudioBuffer(buffer, outChannels, maxNumber, 1) : -1;
}

PINMAMEDLL_API int GetPendingAudioSamples16bit(signed short* buffer, int outChannels, int maxNumber)
{
	return isGameReady ? fillAudioBuffer(buffer, outChannels, maxNumber, 0) : -1;
}

// Switch related functions
// ------------------------
PINMAMEDLL_API bool GetSwitch(int slot)
{
	return isGameReady ? (vp_getSwitch(slot) != 0) : false;
}

PINMAMEDLL_API void SetSwitch(int slot, bool state)
{
	if (!isGameReady)
		return;
	vp_putSwitch(slot, state ? 1 : 0);
}

PINMAMEDLL_API void SetSwitches(int* states, int numSwitches)
{
	if (!isGameReady) // initial state, potentially before game was fully initialized, set this under the hood later-on
	{
		for (int i = 0; i < numSwitches*2; ++i)
			initialSwitches[i] = states[i];
		initialSwitchesToSet = numSwitches;
	}
	else
		for(int i = 0; i < numSwitches; ++i)
			vp_putSwitch(states[i*2], states[i*2+1] ? 1 : 0);
}

// Lamps related functions
// -----------------------
PINMAMEDLL_API int GetMaxLamps() { return CORE_MAXLAMPCOL * 8; }

PINMAMEDLL_API int GetChangedLamps(int* changedStates)
{
	if (!isGameReady)
		return -1;

	vp_tChgLamps chgLamps;
	const int uCount = vp_getChangedLamps(chgLamps);
	if (uCount == 0)
		return 0;

	int* out = changedStates;
	for (int i = 0; i < uCount; i++)
	{
		*(out++) = chgLamps[i].lampNo;
		*(out++) = chgLamps[i].currStat;
	}
	return uCount;
}

// Solenoids related functions
// ---------------------------
PINMAMEDLL_API int GetMaxSolenoids() { return 64; }

PINMAMEDLL_API int GetChangedSolenoids(int* changedStates)
{
	if (!isGameReady)
		return -1;

	vp_tChgSols chgSols;
	const int uCount = vp_getChangedSolenoids(chgSols);
	if (uCount == 0)
		return 0;

	int* out = changedStates;
	for (int i = 0; i < uCount; i++)
	{
		*(out++) = chgSols[i].solNo;
		*(out++) = chgSols[i].currStat;
	}
	return uCount;
}

// GI strings related functions
// ----------------------------
PINMAMEDLL_API int GetMaxGIStrings() { return CORE_MAXGI; }

PINMAMEDLL_API int GetChangedGIs(int* changedStates)
{
	if (!isGameReady)
		return -1;

	vp_tChgGIs chgGIs;
	const int uCount = vp_getChangedGI(chgGIs);
	if (uCount == 0)
		return 0;

	int* out = changedStates;
	for (int i = 0; i < uCount; i++)
	{
		*(out++) = chgGIs[i].giNo;
		*(out++) = chgGIs[i].currStat;
	}
	return uCount;
}

//============================================================
//	osd_init
//============================================================

int osd_init(void)
{
	printf("osd_init\n");
	return 0;
}

//============================================================
//	osd_exit
//============================================================

void osd_exit(void)
{
	printf("osd_exit\n");
}
