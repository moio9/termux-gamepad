#include <SDL2/SDL.h>
#include <stdio.h>

struct effect_case {
    Uint16 type;
    const char *name;
};

static int run_effect(SDL_Haptic *haptic, const struct effect_case *test)
{
    SDL_HapticEffect effect;
    int id;

    SDL_memset(&effect, 0, sizeof(effect));
    effect.type = test->type;
    switch (test->type) {
    case SDL_HAPTIC_CONSTANT:
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;
        effect.constant.length = 120;
        effect.constant.level = 14000;
        break;
    case SDL_HAPTIC_SINE:
    case SDL_HAPTIC_TRIANGLE:
    case SDL_HAPTIC_SAWTOOTHUP:
    case SDL_HAPTIC_SAWTOOTHDOWN:
        effect.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.periodic.direction.dir[0] = 1;
        effect.periodic.length = 120;
        effect.periodic.period = 60;
        effect.periodic.magnitude = 14000;
        break;
    case SDL_HAPTIC_RAMP:
        effect.ramp.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.ramp.direction.dir[0] = 1;
        effect.ramp.length = 120;
        effect.ramp.start = 4000;
        effect.ramp.end = 16000;
        break;
    case SDL_HAPTIC_SPRING:
    case SDL_HAPTIC_DAMPER:
    case SDL_HAPTIC_INERTIA:
    case SDL_HAPTIC_FRICTION:
        effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.length = 120;
        effect.condition.right_sat[0] = 16000;
        effect.condition.left_sat[0] = 16000;
        effect.condition.right_sat[1] = 12000;
        effect.condition.left_sat[1] = 12000;
        effect.condition.right_coeff[0] = 8000;
        effect.condition.left_coeff[0] = -8000;
        effect.condition.right_coeff[1] = 6000;
        effect.condition.left_coeff[1] = -6000;
        break;
    default:
        return -1;
    }

    id = SDL_HapticNewEffect(haptic, &effect);
    if (id < 0) {
        fprintf(stderr, "%s create: %s\n", test->name, SDL_GetError());
        return -1;
    }
    if (SDL_HapticRunEffect(haptic, id, 1) < 0) {
        fprintf(stderr, "%s run: %s\n", test->name, SDL_GetError());
        SDL_HapticDestroyEffect(haptic, id);
        return -1;
    }
    printf("effect=%s id=%d PASS\n", test->name, id);
    SDL_Delay(150);
    SDL_HapticStopEffect(haptic, id);
    SDL_HapticDestroyEffect(haptic, id);
    return 0;
}

int main(void)
{
    static const struct effect_case tests[] = {
        {SDL_HAPTIC_CONSTANT, "constant"},
        {SDL_HAPTIC_SINE, "sine"},
        {SDL_HAPTIC_TRIANGLE, "triangle"},
        {SDL_HAPTIC_SAWTOOTHUP, "sawtooth-up"},
        {SDL_HAPTIC_SAWTOOTHDOWN, "sawtooth-down"},
        {SDL_HAPTIC_RAMP, "ramp"},
        {SDL_HAPTIC_SPRING, "spring"},
        {SDL_HAPTIC_DAMPER, "damper"},
        {SDL_HAPTIC_INERTIA, "inertia"},
        {SDL_HAPTIC_FRICTION, "friction"},
    };
    SDL_Joystick *joystick;
    SDL_Haptic *haptic;
    unsigned int i;
    int failed = 0;

    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    joystick = SDL_JoystickOpen(0);
    if (!joystick || !(haptic = SDL_HapticOpenFromJoystick(joystick))) {
        fprintf(stderr, "open: %s\n", SDL_GetError());
        return 2;
    }
    printf("query=0x%08x\n", SDL_HapticQuery(haptic));
    for (i = 0; i < SDL_arraysize(tests); ++i)
        if (run_effect(haptic, &tests[i]) < 0) failed++;
    SDL_HapticClose(haptic);
    SDL_JoystickClose(joystick);
    SDL_Quit();
    printf("RESULT: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 3 : 0;
}
