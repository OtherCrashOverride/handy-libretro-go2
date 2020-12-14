/*
handy-go2 - Handy (Atary Lynx) port for the ODROID-GO Advance
Copyright (C) 2020  OtherCrashOverride

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <go2/display.h>
#include <go2/audio.h>
#include <go2/input.h>
#include <drm/drm_fourcc.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <pwd.h>

#include "../lynx/system.h"
#include "../lynx/lynxdef.h"


static go2_display_t* display;
static go2_presenter_t* presenter;
static go2_audio_t* audio;
static go2_input_t* go2input;
static go2_gamepad_state_t gamepadState;
static go2_gamepad_state_t previousState;

static volatile bool isRunning = true;


static bool newFrame = false;
uint16_t* framebuffer; //[HANDY_SCREEN_WIDTH * HANDY_SCREEN_HEIGHT];


#define SOUND_FREQUENCY HANDY_AUDIO_SAMPLE_FREQ
#define SOUND_CHANNEL_COUNT (2)
void InitSound()
{
    printf("Sound: SOUND_FREQUENCY=%d\n", SOUND_FREQUENCY);

	audio = go2_audio_create(SOUND_FREQUENCY);
}

void ProcessAudio(uint8_t* samples, int frameCount)
{
    go2_audio_submit(audio, (const short*)samples, frameCount);
}

static void InitJoysticks()
{
    go2input = go2_input_create();
}

static void ReadJoysticks()
{
    go2_input_gamepad_read(go2input, &gamepadState);

    if (gamepadState.buttons.f1)
        isRunning = false;
}

static const char* FileNameFromPath(const char* fullpath)
{
    // Find last slash
    const char* ptr = strrchr(fullpath,'/');
    if (!ptr)
    {
        ptr = fullpath;
    }
    else
    {
        ++ptr;
    } 

    return ptr;   
}

static char* PathCombine(const char* path, const char* filename)
{
    int len = strlen(path);
    int total_len = len + strlen(filename);

    char* result = NULL;

    if (path[len-1] != '/')
    {
        ++total_len;
        result = (char*)calloc(total_len + 1, 1);
        strcpy(result, path);
        strcat(result, "/");
        strcat(result, filename);
    }
    else
    {
        result = (char*)calloc(total_len + 1, 1);
        strcpy(result, path);
        strcat(result, filename);
    }
    
    return result;
}

static int LoadState(CSystem* lynx, const char* saveName)
{
    FILE* file = fopen(saveName, "rb");
	if (!file) return -1;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

    if (size < 1) return -1;

    void* ptr = malloc(size);
    if (!ptr) abort();

    size_t count = fread(ptr, 1, size, file);
    if ((size_t)size != count)
    {
        free(ptr);
        abort();
    }

    fclose(file);

    LSS_FILE fp;
    fp.memptr = (UBYTE*)ptr;
    fp.index = 0;
    fp.index_limit = size;

    lynx->ContextLoad(&fp);
    free(ptr);

    printf("LoadState: loaded.\n");
    return 0;
}

static void SaveState(CSystem* lynx, const char* saveName)
{
    //lynx->ContextSave(saveName);
    size_t size = lynx->ContextSize();
    void* data = malloc(size);

    LSS_FILE fp;
    fp.memptr = (UBYTE*)data;
    fp.index = 0;
    fp.index_limit = size;

    lynx->ContextSave(&fp);

    FILE* file = fopen(saveName, "wb");
    size_t count = fwrite(data, 1, size, file);
    if (count != size)
    {
        free(data);
        abort();
    }

    fclose(file);
    free(data);
}

static UBYTE* lynx_display_callback(ULONG objref)
{
    if(gAudioBufferPointer > 0)
    {
        //printf("%s: gAudioBufferPointer=%lu\n", __func__, gAudioBufferPointer);
#if 1
        int f = gAudioBufferPointer / 4; // /1 - 8 bit mono, /2 8 bit stereo, /4 16 bit stereo
        ProcessAudio(gAudioBuffer, f);
        gAudioBufferPointer = 0;
#endif

    }

    newFrame = true;
    return (UBYTE*)framebuffer;
}

int main (int argc, char **argv)
{
    // Print help if no game specified
    if(argc < 2)
    {
        printf("USAGE: %s romfile\n", argv[0]);
        return 1;
    }


    display = go2_display_create();
    int dw = go2_display_height_get(display);
    int dh = go2_display_width_get(display);

    presenter = go2_presenter_create(display, DRM_FORMAT_RGB565, 0xff080808);

    go2_surface_t* fbsurface = go2_surface_create(display, HANDY_SCREEN_WIDTH * 3, HANDY_SCREEN_HEIGHT * 3, DRM_FORMAT_RGB565);
    framebuffer = (uint16_t*)malloc(HANDY_SCREEN_WIDTH * HANDY_SCREEN_HEIGHT * sizeof(uint16_t)); //(uint16_t*)go2_surface_map(fbsurface);
    if (!framebuffer) abort();


    InitSound();
    InitJoysticks();

    
    const char* romfile = argv[1];
    const char* biosfilename = ""; //"lynxboot.img";

    CSystem* lynx = new CSystem(romfile, biosfilename, true);
    lynx->DisplaySetAttributes(MIKIE_NO_ROTATE, MIKIE_PIXEL_FORMAT_16BPP_565, HANDY_SCREEN_WIDTH * 2, lynx_display_callback, (ULONG)0);

    gAudioEnabled = true;
 

    const char* fileName = FileNameFromPath(romfile);
    
    char* saveName = (char*)malloc(strlen(fileName) + 4 + 1);
    strcpy(saveName, fileName);
    strcat(saveName, ".sav");


    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;

    char* savePath = PathCombine(homedir, saveName);
    printf("savePath='%s'\n", savePath);
    LoadState(lynx, savePath);


    int totalFrames = 0;
    double totalElapsed = 0.0;

    //Stopwatch_Reset();
    //Stopwatch_Start();
    int sw = dh * ((float)HANDY_SCREEN_WIDTH / (float)HANDY_SCREEN_HEIGHT);
    if (sw > dw) sw = dw;
    
    isRunning = true;
    while(isRunning)
    {
        ReadJoysticks();

        const float TRIM = 0.5f;
        if (gamepadState.thumb.y < -TRIM) gamepadState.dpad.up = ButtonState_Pressed;
        if (gamepadState.thumb.y > TRIM) gamepadState.dpad.down = ButtonState_Pressed;
        if (gamepadState.thumb.x < -TRIM) gamepadState.dpad.left = ButtonState_Pressed;
        if (gamepadState.thumb.x > TRIM) gamepadState.dpad.right = ButtonState_Pressed;


        unsigned long data = 0;

        if (gamepadState.buttons.a) data |= BUTTON_A;
        if (gamepadState.buttons.b) data |= BUTTON_B;

        if (gamepadState.buttons.f5) data |= BUTTON_OPT1;
        if (gamepadState.buttons.f6) data |= BUTTON_OPT2;
        if (gamepadState.buttons.f3) data |= BUTTON_PAUSE;

        if (gamepadState.dpad.left) data |= BUTTON_LEFT;
        if (gamepadState.dpad.right) data |= BUTTON_RIGHT;
        if (gamepadState.dpad.up) data |= BUTTON_UP;
        if (gamepadState.dpad.down) data |= BUTTON_DOWN;

        lynx->SetButtonData(data);


        while (!newFrame) lynx->Update();
        newFrame = false;


        uint16_t* src = framebuffer;
        const int srcStride = HANDY_SCREEN_WIDTH;

        uint16_t* dst = (uint16_t*)go2_surface_map(fbsurface);
        const int dstStride = HANDY_SCREEN_WIDTH * 3;       

        for (int y = 0; y < HANDY_SCREEN_HEIGHT; ++y)
        {
            for (int x = 0; x < HANDY_SCREEN_WIDTH; ++x)
            {
                uint16_t pixel = src[x];
                dst[x * 3] = pixel;
                dst[x * 3 + 1] = pixel;
#if 1
                const uint8_t R = (pixel >> 11) >> 1;
                const uint8_t G = (pixel >> 5 & 0x3f) >> 1;
                const uint8_t B = (pixel & 0x1f) >> 1;
                dst[x * 3 + 2] = (R << 11) | (G << 5) | B;
#else
                dst[x * 3 + 2] = pixel;
#endif
            }

            src += srcStride;

            memcpy(dst + dstStride, dst, dstStride * sizeof(uint16_t));
            memcpy(dst + dstStride * 2, dst, dstStride * sizeof(uint16_t));
            dst += dstStride * 3;
        }

        go2_presenter_post( presenter,
                            fbsurface,
                            0, 0, HANDY_SCREEN_WIDTH * 3, HANDY_SCREEN_HEIGHT * 3,
                            //(320 / 2) - (HANDY_SCREEN_HEIGHT * 3 / 2), 0, HANDY_SCREEN_HEIGHT * 3, HANDY_SCREEN_WIDTH * 3,
                            0, (dw / 2) - (sw / 2), dh, sw,
                            GO2_ROTATION_DEGREES_270);
        ++totalFrames;

#if 0
        // Measure FPS
        totalElapsed += Stopwatch_Elapsed();

        if (totalElapsed >= 1.0)
        {
            int fps = (int)(totalFrames / totalElapsed);
            fprintf(stderr, "FPS: %i\n", fps);

            totalFrames = 0;
            totalElapsed = 0;
        }

        Stopwatch_Reset();
#endif
    }

    SaveState(lynx, savePath);
    free(savePath);

    return 0;
}
