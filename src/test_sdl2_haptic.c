#include <SDL2/SDL.h>
#include <stdio.h>

int main(void) {
    SDL_Joystick *joystick;
    SDL_Haptic *haptic;
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    printf("joysticks=%d haptics=%d\n", SDL_NumJoysticks(), SDL_NumHaptics());
    if (SDL_NumJoysticks() < 1) return 2;
    joystick = SDL_JoystickOpen(0);
    if (!joystick) {
        fprintf(stderr, "SDL_JoystickOpen: %s\n", SDL_GetError());
        return 3;
    }
    printf("name=%s vendor=%04x product=%04x game_controller=%d is_haptic=%d\n",
           SDL_JoystickName(joystick), SDL_JoystickGetVendor(joystick),
           SDL_JoystickGetProduct(joystick), SDL_IsGameController(0),
           SDL_JoystickIsHaptic(joystick));
    haptic = SDL_HapticOpenFromJoystick(joystick);
    if (!haptic) {
        fprintf(stderr, "SDL_HapticOpenFromJoystick: %s\n", SDL_GetError());
        return 4;
    }
    printf("query=0x%08x axes=%d effects=%d\n", SDL_HapticQuery(haptic),
           SDL_HapticNumAxes(haptic), SDL_HapticNumEffects(haptic));
    if (SDL_HapticRumbleInit(haptic) < 0 ||
        SDL_HapticRumblePlay(haptic, 1.0f, 600) < 0) {
        fprintf(stderr, "SDL haptic rumble: %s\n", SDL_GetError());
        return 5;
    }
    SDL_Delay(700);
    SDL_HapticClose(haptic);
    SDL_JoystickClose(joystick);
    SDL_Quit();
    return 0;
}
