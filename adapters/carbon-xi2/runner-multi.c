#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static atomic_bool running = true;
static void *sdl;
static void (*set_touch)(uint16_t, int16_t, int16_t, int16_t, int16_t, uint8_t, uint8_t);
#define LOAD(name) __typeof__(&name) p_##name = dlsym(sdl, #name)

/* Android's synthetic touch pad occupies player 1 in this release. Mirror the
 * physical pad and X11 keyboard into that pad using the SDK's public setter. */
static void *input_thread(void *unused) {
    (void)unused;
    LOAD(SDL_WasInit); LOAD(SDL_GetGamepads); LOAD(SDL_OpenGamepad);
    LOAD(SDL_CloseGamepad); LOAD(SDL_GamepadConnected); LOAD(SDL_GetGamepadAxis);
    LOAD(SDL_GetGamepadButton); LOAD(SDL_free); LOAD(SDL_GetKeyboardState);
    LOAD(SDL_GetKeyboardFocus);
    if (!p_SDL_WasInit || !p_SDL_GetGamepads || !p_SDL_OpenGamepad ||
        !p_SDL_CloseGamepad || !p_SDL_GamepadConnected || !p_SDL_GetGamepadAxis ||
        !p_SDL_GetGamepadButton || !p_SDL_free || !p_SDL_GetKeyboardState ||
        !p_SDL_GetKeyboardFocus || !set_touch) {
        fprintf(stderr, "input bridge: required symbol missing\n"); return NULL;
    }
    SDL_Gamepad *pad = NULL;
    SDL_JoystickID pad_id = 0;
    uint16_t previous_buttons = 0;
    bool trace_input = getenv("CARBON_TRACE_INPUT") != NULL;
    static const uint16_t buttons[] = {0x1000,0x2000,0x4000,0x8000,0x0020,0x0400,
        0x0010,0x0040,0x0080,0x0100,0x0200,0x0001,0x0002,0x0004,0x0008};
    while (atomic_load(&running)) {
        uint16_t b=0; int16_t lx=0, ly=0, rx=0, ry=0; uint8_t lt=0, rt=0;
        if (p_SDL_WasInit(SDL_INIT_GAMEPAD)) {
            // The touch pad shares guest slot 0 with the first physical pad.
            // Mirror that same pad, leaving the second physical pad on slot 1.
            int count=0; SDL_JoystickID *ids=p_SDL_GetGamepads(&count);
            SDL_JoystickID desired = ids && count > 0 ? ids[0] : 0;
            if (pad && (!p_SDL_GamepadConnected(pad) || pad_id != desired)) {
                p_SDL_CloseGamepad(pad); pad=NULL; pad_id=0;
            }
            if (!pad && desired) {
                pad=p_SDL_OpenGamepad(desired);
                if (pad) {
                    pad_id=desired;
                    fprintf(stderr, "input bridge: SDL gamepad %u assigned to player 1\n", pad_id);
                }
            }
            if (ids) p_SDL_free(ids);
            if (pad) {
                for (int i=0;i<15;i++) if (p_SDL_GetGamepadButton(pad, (SDL_GamepadButton)i)) b |= buttons[i];
                lx=p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
                ly=~p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
                rx=p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
                ry=~p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
                int l=p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                int r=p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                lt=l>0 ? l>>7 : 0; rt=r>0 ? r>>7 : 0;
            }
        } else { pad=NULL; pad_id=0; }
        int n=0; const bool *k=p_SDL_GetKeyboardState(&n);
        if (k && n>SDL_SCANCODE_UP && p_SDL_GetKeyboardFocus()) {
            if(k[SDL_SCANCODE_RETURN]) b|=0x0010;
            if(k[SDL_SCANCODE_SPACE]) b|=0x1000;
            if(k[SDL_SCANCODE_BACKSPACE]) b|=0x2000;
            if(k[SDL_SCANCODE_L]) b|=0x4000;
            if(k[SDL_SCANCODE_P]) b|=0x8000;
            if(k[SDL_SCANCODE_UP]) b|=0x0001;
            if(k[SDL_SCANCODE_DOWN]) b|=0x0002;
            if(k[SDL_SCANCODE_LEFT]) b|=0x0004;
            if(k[SDL_SCANCODE_RIGHT]) b|=0x0008;
            if(k[SDL_SCANCODE_A]) lx=-32767; else if(k[SDL_SCANCODE_D]) lx=32767;
            if(k[SDL_SCANCODE_W]) ly=32767; else if(k[SDL_SCANCODE_S]) ly=-32767;
            if(k[SDL_SCANCODE_Q]) lt=255;
            if(k[SDL_SCANCODE_E]) rt=255;
        }
        if (trace_input && b != previous_buttons) {
            fprintf(stderr, "input bridge: player 1 buttons=%04x\n", b);
            previous_buttons = b;
        }
        set_touch(b,lx,ly,rx,ry,lt,rt);
        usleep(8000);
    }
    if (pad && p_SDL_WasInit(SDL_INIT_GAMEPAD)) p_SDL_CloseGamepad(pad);
    return NULL;
}
int main(int argc, char **argv) {
    dlopen("/system/lib64/liblzma.so", RTLD_NOW | RTLD_GLOBAL);
    sdl=dlopen("libSDL3.so", RTLD_NOW | RTLD_GLOBAL);
    if (!sdl) { fprintf(stderr, "SDL: %s\n", dlerror()); return 1; }
    void (*ready)(void)=dlsym(sdl, "SDL_SetMainReady"); if (ready) ready();
    void *app=dlopen("libmain.so", RTLD_NOW | RTLD_GLOBAL);
    if (!app) { fprintf(stderr, "Carbon: %s\n", dlerror()); return 2; }
    int (*entry)(int,char **)=dlsym(app,"SDL_main");
    if (!entry) { fprintf(stderr,"SDL_main: %s\n",dlerror()); return 3; }
    set_touch=dlsym(RTLD_DEFAULT,"rex_sdl_set_touch_gamepad_state");
    const char *bridge_path = getenv("CARBON_XI2_BRIDGE");
    if (!bridge_path || !*bridge_path) { fprintf(stderr,"missing CARBON_XI2_BRIDGE\n"); return 4; }
    void *bridge = dlopen(bridge_path, RTLD_NOW | RTLD_LOCAL);
    if (!bridge) { fprintf(stderr,"XI2 bridge: %s\n",dlerror()); return 4; }
    int (*bridge_start)(void) = dlsym(bridge,"carbon_xi2_start");
    void (*bridge_stop)(void) = dlsym(bridge,"carbon_xi2_stop");
    if (!bridge_start || !bridge_stop || bridge_start() != 0) {
        fprintf(stderr,"XI2 bridge failed to start\n"); return 4;
    }
    pthread_t thread; bool started=pthread_create(&thread,NULL,input_thread,NULL)==0;
    int result=entry(argc,argv);
    atomic_store(&running,false); if(started) pthread_join(thread,NULL);
    bridge_stop();
    fprintf(stderr,"SDL_main exited: %d\n",result); return result;
}
