/*
 Copyright (C) 2009 Jonathon Fowler <jf@jonof.id.au>
 Copyright (C) 2022 Sergey Galushko

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

/**
 * PSP Audio output driver for MultiVoc
 */

#include "compat.h"
#include "psp_inc.h"
#include "driver_psp.h"
#include "multivoc.h"

#define PSP_NUM_AUDIO_SAMPLES 1024 // must be multiple of 64
#define PSP_OUTPUT_CHANNELS 2
#define PSP_OUTPUT_BUFFER_SIZE ((PSP_NUM_AUDIO_SAMPLES) * (PSP_OUTPUT_CHANNELS))

static struct
{
    SceUID threadUID;
    SceUID semaUID;
    int32_t channel;
    volatile int32_t volL;
    volatile int32_t volR;
    volatile int32_t running;
} psp_driver = { -1, -1, -1, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, 1 };

static int16_t psp_driver_buff[2][PSP_OUTPUT_BUFFER_SIZE]  __attribute__((aligned(64)));

static int32_t ErrorCode = PSPErr_Ok;
static int32_t Initialised = 0;
static int32_t Playing = 0;

static char *MixBuffer = 0;
static int32_t MixBufferSize = 0;
static int32_t MixBufferCount = 0;
static int32_t MixBufferCurrent = 0;
static int32_t MixBufferUsed = 0;
static void ( *MixCallBack )( void ) = 0;

static inline void fillData(int chan, void *ptr, int remaining, void *udata)
{
    int32_t len;
    char *sptr;

    UNREFERENCED_PARAMETER(chan);
    UNREFERENCED_PARAMETER(udata);

    if (!MixBuffer || !MixCallBack)
        return;

    sceKernelWaitSema(psp_driver.semaUID, 1, NULL);

    while (remaining > 0)
    {
        if (MixBufferUsed == MixBufferSize)
        {
            MixCallBack();

            MixBufferUsed = 0;
            MixBufferCurrent++;
            if (MixBufferCurrent >= MixBufferCount)
                MixBufferCurrent -= MixBufferCount;
        }

        while (remaining > 0 && MixBufferUsed < MixBufferSize)
        {
            sptr = MixBuffer + (MixBufferCurrent * MixBufferSize) + MixBufferUsed;

            len = MixBufferSize - MixBufferUsed;
            if (remaining < len)
                len = remaining;

            memcpy(ptr, sptr, len);

            ptr = (void *)((uintptr_t)(ptr) + len);
            MixBufferUsed += len;
            remaining -= len;
        }
    }

    sceKernelSignalSema(psp_driver.semaUID, 1);
}

static int fillDataThread(SceSize args, void *argp)
{
    int index = 0;

    UNREFERENCED_PARAMETER(args);
    UNREFERENCED_PARAMETER(argp);

    while(psp_driver.running)
    {
        fillData(0, psp_driver_buff[index], PSP_OUTPUT_BUFFER_SIZE * sizeof(int16_t), NULL);
        sceAudioOutputPannedBlocking(psp_driver.channel, psp_driver.volL, psp_driver.volR, psp_driver_buff[index]);
        index = !index; // swap
    }
    sceKernelExitThread(0);
    return 0;
}

int32_t PSPDrv_GetError(void)
{
    return ErrorCode;
}

const char *PSPDrv_ErrorString( int32_t ErrorNumber )
{
    const char *ErrorString;

    switch( ErrorNumber )
    {
        case PSPErr_Warning:
        case PSPErr_Error:
            ErrorString = PSPDrv_ErrorString( ErrorCode );
            break;

        case PSPErr_Ok:
            ErrorString = "PSP Audio ok.";
            break;

        case PSPErr_Uninitialised:
            ErrorString = "PSP Audio uninitialised.";
            break;

        case PSPErr_ChannelReserve:
            ErrorString = "PSP Audio: channel reserve failed";
            break;

        case PSPErr_CreateSemaphore:
            ErrorString = "PSP Audio: failed creating mix semaphore.";
            break;

        case PSPErr_CreateThread:
            ErrorString = "PSP Audio: failed creating mix thread.";
            break;

        case PSPErr_StartThread:
            ErrorString = "PSP Audio: failed starting mix thread.";
            break;

        default:
            ErrorString = "Unknown PSP Audio error code.";
            break;
    }

    return ErrorString;
}

int32_t PSPDrv_PCM_Init(int32_t *mixrate, int32_t *numchannels, void * initdata)
{
    //int32_t chunksize;

    UNREFERENCED_PARAMETER(numchannels);
    UNREFERENCED_PARAMETER(initdata);

    if (Initialised)
        PSPDrv_PCM_Shutdown();
/*
    chunksize = 512;

    if (*mixrate >= 16000) chunksize *= 2;
    if (*mixrate >= 32000) chunksize *= 2;

    int intmixrate = *mixrate;
    int intnumchannels = *numchannels;

    *mixrate = intmixrate;
    *numchannels = intnumchannels;
*/
    *mixrate = 44100;
    *numchannels = 2;

    // allocate and initialize a hardware output channel
    psp_driver.channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, PSP_NUM_AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if(psp_driver.channel < 0)
    {
       ErrorCode = PSPErr_ChannelReserve;
       return PSPErr_Error;
    }

    // create semaphore
    psp_driver.semaUID = sceKernelCreateSema("sound_sema", 0, 1, 255, NULL);
    if(psp_driver.semaUID <= 0)
    {
       ErrorCode = PSPErr_CreateSemaphore;
       return PSPErr_Error;
    }

    Initialised = 1;

    return PSPErr_Ok;
}

void PSPDrv_PCM_Shutdown(void)
{
    if (!Initialised)
        return;

    psp_driver.running = 0;

    if(psp_driver.threadUID >= 0)
    {
        sceKernelWaitThreadEnd(psp_driver.threadUID, NULL);
        sceKernelDeleteThread(psp_driver.threadUID);
        psp_driver.threadUID = -1;
    }

    if(psp_driver.semaUID > 0)
    {
        sceKernelDeleteSema(psp_driver.semaUID);
        psp_driver.semaUID = -1;
    }

    if(psp_driver.channel >= 0)
    {
        sceAudioChRelease(psp_driver.channel);
        psp_driver.channel = -1;
    }

    Initialised = 0;
}

int32_t PSPDrv_PCM_BeginPlayback(char *BufferStart, int32_t BufferSize,
                        int32_t NumDivisions, void ( *CallBackFunc )( void ) )
{
    if (!Initialised)
    {
        ErrorCode = PSPErr_Uninitialised;
        return PSPErr_Error;
    }

    if (Playing)
        PSPDrv_PCM_StopPlayback();

    MixBuffer = BufferStart;
    MixBufferSize = BufferSize;
    MixBufferCount = NumDivisions;
    MixBufferCurrent = 0;
    MixBufferUsed = 0;
    MixCallBack = CallBackFunc;

    // pure the buffer
    memset(psp_driver_buff, 0, sizeof(psp_driver_buff));
    psp_driver.running = 1;

    // create audio thread
    psp_driver.threadUID = sceKernelCreateThread("sound_thread", fillDataThread, 0x11, 0x8000, 0, 0);
    if(psp_driver.threadUID < 0)
    {
        ErrorCode = PSPErr_CreateThread;
        return PSPErr_Error;
    }

    // start audio thread
    if(sceKernelStartThread(psp_driver.threadUID, 0, 0) < 0)
    {
        ErrorCode = PSPErr_StartThread;
        return PSPErr_Error;
    }

    Playing = 1;

    return PSPErr_Ok;
}

void PSPDrv_PCM_StopPlayback(void)
{
    if (!Initialised || !Playing)
        return;

    psp_driver.running = 0;

    if(psp_driver.threadUID >= 0)
    {
        sceKernelWaitThreadEnd(psp_driver.threadUID, NULL);
        sceKernelDeleteThread(psp_driver.threadUID);
        psp_driver.threadUID = -1;
    }

    Playing = 0;
}

void PSPDrv_PCM_Lock(void)
{
    if(psp_driver.semaUID > 0)
        sceKernelWaitSema(psp_driver.semaUID, 1, NULL);
}

void PSPDrv_PCM_Unlock(void)
{
    if(psp_driver.semaUID > 0)
        sceKernelSignalSema(psp_driver.semaUID, 1);
}
