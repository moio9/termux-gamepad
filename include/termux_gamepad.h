#ifndef TERMUX_GAMEPAD_H
#define TERMUX_GAMEPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERMUX_GAMEPAD_ABI_VERSION 1U
#define TERMUX_GAMEPAD_AXIS_COUNT 6
#define TERMUX_GAMEPAD_BUTTON_COUNT 12

#define TERMUX_GAMEPAD_CAP_RUMBLE         (1U << 0)
#define TERMUX_GAMEPAD_CAP_TRIGGER_RUMBLE (1U << 1)

#define TERMUX_GAMEPAD_INPUT_NONE    0
#define TERMUX_GAMEPAD_INPUT_XINPUT  1
#define TERMUX_GAMEPAD_INPUT_DINPUT  2
#define TERMUX_GAMEPAD_INPUT_XDINPUT 3

#if defined(_WIN32)
#define TERMUX_GAMEPAD_API __declspec(dllexport)
#else
#define TERMUX_GAMEPAD_API __attribute__((visibility("default")))
#endif

typedef struct termux_gamepad termux_gamepad;

typedef enum termux_gamepad_backend {
    TERMUX_GAMEPAD_BACKEND_AUTO = 0,
    TERMUX_GAMEPAD_BACKEND_UDEV = 1,
    TERMUX_GAMEPAD_BACKEND_SDL = 2,
    TERMUX_GAMEPAD_BACKEND_OFF = 3
} termux_gamepad_backend;

struct termux_gamepad_descriptor {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t present;
    uint8_t capabilities;
    uint8_t input_mode;
    uint8_t reserved0;
    uint32_t vendor_id;
    uint32_t product_id;
    int32_t device_id;
    char name[64];
};

struct termux_gamepad_state {
    int16_t axes[TERMUX_GAMEPAD_AXIS_COUNT];
    uint16_t buttons;
    int8_t hat_x;
    int8_t hat_y;
    uint32_t packet_number;
};

TERMUX_GAMEPAD_API uint32_t termux_gamepad_get_abi_version(void);
TERMUX_GAMEPAD_API termux_gamepad_backend termux_gamepad_get_backend(void);

/* A NULL/empty socket path uses TERMUX_GAMEPAD_EVDEV_SOCKET, then the
 * standard Termux bridge socket.  The handle remains valid while the bridge
 * is unavailable and reconnects from termux_gamepad_refresh(). */
TERMUX_GAMEPAD_API termux_gamepad *termux_gamepad_create(const char *socket_path);
TERMUX_GAMEPAD_API void termux_gamepad_destroy(termux_gamepad *gamepad);

/* Returns 1 when presence/identity/profile changed, 0 otherwise, or a
 * negative errno value. */
TERMUX_GAMEPAD_API int termux_gamepad_refresh(termux_gamepad *gamepad);

/* Drains pending controller events and updates the state snapshot. Returns
 * the number of SYN_REPORT packets processed, or a negative errno value. */
TERMUX_GAMEPAD_API int termux_gamepad_update(termux_gamepad *gamepad);

TERMUX_GAMEPAD_API int termux_gamepad_get_descriptor(
    const termux_gamepad *gamepad, struct termux_gamepad_descriptor *descriptor);
TERMUX_GAMEPAD_API int termux_gamepad_get_state(
    const termux_gamepad *gamepad, struct termux_gamepad_state *state);
TERMUX_GAMEPAD_API int termux_gamepad_get_fd(const termux_gamepad *gamepad);

TERMUX_GAMEPAD_API int termux_gamepad_rumble(
    termux_gamepad *gamepad, uint16_t low_frequency,
    uint16_t high_frequency, uint32_t duration_ms);
TERMUX_GAMEPAD_API int termux_gamepad_stop_rumble(termux_gamepad *gamepad);

#ifdef __cplusplus
}
#endif

#endif
