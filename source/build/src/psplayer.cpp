// Sony PSP interface layer for the Build Engine
#include <signal.h>

#include "a.h"
#include "build.h"
#include "cache1d.h"
#include "compat.h"
#include "engine_priv.h"
#include "osd.h"
#include "palette.h"
#include "renderlayer.h"
#include "psp_inc.h"
#include "softsurface.h"
#include "joystick.h"

#ifdef USE_OPENGL
# include "glad/glad.h"
# include "glbuild.h"
# include "glsurface.h"
#endif

#include "vfs.h"

PSP_MODULE_INFO( "EDuke32", PSP_MODULE_USER, 1, 0 );
PSP_MAIN_THREAD_ATTR( PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU );
//PSP_MAIN_THREAD_STACK_SIZE_KB( 512 );
PSP_HEAP_SIZE_KB( -1 * 1024 );
PSP_NO_CREATE_MAIN_THREAD();

// Set frame buffer
#define PSP_FB_WIDTH     480
#define PSP_FB_HEIGHT    272
#define PSP_FB_BWIDTH    512
#define PSP_FB_FORMAT    PSP_DISPLAY_PIXEL_FORMAT_565 //4444,5551,565,8888

#if   PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_4444
#define PSP_FB_BPP    2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_5551
#define PSP_FB_BPP    2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_565
#define PSP_FB_BPP    2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_8888
#define PSP_FB_BPP    4
#endif

// undefine to restrict windowed resolutions to conventional sizes
#define ANY_WINDOWED_SIZE



// fix for mousewheel
int32_t inputchecked = 0;

char quitevent=0, appactive=1, novideo=0;

// video
static struct
{
    void *draw_buffer;
    void *disp_buffer;
}vid_psp;

int32_t xres=-1, yres=-1, bpp=0, fullscreen=0, bytesperline;
intptr_t frameplace=0;
int32_t lockcount=0;
char modechange=1;
char offscreenrendering=0;
char videomodereset = 0;
int32_t nofog=0;
#ifndef EDUKE32_GLES
static uint16_t sysgamma[3][256];
#endif
#ifdef USE_OPENGL
// OpenGL stuff
char nogl=0;
#endif
static int32_t vsync_renderlayer;
int32_t maxrefreshfreq=0;
static uint32_t currentVBlankInterval=0;

// last gamma, contrast, brightness
static float lastvidgcb[3];

//#define KEY_PRINT_DEBUG

static mutex_t m_initprintf;

// Joystick dead and saturation zones
uint16_t *joydead, *joysatur;

// Debug screen init
static bool g_dbgscreen = false;

int32_t wm_msgbox(const char *name, const char *fmt, ...)
{
    SceCtrlData pad;
    char buf[2048];
    va_list va;

    va_start(va,fmt);
    vsnprintf(buf,sizeof(buf),fmt,va);
    va_end(va);

    // Print message to the debug screen.
    if (!g_dbgscreen)
    {
        pspDebugScreenInit();
        g_dbgscreen = true;
    }
    pspDebugScreenSetTextColor(0x0080ff);
    pspDebugScreenPrintf("\n\n\n\n%s:\n", name);
    pspDebugScreenSetTextColor(0x0000ff);
    pspDebugScreenPrintData(buf, strlen(buf));
    pspDebugScreenSetTextColor(0xffffff);
    pspDebugScreenPrintf( "\n\nPress X to continue.\n" );
    
    // Wait for a button press.
    do
    {
        sceCtrlReadBufferPositive(&pad, 1);
    }
    while(!(pad.Buttons & PSP_CTRL_CROSS));
    pspDebugScreenClear();
    
    return 0;
}

int32_t wm_ynbox(const char *name, const char *fmt, ...)
{
    SceCtrlData pad;
    int32_t result = -1;
    char buf[2048];
    va_list va;

    va_start(va,fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    // Print message to the debug screen.
    if (!g_dbgscreen)
    {
        pspDebugScreenInit();
        g_dbgscreen = true;
    }
    pspDebugScreenSetTextColor(0x0080ff);
    pspDebugScreenPrintf("\n\n\n\n%s:\n", name);
    pspDebugScreenSetTextColor(0x0000ff);
    pspDebugScreenPrintData(buf, strlen(buf));
    pspDebugScreenSetTextColor(0xffffff);
    pspDebugScreenPrintf("\n\nPress X for yes, or O for no\n");
    
    // Wait for a button press.
    do
    {
        sceCtrlReadBufferPositive(&pad, 1);

        if(pad.Buttons & PSP_CTRL_CROSS) result = 1;
        else if(pad.Buttons & PSP_CTRL_CIRCLE) result = 0;
    else if(pad.Buttons & PSP_CTRL_START) result = 0;
    }
    while(result == -1);
    pspDebugScreenClear();
    
    return result;
}

void wm_setapptitle(const char *name)
{
    UNREFERENCED_PARAMETER(name);
}

//
//
// ---------------------------------------
//
// System
//
// ---------------------------------------
//
//
#ifdef __cplusplus
extern "C"
{
#endif
void G_Shutdown(void);
#ifdef __cplusplus
}
#endif

static int exitcallback(int arg1, int arg2, void *common)
{
    uninitsystem();
    sceKernelExitGame();

    return 0;
}

static void exitcallback(void)
{
    exitcallback(0, 0, NULL);
}

static int callbackthread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("Exit Callback", exitcallback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();

    return 0;
}

static int setupcallbacks( void )
{
    int thid = sceKernelCreateThread("update_thread", callbackthread, 0x11, 0xFA0, 0, 0);
    if(thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

static void sighandler(int signum)
{
    UNREFERENCED_PARAMETER(signum);

    app_crashhandler();
    uninitsystem();
    sceKernelExitGame();
}

int main(int argc, char *argv[])
{
#ifdef USE_OPENGL
    char *argp;

    if ((argp = Bgetenv("BUILD_NOFOG")) != NULL)
        nofog = Batol(argp);

#endif
    signal(SIGSEGV, sighandler);
    signal(SIGILL, sighandler);  /* clang -fcatch-undefined-behavior uses an ill. insn */
    signal(SIGABRT, sighandler);
    signal(SIGFPE, sighandler);

    maybe_redirect_outputs();

    return app_main(argc, (char const * const *)argv);
}

int32_t videoSetVsync(int32_t newSync)
{
    if (vsync_renderlayer == newSync)
        return newSync;

#ifdef USE_OPENGL
    if (sdl_context)
    {
        int result = SDL_GL_SetSwapInterval(newSync);

        if (result == -1)
        {
            if (newSync == -1)
            {
                newSync = 1;
                result = SDL_GL_SetSwapInterval(newSync);
            }

            if (result == -1)
            {
                newSync = 0;
                OSD_Printf("Unable to enable VSync!\n");
            }
        }

        vsync_renderlayer = newSync;
    }
    else
#endif
    {
        vsync_renderlayer = newSync;

        videoResetMode();
        if (videoSetGameMode(fullscreen, xres, yres, bpp, upscalefactor))
            OSD_Printf("restartvid: Reset failed...\n");
    }

    return newSync;
}

//
// initsystem() -- init system
//
int32_t initsystem(void)
{
    // disable fpu exceptions (division by zero and etc...)
    pspSdkDisableFPUExceptions();
    
    // exit callback thread
    setupcallbacks();
    
    // set max cpu/gpu frequency
    scePowerSetClockFrequency( 333, 333, 166 );

    mutex_init(&m_initprintf);

    atexit(exitcallback);

    frameplace = 0;
    lockcount = 0;

    if (!novideo)
    {
#ifdef USE_OPENGL
        if (SDL_GL_LoadLibrary(0))
        {
            initprintf("Failed loading OpenGL Driver.  GL modes will be unavailable. Error: %s\n", SDL_GetError());
            nogl = 1;
        }
#ifdef POLYMER
        if (loadglulibrary(getenv("BUILD_GLULIB")))
        {
            initprintf("Failed loading GLU.  GL modes will be unavailable. Error: %s\n", SDL_GetError());
            nogl = 1;
        }
#endif
#endif
        wm_setapptitle(apptitle); 
    }

    return 0;
}

//
// uninitsystem() -- uninit systems
//
void uninitsystem(void)
{
    uninitinput();
    timerUninit();

#ifdef USE_OPENGL
    SDL_GL_UnloadLibrary();
# ifdef POLYMER
    unloadglulibrary();
# endif
#endif
}

//
// system_getcvars() -- propagate any cvars that are read post-initialization
//
void system_getcvars(void)
{
    vsync = videoSetVsync(vsync);
}

//
// initprintf() -- prints a formatted string to the intitialization window
//
void initprintf(const char *f, ...)
{
    va_list va;
    char buf[2048];

    va_start(va, f);
    Bvsnprintf(buf, sizeof(buf), f, va);
    va_end(va);

    initputs(buf);
}


//
// initputs() -- prints a string to the intitialization window
//
void initputs(const char *buf)
{
    static char dabuf[2048];

    OSD_Puts(buf);
//    Bprintf("%s", buf);

    mutex_lock(&m_initprintf);
    if (Bstrlen(dabuf) + Bstrlen(buf) > 1022)
        Bmemset(dabuf, 0, sizeof(dabuf));

    Bstrcat(dabuf,buf);

    if (g_logFlushWindow || Bstrlen(dabuf) > 768)
        Bmemset(dabuf, 0, sizeof(dabuf));

    mutex_unlock(&m_initprintf);
}

//
// debugprintf() -- prints a formatted debug string to stderr
//
void debugprintf(const char *f, ...)
{
#if defined DEBUGGINGAIDS && !(defined __APPLE__ && defined __BIG_ENDIAN__)
    va_list va;

    va_start(va,f);
    Bvfprintf(stderr, f, va);
    va_end(va);
#else
    UNREFERENCED_PARAMETER(f);
#endif
}

//
//
// ---------------------------------------
//
// All things Input
//
// ---------------------------------------
//
//


//
// initinput() -- init input system
//
int32_t initinput(void)
{
    int32_t i;

    inputdevices |= 4; //1 | 2;  // keyboard (1) mouse (2) joystick (4)
    g_mouseGrabbed = 0;

    memset(g_keyNameTable, 0, sizeof(g_keyNameTable));
/*
    for (i = SDL_NUM_SCANCODES - 1; i >= 0; i--)
    {
        if (!keytranslation[i])
            continue;

        Bstrncpyz(g_keyNameTable[keytranslation[i]], SDL_GetKeyName(SDL_SCANCODE_TO_KEYCODE(i)), sizeof(g_keyNameTable[0]));
    }
*/
    // set up the controller.
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    
    // KEEPINSYNC duke3d/src/gamedefs.h, mact/include/_control.h
    joystick.numAxes = 2; // 9 - max
    joystick.numButtons = 13; // 32 - max
    joystick.numHats = 0; // (36-joystick.numButtons)/4 - max

    initprintf("Joystick 1 has %d axes, %d buttons, and %d hat(s).\n", joystick.numAxes, joystick.numButtons, joystick.numHats);

    joystick.pAxis = (int32_t *)Xcalloc(joystick.numAxes, sizeof(int32_t));

    if (joystick.numHats)
        joystick.pHat = (int32_t *)Xcalloc(joystick.numHats, sizeof(int32_t));

    for (i = 0; i < joystick.numHats; i++) joystick.pHat[i] = -1;  // centre

    joydead = (uint16_t *)Xcalloc(joystick.numAxes, sizeof(uint16_t));
    joysatur = (uint16_t *)Xcalloc(joystick.numAxes, sizeof(uint16_t));   

    return 0;
}

//
// uninitinput() -- uninit input system
//
void uninitinput(void)
{
    if(joystick.pAxis) Bfree(joystick.pAxis);
    joystick.pAxis = NULL;
    if(joystick.pHat) Bfree(joystick.pHat);
    joystick.pHat = NULL;
    if(joydead) Bfree(joydead);
    joydead = NULL;
    if(joysatur) Bfree(joysatur);
    joysatur = NULL;

    mouseUninit();
}

static const char *joynames[3][15] = {
    {
        "Left Stick X",
        "Left Stick Y"
    },
    {
        "Button SELECT",
        "Button START",
        "Button UP",
        "Button RIGHT",
        "Button DOWN",
        "Button LEFT",
        "Trigger L",
        "Trigger R",
        "Button TRIANGLE",
        "Button CIRCLE",
        "Button CROSS",
        "Button SQUARE",
        "Button HOME"
    },
    {
        "D-Pad Up",
        "D-Pad Right",
        "D-Pad Down",
        "D-Pad Left"
    }
};

const char *joyGetName(int32_t what, int32_t num)
{
    switch (what)
    {
        case 0:	// axis
            if ((unsigned)num > (unsigned)joystick.numAxes) return NULL;
            return joynames[0][num];

        case 1: // button
            if ((unsigned)num > (unsigned)joystick.numButtons) return NULL;
            return joynames[1][num];

        case 2: // hat
            if ((unsigned)num > (unsigned)joystick.numHats) return NULL;
            return joynames[2][num];

        default: return NULL;
    }
}

//
// initmouse() -- init mouse input
//
void mouseInit(void)
{
    mouseGrabInput(g_mouseEnabled = g_mouseLockedToWindow);  // FIXME - SA
}

//
// uninitmouse() -- uninit mouse input
//
void mouseUninit(void)
{
    mouseGrabInput(0);
    g_mouseEnabled = 0;
}

//
// grabmouse() -- show/hide mouse cursor
//
void mouseGrabInput(bool grab)
{
    g_mouseGrabbed = grab;
    g_mousePos.x = g_mousePos.y = 0;
}

void mouseLockToWindow(char a)
{
    if (!(a & 2))
    {
        mouseGrabInput(a);
        g_mouseLockedToWindow = g_mouseGrabbed;
    }
}

//
// setjoydeadzone() -- sets the dead and saturation zones for the joystick
//
void joySetDeadZone(int32_t axis, uint16_t dead, uint16_t satur)
{
    printf("SET --------- %d %d %d\n", axis, dead, satur);
    joydead[axis] = dead;
    joysatur[axis] = satur;
}


//
// getjoydeadzone() -- gets the dead and saturation zones for the joystick
//
void joyGetDeadZone(int32_t axis, uint16_t *dead, uint16_t *satur)
{
    *dead = joydead[axis];
    *satur = joysatur[axis];
}


//
//
// ---------------------------------------
//
// All things Timer
// Ken did this
//
// ---------------------------------------
//
//

static uint32_t timerfreq;
static uint32_t timerlastsample;
int32_t timerticspersec=0;
static double msperu64tick = 0;
static void(*usertimercallback)(void) = NULL;


//
// inittimer() -- initialize timer
//
int32_t timerInit(int32_t tickspersecond)
{
    u64 current_ticks;

    if (timerfreq) return 0;	// already installed
    
    sceRtcGetCurrentTick(&current_ticks);

//    initprintf("Initializing timer\n");
    timerfreq = 1000/*000*/;
    timerticspersec = tickspersecond;
    timerlastsample = ((uint32_t)current_ticks / 1000) * timerticspersec / timerfreq;

    usertimercallback = NULL;

    msperu64tick = 1000.0 / (double)timerGetFreqU64();

    return 0;
}

//
// uninittimer() -- shut down timer
//
void timerUninit(void)
{
    timerfreq = 0;
    msperu64tick = 0;
}

//
// sampletimer() -- update totalclock
//
void timerUpdate(void)
{
    u64 current_ticks;
    if (!timerfreq) return;

    sceRtcGetCurrentTick(&current_ticks);

    int32_t n = tabledivide64(((uint32_t)current_ticks / 1000) * timerticspersec, timerfreq) - timerlastsample;

    if (n <= 0) return;

    totalclock += n;
    timerlastsample += n;

    if (usertimercallback)
        for (; n > 0; n--) usertimercallback();
}

#if defined LUNATIC
//
// getticks() -- returns the sdl ticks count
//
uint32_t timerGetTicks(void)
{
    u64 current_ticks;
    sceRtcGetCurrentTick(&current_ticks);
    return (uint32_t)current_ticks / 1000;
}
#endif

// high-resolution timers for profiling
uint64_t timerGetTicksU64(void)
{
    u64 current_ticks;
    sceRtcGetCurrentTick(&current_ticks);
    return (uint32_t)current_ticks / 1000; // TODO!!!!!
}

uint64_t timerGetFreqU64(void)
{
    return 1000/*000*/;
}

// Returns the time since an unspecified starting time in milliseconds.
// (May be not monotonic for certain configurations.)
ATTRIBUTE((flatten))
double timerGetHiTicks(void)
{
    return (double)timerGetTicksU64() * msperu64tick;
}

//
// gettimerfreq() -- returns the number of ticks per second the timer is configured to generate
//
int32_t timerGetFreq(void)
{
    return timerticspersec;
}


//
// installusertimercallback() -- set up a callback function to be called when the timer is fired
//
void(*timerSetCallback(void(*callback)(void)))(void)
{
    void(*oldtimercallback)(void);

    oldtimercallback = usertimercallback;
    usertimercallback = callback;

    return oldtimercallback;
}

//
//
// ---------------------------------------
//
// All things Video
//
// ---------------------------------------
//
//

//
// getvalidmodes() -- figure out what video modes are available
//

#define ADDMODE(x,y,c)                                                                          \
    {                                                                                           \
        if (validmodecnt<MAXVALIDMODES)                                                         \
        {                                                                                       \
            validmode[validmodecnt].xdim=x;                                                     \
            validmode[validmodecnt].ydim=y;                                                     \
            validmode[validmodecnt].bpp=c;                                                      \
            validmode[validmodecnt].fs=1;                                                       \
            validmodecnt++;                                                                     \
        }                                                                                       \
    }

#define CHECKMODE(w,h) ((w < maxx) && (h < maxy))

static int sortmodes(const void *a_, const void *b_)
{

    auto a = (const struct validmode_t *)a_;
    auto b = (const struct validmode_t *)b_;

    int x;

    if ((x = a->fs   - b->fs)   != 0) return x;
    if ((x = a->bpp  - b->bpp)  != 0) return x;
    if ((x = a->xdim - b->xdim) != 0) return x;
    if ((x = a->ydim - b->ydim) != 0) return x;

    return 0;
}

static char modeschecked=0;
void videoGetModes(void)
{
    int32_t i, maxx = MAXXDIM, maxy = MAXYDIM;
   
    if (modeschecked || novideo)
        return;

    validmodecnt = 0;
    //    initprintf("Detecting video modes:\n");

    for (i = 0; g_defaultVideoModes[i].x; i++)
    {
        auto const &mode = g_defaultVideoModes[i];

        if(!CHECKMODE(mode.x, mode.y))
            continue;

        // 8-bit == Software, 32-bit == OpenGL
        ADDMODE(mode.x, mode.y, 8);

#ifdef USE_OPENGL
        if (nogl)
            continue;

        ADDMODE(mode.x, mode.y, 32);
#endif
    }

    qsort((void *)validmode, validmodecnt, sizeof(struct validmode_t), &sortmodes);

    modeschecked = 1;
}

//
// checkvideomode() -- makes sure the video mode passed is legal
//
int32_t videoCheckMode(int32_t *x, int32_t *y, int32_t c, int32_t fs, int32_t forced)
{
    int32_t i, nearest=-1, dx, dy, odx=9999, ody=9999;

    videoGetModes();

    if (c>8
#ifdef USE_OPENGL
            && nogl
#endif
       ) return -1;

    // fix up the passed resolution values to be multiples of 8
    // and at least 320x200 or at most MAXXDIMxMAXYDIM
    *x = clamp(*x, 320, MAXXDIM);
    *y = clamp(*y, 200, MAXYDIM);

    for (i = 0; i < validmodecnt; i++)
    {
        if (validmode[i].bpp != c || validmode[i].fs != fs)
            continue;

        dx = klabs(validmode[i].xdim - *x);
        dy = klabs(validmode[i].ydim - *y);

        if (!(dx | dy))
        {
            // perfect match
            nearest = i;
            break;
        }

        if ((dx <= odx) && (dy <= ody))
        {
            nearest = i;
            odx = dx;
            ody = dy;
        }
    }

#ifdef ANY_WINDOWED_SIZE
    if (!forced && (fs&1) == 0 && (nearest < 0 || (validmode[nearest].xdim!=*x || validmode[nearest].ydim!=*y)))
        return 0x7fffffffl;
#endif

    if (nearest < 0)
        return -1;

    *x = validmode[nearest].xdim;
    *y = validmode[nearest].ydim;

    return nearest;
}

static void destroy_window_resources()
{

}

#ifdef USE_OPENGL
void sdlayer_setvideomode_opengl(void)
{
    glsurface_destroy();
    polymost_glreset();

    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);  // GL_FLAT
    glClearColor(0, 0, 0, 1.0);  // Black Background
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);  // Use FASTEST for ortho!
//    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

#ifndef EDUKE32_GLES
    glDisable(GL_DITHER);
#endif

    glinfo.vendor = (const char *) glGetString(GL_VENDOR);
    glinfo.renderer = (const char *) glGetString(GL_RENDERER);
    glinfo.version = (const char *) glGetString(GL_VERSION);
    glinfo.extensions = (const char *) glGetString(GL_EXTENSIONS);

#ifdef POLYMER
    if (!Bstrcmp(glinfo.vendor, "ATI Technologies Inc."))
    {
        pr_ati_fboworkaround = 1;
        initprintf("Enabling ATI FBO color attachment workaround.\n");

        if (Bstrstr(glinfo.renderer, "Radeon X1"))
        {
            pr_ati_nodepthoffset = 1;
            initprintf("Enabling ATI R520 polygon offset workaround.\n");
        }
        else
            pr_ati_nodepthoffset = 0;
#ifdef __APPLE__
        // See bug description at http://lists.apple.com/archives/mac-opengl/2005/Oct/msg00169.html
        if (!Bstrncmp(glinfo.renderer, "ATI Radeon 9600", 15))
        {
            pr_ati_textureformat_one = 1;
            initprintf("Enabling ATI Radeon 9600 texture format workaround.\n");
        }
        else
            pr_ati_textureformat_one = 0;
#endif
    }
    else
        pr_ati_fboworkaround = 0;
#endif  // defined POLYMER

    glinfo.maxanisotropy = 1.0;
    glinfo.bgra = 0;
    glinfo.clamptoedge = 1;
    glinfo.multitex = 1;

    // process the extensions string and flag stuff we recognize

    glinfo.texnpot = !!Bstrstr(glinfo.extensions, "GL_ARB_texture_non_power_of_two") || !!Bstrstr(glinfo.extensions, "GL_OES_texture_npot");
    glinfo.multisample = !!Bstrstr(glinfo.extensions, "GL_ARB_multisample");
    glinfo.nvmultisamplehint = !!Bstrstr(glinfo.extensions, "GL_NV_multisample_filter_hint");
    glinfo.arbfp = !!Bstrstr(glinfo.extensions, "GL_ARB_fragment_program");
    glinfo.depthtex = !!Bstrstr(glinfo.extensions, "GL_ARB_depth_texture");
    glinfo.shadow = !!Bstrstr(glinfo.extensions, "GL_ARB_shadow");
    glinfo.fbos = !!Bstrstr(glinfo.extensions, "GL_EXT_framebuffer_object") || !!Bstrstr(glinfo.extensions, "GL_OES_framebuffer_object");

#if !defined EDUKE32_GLES
    glinfo.texcompr = !!Bstrstr(glinfo.extensions, "GL_ARB_texture_compression") && Bstrcmp(glinfo.vendor, "ATI Technologies Inc.");
# ifdef DYNAMIC_GLEXT
    if (glinfo.texcompr && (!glCompressedTexImage2D || !glGetCompressedTexImage))
    {
        // lacking the necessary extensions to do this
        initprintf("Warning: the GL driver lacks necessary functions to use caching\n");
        glinfo.texcompr = 0;
    }
# endif

    glinfo.bgra = !!Bstrstr(glinfo.extensions, "GL_EXT_bgra");
    glinfo.clamptoedge = !!Bstrstr(glinfo.extensions, "GL_EXT_texture_edge_clamp") ||
                         !!Bstrstr(glinfo.extensions, "GL_SGIS_texture_edge_clamp");
    glinfo.rect =
    !!Bstrstr(glinfo.extensions, "GL_NV_texture_rectangle") || !!Bstrstr(glinfo.extensions, "GL_EXT_texture_rectangle");

    glinfo.multitex = !!Bstrstr(glinfo.extensions, "GL_ARB_multitexture");

    glinfo.envcombine = !!Bstrstr(glinfo.extensions, "GL_ARB_texture_env_combine");
    glinfo.vbos = !!Bstrstr(glinfo.extensions, "GL_ARB_vertex_buffer_object");
    glinfo.sm4 = !!Bstrstr(glinfo.extensions, "GL_EXT_gpu_shader4");
    glinfo.occlusionqueries = !!Bstrstr(glinfo.extensions, "GL_ARB_occlusion_query");
    glinfo.glsl = !!Bstrstr(glinfo.extensions, "GL_ARB_shader_objects");
    glinfo.debugoutput = !!Bstrstr(glinfo.extensions, "GL_ARB_debug_output");
    glinfo.bufferstorage = !!Bstrstr(glinfo.extensions, "GL_ARB_buffer_storage");
    glinfo.sync = !!Bstrstr(glinfo.extensions, "GL_ARB_sync");

    if (Bstrstr(glinfo.extensions, "WGL_3DFX_gamma_control"))
    {
        static int32_t warnonce;
        // 3dfx cards have issues with fog
        nofog = 1;
        if (!(warnonce & 1))
            initprintf("3dfx card detected: OpenGL fog disabled\n");
        warnonce |= 1;
    }
#else
    // don't bother checking because ETC2 et al. are not listed in extensions anyway
    glinfo.texcompr = 1; // !!Bstrstr(glinfo.extensions, "GL_OES_compressed_ETC1_RGB8_texture");
#endif

//    if (Bstrstr(glinfo.extensions, "GL_EXT_texture_filter_anisotropic"))
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glinfo.maxanisotropy);

    if (!glinfo.dumped)
    {
        int32_t oldbpp = bpp;
        bpp = 32;
        osdcmd_glinfo(NULL);
        glinfo.dumped = 1;
        bpp = oldbpp;
    }
}
#endif  // defined USE_OPENGL

//
// setvideomode() -- set video mode
//

int32_t setvideomode_common(int32_t *x, int32_t *y, int32_t c, int32_t fs, int32_t *regrab)
{
    if ((fs == fullscreen) && (*x == xres) && (*y == yres) && (c == bpp) && !videomodereset)
        return 0;

    if (g_mouseGrabbed)
    {
        *regrab = 1;
        mouseGrabInput(0);
    }

    while (lockcount) videoEndDrawing();

#ifdef USE_OPENGL
    if (sdl_surface)
    {
        if (bpp > 8)
            polymost_glreset();
    }
    if (!nogl)
    {
        if (bpp == 8)
            glsurface_destroy();
        if ((fs == fullscreen) && (*x == xres) && (*y == yres) && (bpp != 0) && !videomodereset)
            return 0;
    }
    else
#endif
    {
       softsurface_destroy();
    }

    // clear last gamma/contrast/brightness so that it will be set anew
    lastvidgcb[0] = lastvidgcb[1] = lastvidgcb[2] = 0.0f;

    return 1;
}

void setvideomode_commonpost(int32_t x, int32_t y, int32_t c, int32_t fs, int32_t regrab)
{
#ifdef USE_OPENGL
    if (!nogl)
        sdlayer_setvideomode_opengl();
#endif

    xres = x;
    yres = y;
    bpp = c;
    fullscreen = fs;
    // bytesperline = sdl_surface->pitch;
    numpages = c > 8 ? 2 : 1;
    frameplace = 0;
    lockcount = 0;
    modechange = 1;
    videomodereset = 0;
#if 0
    // save the current system gamma to determine if gamma is available
#ifndef EDUKE32_GLES
    if (!gammabrightness)
    {
        //        float f = 1.0 + ((float)curbrightness / 10.0);
        if (SDL_GetWindowGammaRamp(sdl_window, sysgamma[0], sysgamma[1], sysgamma[2]) == 0)
            gammabrightness = 1;

        // see if gamma really is working by trying to set the brightness
        if (gammabrightness && videoSetGamma() < 0)
            gammabrightness = 0;  // nope
    }
#endif
#endif
    videoFadePalette(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);

    if (regrab)
        mouseGrabInput(g_mouseLockedToWindow);
}

void setrefreshrate(void)
{
#if 0
    SDL_DisplayMode dispmode;
    SDL_GetCurrentDisplayMode(0, &dispmode);

    dispmode.refresh_rate = maxrefreshfreq;

    SDL_DisplayMode newmode;
    SDL_GetClosestDisplayMode(0, &dispmode, &newmode);

    if (dispmode.refresh_rate != newmode.refresh_rate)
    {
        initprintf("Refresh rate: %dHz\n", newmode.refresh_rate);
        SDL_SetWindowDisplayMode(sdl_window, &newmode);
    }

    if (!newmode.refresh_rate)
        newmode.refresh_rate = 60;

    currentVBlankInterval = 1000/newmode.refresh_rate;
#endif
}

int32_t videoSetMode(int32_t x, int32_t y, int32_t c, int32_t fs)
{
    int32_t regrab = 0, ret;

    x = 480; // dest width
    y = 272; // dest height
    c = 8; // src bpp
    
    ret = setvideomode_common(&x, &y, c, fs, &regrab);
    if (ret != 1)
    {
        if (ret == 0)
        {
            setvideomode_commonpost(x, y, c, fs, regrab);
        }
        return ret;
    }

    // deinit
    destroy_window_resources();

    initprintf("Setting video mode %dx%d (%d-bpp %s)\n", x, y, c, ((fs & 1) ? "fullscreen" : "windowed"));

    vid_psp.draw_buffer = (void*)malloc( PSP_FB_HEIGHT * PSP_FB_BWIDTH * PSP_FB_BPP );
    if( !vid_psp.draw_buffer )
    {
        initprintf( "Memory allocation failled! (vid_psp.draw_buffer)\n");
    return -1;
    }

    vid_psp.disp_buffer = (void*)malloc( PSP_FB_HEIGHT * PSP_FB_BWIDTH * PSP_FB_BPP );
    if( !vid_psp.disp_buffer )
    {
        initprintf( "Memory allocation failled! (vid_psp.disp_buffer)\n");
    return -1;
    }
    
    sceDisplaySetMode(0, PSP_FB_WIDTH, PSP_FB_HEIGHT);
    sceDisplaySetFrameBuf(vid_psp.disp_buffer, PSP_FB_BWIDTH, PSP_FB_FORMAT, 1);

    setvideomode_commonpost(x, y, c, fs, regrab);

    return 0;
}

//
// resetvideomode() -- resets the video system
//
void videoResetMode(void)
{
    videomodereset = 1;
    modeschecked = 0;
}

//
// begindrawing() -- locks the framebuffer for drawing
//

#ifdef DEBUG_FRAME_LOCKING
uint32_t begindrawing_line[BEGINDRAWING_SIZE];
const char *begindrawing_file[BEGINDRAWING_SIZE];
void begindrawing_real(void)
#else
void videoBeginDrawing(void)
#endif
{
    if (bpp > 8)
    {
        if (offscreenrendering) return;
        frameplace = 0;
        bytesperline = 0;
        modechange = 0;
        return;
    }

    // lock the frame
    if (lockcount++ > 0)
        return;

    if (offscreenrendering) return;

#ifdef USE_OPENGL
    if (!nogl)
    {
        frameplace = (intptr_t)glsurface_getBuffer();
        if (modechange)
        {
            bytesperline = xdim;
            calc_ylookup(bytesperline, ydim);
            modechange=0;
        }
        return;
    }
#endif

    frameplace = (intptr_t)softsurface_getBuffer();
    if (modechange)
    {
        bytesperline = xdim;
        calc_ylookup(bytesperline, ydim);
        modechange=0;
    }
}


//
// enddrawing() -- unlocks the framebuffer
//
void videoEndDrawing(void)
{
    if (bpp > 8)
    {
        if (!offscreenrendering) frameplace = 0;
        return;
    }

    if (!frameplace) return;
    if (lockcount > 1) { lockcount--; return; }
    if (!offscreenrendering) frameplace = 0;
    if (lockcount == 0) return;
    lockcount = 0;
}

//
// showframe() -- update the display
//
void videoShowFrame(int32_t w)
{
    UNREFERENCED_PARAMETER(w);

#ifdef USE_OPENGL
    if (!nogl)
    {
        if (bpp > 8)
        {
            if (palfadedelta)
                fullscreen_tint_gl(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);
        }
        else
        {
            glsurface_blitBuffer();
        }

        SDL_GL_SwapWindow(sdl_window);
        if (vsync)
        {
            static uint32_t lastSwapTime = 0;
            // busy loop until we're ready to update again
            while (SDL_GetTicks()-lastSwapTime < currentVBlankInterval) {}
            lastSwapTime = SDL_GetTicks();
        }
        return;
    }
#endif

    if (offscreenrendering) return;

    if (lockcount)
    {
        printf("Frame still locked %d times when showframe() called.\n", lockcount);
        while (lockcount) videoEndDrawing();
    }

    softsurface_blitBuffer((uint32_t*) vid_psp.draw_buffer, PSP_FB_BPP * 8, PSP_FB_BWIDTH);

    void* p_swap = vid_psp.disp_buffer;
    vid_psp.disp_buffer = vid_psp.draw_buffer;
    vid_psp.draw_buffer = p_swap;
    
    sceKernelDcacheWritebackInvalidateAll();
    sceDisplaySetFrameBuf(vid_psp.disp_buffer, PSP_FB_BWIDTH, PSP_FB_FORMAT, PSP_DISPLAY_SETBUF_NEXTFRAME);
    
    //sceDisplayWaitVblankStart();
}

//
// setpalette() -- set palette values
//
int32_t videoUpdatePalette(int32_t start, int32_t num)
{
    UNREFERENCED_PARAMETER(start);
    UNREFERENCED_PARAMETER(num);

    if (bpp > 8)
        return 0;  // no palette in opengl

#ifdef USE_OPENGL
    if (!nogl)
        glsurface_setPalette(curpalettefaded);
    else
#endif
    {
        softsurface_setPalette(curpalettefaded, 0x0000001F, 0x000007E0, 0x0000F800); // 565 format
    }

    return 0;
}

//
// setgamma
//
int32_t videoSetGamma(void)
{
#if 1
    return 0;
#else
    if (novideo)
        return 0;

    int32_t i;
    uint16_t gammaTable[768];
    float gamma = max(0.1f, min(4.f, g_videoGamma));
    float contrast = max(0.1f, min(3.f, g_videoContrast));
    float bright = max(-0.8f, min(0.8f, g_videoBrightness));

    float invgamma = 1.f / gamma;
    float norm = powf(255.f, invgamma - 1.f);

    if (lastvidgcb[0] == gamma && lastvidgcb[1] == contrast && lastvidgcb[2] == bright)
        return 0;

    // This formula is taken from Doomsday

    for (i = 0; i < 256; i++)
    {
        float val = i * contrast - (contrast - 1.f) * 127.f;
        if (gamma != 1.f)
            val = powf(val, invgamma) / norm;

        val += bright * 128.f;

        gammaTable[i] = gammaTable[i + 256] = gammaTable[i + 512] = (uint16_t)max(0.f, min(65535.f, val * 256.f));
    }

    i = INT32_MIN;

    if (sdl_window)
        i = SDL_SetWindowGammaRamp(sdl_window, &gammaTable[0], &gammaTable[256], &gammaTable[512]);

    if (i < 0)
    {
#ifndef __ANDROID__  // Don't do this check, it is really supported, TODO
/*
        if (i != INT32_MIN)
            initprintf("Unable to set gamma: SDL_SetWindowGammaRamp failed: %s\n", SDL_GetError());
*/
#endif

        OSD_Printf("videoSetGamma(): %s\n", SDL_GetError());

#ifndef EDUKE32_GLES
        if (sdl_window)
            SDL_SetWindowGammaRamp(sdl_window, &sysgamma[0][0], &sysgamma[1][0], &sysgamma[2][0]);
        gammabrightness = 0;
#endif
    }
    else
    {
        lastvidgcb[0] = gamma;
        lastvidgcb[1] = contrast;
        lastvidgcb[2] = bright;

        gammabrightness = 1;
    }

    return i;
#endif
}

//
//
// ---------------------------------------
//
// Miscellany
//
// ---------------------------------------
//
//

int32_t handleevents_peekkeys(void)
{
/*
    SDL_PumpEvents();

    return SDL_PeepEvents(NULL, 1, SDL_PEEKEVENT, SDL_KEYDOWN, SDL_KEYDOWN);
*/
    return 0;
}

void handleevents_updatemousestate(uint8_t state)
{
    g_mouseClickState = state ? MOUSE_RELEASED : MOUSE_PRESSED;
}

//
// handleevents() -- process the SDL message queue
//   returns !0 if there was an important event worth checking (like quitting)
//

uint32_t translate_scejoy[]=
{
    PSP_CTRL_SELECT  ,  
    PSP_CTRL_START   ,
    PSP_CTRL_UP      ,
    PSP_CTRL_RIGHT   ,
    PSP_CTRL_DOWN    ,
    PSP_CTRL_LEFT    ,
    PSP_CTRL_LTRIGGER,
    PSP_CTRL_RTRIGGER,
    PSP_CTRL_TRIANGLE,
    PSP_CTRL_CIRCLE  ,
    PSP_CTRL_CROSS   ,
    PSP_CTRL_SQUARE  ,
    PSP_CTRL_HOME
/*
    PSP_CTRL_HOLD    ,
    PSP_CTRL_NOTE    ,
    PSP_CTRL_SCREEN  ,
    PSP_CTRL_VOLUP   ,
    PSP_CTRL_VOLDOWN ,
    PSP_CTRL_WLAN_UP ,
    PSP_CTRL_REMOTE  ,
    PSP_CTRL_DISC    ,
    PSP_CTRL_MS
*/
};

int32_t handleevents(void)
{
    uint32_t i;
    SceCtrlData buf;
    static uint32_t last_buttons;

    if (inputchecked && g_mouseEnabled)
    {
        if (g_mouseCallback)
        {
            if (g_mouseBits & 16)
                g_mouseCallback(5, 0);
            if (g_mouseBits & 32)
                g_mouseCallback(6, 0);
        }
        g_mouseBits &= ~(16 | 32);
    }
 
    sceCtrlPeekBufferPositive(&buf, 1);

    if(last_buttons != buf.Buttons)
    {
        for(i = 0; i < sizeof(translate_scejoy)/sizeof(uint32_t); i++)
        {
            if(buf.Buttons & translate_scejoy[i])
                joystick.bits |= 1 << i;
            else
                joystick.bits &= ~(1 << i);   
        }
    }
    last_buttons = buf.Buttons;
    
    joystick.pAxis[0] = (buf.Lx - 128) * 10000 / 128;
    joystick.pAxis[1] = (buf.Ly - 128) * 10000 / 128;
    //printf("dz: %d(%#08X) sat: %d(%#08X)\n", joydead[0], joydead[0], joysatur[0], joysatur[0]);
    for(i = 0; i < joystick.numAxes; i++)
    {
        

        /*joystick.pAxis[i] = axis[i];*/
        if ((joystick.pAxis[i] < joydead[i]) &&
            (joystick.pAxis[i] > -joydead[i]))
            joystick.pAxis[i] = 0;
        else if (joystick.pAxis[i] >= joysatur[i])
            joystick.pAxis[i] = 10000;
        else if (joystick.pAxis[i] <= -joysatur[i])
            joystick.pAxis[i] = -10000;
        else
            joystick.pAxis[i] = joystick.pAxis[i] * 10000 / joysatur[i];
    }

    inputchecked = 0;
    timerUpdate();

    return 0;
}
