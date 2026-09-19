#include <SDL3/SDL.h>
#include <stdio.h>

int main(void) {
    int count = 0;
    SDL_JoystickID *ids;
    SDL_Gamepad *pad;
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    ids = SDL_GetGamepads(&count);
    printf("gamepads=%d hint=%s\n", count,
           SDL_GetHint(SDL_HINT_JOYSTICK_DEVICE) ?: "(unset)");
    if (!ids || count < 1) {
        fprintf(stderr, "SDL_GetGamepads: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    printf("id=%u name=%s path=%s\n", ids[0],
           SDL_GetGamepadNameForID(ids[0]), SDL_GetGamepadPathForID(ids[0]));
    pad = SDL_OpenGamepad(ids[0]);
    if (!pad) {
        fprintf(stderr, "SDL_OpenGamepad: %s\n", SDL_GetError());
        return 3;
    }
    printf("rumble=%s\n", SDL_RumbleGamepad(pad, 0xffff, 0x8000, 600) ? "yes" : SDL_GetError());
    SDL_Delay(700);
    SDL_CloseGamepad(pad);
    SDL_free(ids);
    SDL_Quit();
    return 0;
}
