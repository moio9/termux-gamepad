#include "termux_gamepad.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct termux_gamepad_descriptor descriptor;
    struct termux_gamepad_state state;
    termux_gamepad *gamepad = termux_gamepad_create(NULL);
    int i;

    if (!gamepad) {
        perror("termux_gamepad_create");
        return 1;
    }
    (void)termux_gamepad_refresh(gamepad);
    (void)termux_gamepad_update(gamepad);
    termux_gamepad_get_descriptor(gamepad, &descriptor);
    termux_gamepad_get_state(gamepad, &state);
    printf("abi=%u present=%u mode=%u capabilities=0x%x\n",
           descriptor.abi_version, descriptor.present,
           descriptor.input_mode, descriptor.capabilities);
    printf("name=%s vid=%04x pid=%04x fd=%d packet=%u\n",
           descriptor.name, descriptor.vendor_id, descriptor.product_id,
           termux_gamepad_get_fd(gamepad), state.packet_number);
    if (argc > 1 && strcmp(argv[1], "--rumble") == 0) {
        printf("rumble=500ms\n");
        termux_gamepad_rumble(gamepad, 24576, 65535, 500);
        usleep(500000);
        termux_gamepad_stop_rumble(gamepad);
    }
    if (argc > 1 && strcmp(argv[1], "--monitor") == 0) {
        for (i = 0; i < 300; ++i) {
            int changed = termux_gamepad_refresh(gamepad);
            (void)termux_gamepad_update(gamepad);
            if (changed > 0) {
                termux_gamepad_get_descriptor(gamepad, &descriptor);
                printf("hotplug present=%u mode=%u name=%s\n",
                       descriptor.present, descriptor.input_mode,
                       descriptor.name);
                fflush(stdout);
            }
            usleep(100000);
        }
    }
    termux_gamepad_destroy(gamepad);
    return descriptor.present ? 0 : 2;
}
