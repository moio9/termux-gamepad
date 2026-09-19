#include "termux_gamepad.h"
#include "tg_common.h"
#include "tg_evdev_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct termux_gamepad {
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    int monitor_fd;
    int event_fd;
    struct termux_gamepad_descriptor descriptor;
    struct termux_gamepad_state state;
    unsigned char monitor_buffer[sizeof(struct tg_hotplug_message)];
    size_t monitor_used;
    unsigned char event_buffer[sizeof(struct input_event)];
    size_t event_used;
};

static void reset_state(struct termux_gamepad_state *state)
{
    memset(state, 0, sizeof(*state));
    state->axes[4] = INT16_MIN;
    state->axes[5] = INT16_MIN;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -errno;
    flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    return 0;
}

static int connect_socket(const char *path)
{
    struct sockaddr_un address;
    int fd;

    if (!path || strlen(path) >= sizeof(address.sun_path))
        return -ENAMETOOLONG;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    return fd;
}

static int send_all(int fd, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t sent = 0;

    while (sent < size) {
        ssize_t result = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pollfd = { fd, POLLOUT, 0 };
            if (poll(&pollfd, 1, 100) > 0)
                continue;
        }
        return -(result < 0 ? errno : EPIPE);
    }
    return 0;
}

static int receive_all(int fd, void *data, size_t size)
{
    unsigned char *bytes = (unsigned char *)data;
    size_t received = 0;

    while (received < size) {
        ssize_t result = recv(fd, bytes + received, size - received, 0);
        if (result > 0) {
            received += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return -(result < 0 ? errno : ECONNRESET);
    }
    return 0;
}

static int send_command(int fd, uint16_t command, uint16_t low,
                        uint16_t high, uint32_t duration_ms)
{
    struct tg_control_message message;

    memset(&message, 0, sizeof(message));
    message.magic = TG_CONTROL_MAGIC;
    message.version = TG_CONTROL_VERSION;
    message.command = command;
    message.low_frequency = low;
    message.high_frequency = high;
    message.duration_ms = duration_ms;
    return send_all(fd, &message, sizeof(message));
}

static void copy_descriptor(struct termux_gamepad_descriptor *output,
                            const struct tg_device_descriptor *input)
{
    memset(output, 0, sizeof(*output));
    output->abi_version = TERMUX_GAMEPAD_ABI_VERSION;
    output->struct_size = sizeof(*output);
    output->present = input->present;
    output->capabilities =
        (input->capabilities & TG_DEVICE_CAP_RUMBLE) ?
        TERMUX_GAMEPAD_CAP_RUMBLE | TERMUX_GAMEPAD_CAP_TRIGGER_RUMBLE : 0;
    output->input_mode = input->input_mode;
    output->vendor_id = input->vendor_id;
    output->product_id = input->product_id;
    output->device_id = input->device_id;
    strncpy(output->name, input->name, sizeof(output->name) - 1);
}

static int descriptor_equal(const struct termux_gamepad_descriptor *a,
                            const struct termux_gamepad_descriptor *b)
{
    return a->present == b->present &&
           a->capabilities == b->capabilities &&
           a->input_mode == b->input_mode &&
           a->vendor_id == b->vendor_id &&
           a->product_id == b->product_id &&
           a->device_id == b->device_id &&
           strncmp(a->name, b->name, sizeof(a->name)) == 0;
}

static void close_event_stream(termux_gamepad *gamepad)
{
    if (gamepad->event_fd >= 0)
        close(gamepad->event_fd);
    gamepad->event_fd = -1;
    gamepad->event_used = 0;
    reset_state(&gamepad->state);
}

static int open_event_stream(termux_gamepad *gamepad)
{
    struct tg_device_descriptor descriptor;
    int fd = connect_socket(gamepad->socket_path);
    int result;

    if (fd < 0)
        return fd;
    result = send_command(fd, TG_CONTROL_QUERY_DESCRIPTOR, 0, 0, 0);
    if (result == 0)
        result = receive_all(fd, &descriptor, sizeof(descriptor));
    if (result < 0 || descriptor.magic != TG_DEVICE_MAGIC ||
        descriptor.version != TG_CONTROL_VERSION || !descriptor.present) {
        close(fd);
        return result < 0 ? result : -ENODEV;
    }
    result = set_nonblocking(fd);
    if (result < 0) {
        close(fd);
        return result;
    }
    close_event_stream(gamepad);
    gamepad->event_fd = fd;
    return 0;
}

static int apply_hotplug(termux_gamepad *gamepad,
                         const struct tg_hotplug_message *message)
{
    struct termux_gamepad_descriptor descriptor;
    int changed;

    if (message->magic != TG_HOTPLUG_MAGIC ||
        message->version != TG_CONTROL_VERSION)
        return -EPROTO;
    copy_descriptor(&descriptor, &message->descriptor);
    changed = !descriptor_equal(&gamepad->descriptor, &descriptor);
    if (!changed)
        return 0;
    gamepad->descriptor = descriptor;
    close_event_stream(gamepad);
    if (descriptor.present)
        (void)open_event_stream(gamepad);
    return 1;
}

static int open_monitor(termux_gamepad *gamepad)
{
    struct tg_hotplug_message message;
    int fd = connect_socket(gamepad->socket_path);
    int result;

    if (fd < 0)
        return fd;
    result = send_command(fd, TG_CONTROL_MONITOR, 0, 0, 0);
    if (result == 0)
        result = receive_all(fd, &message, sizeof(message));
    if (result < 0) {
        close(fd);
        return result;
    }
    result = set_nonblocking(fd);
    if (result < 0) {
        close(fd);
        return result;
    }
    gamepad->monitor_fd = fd;
    gamepad->monitor_used = 0;
    return apply_hotplug(gamepad, &message);
}

static int button_index(unsigned short code)
{
    switch (code) {
    case BTN_SOUTH: return 0;
    case BTN_EAST: return 1;
    case BTN_NORTH: return 2;
    case BTN_WEST: return 3;
    case BTN_TL: return 4;
    case BTN_TR: return 5;
    case BTN_SELECT: return 6;
    case BTN_START: return 7;
    case BTN_THUMBL: return 8;
    case BTN_THUMBR: return 9;
    case BTN_MODE: return 10;
    case BTN_C: return 11;
    default: return -1;
    }
}

static void process_event(termux_gamepad *gamepad,
                          const struct input_event *event, int *reports)
{
    int index;

    if (event->type == EV_ABS) {
        switch (event->code) {
        case ABS_X: gamepad->state.axes[0] = (int16_t)event->value; break;
        case ABS_Y: gamepad->state.axes[1] = (int16_t)event->value; break;
        case ABS_RX: gamepad->state.axes[2] = (int16_t)event->value; break;
        case ABS_RY: gamepad->state.axes[3] = (int16_t)event->value; break;
        case ABS_Z:
            gamepad->state.axes[4] = (int16_t)(event->value * 257 - 32768);
            break;
        case ABS_RZ:
            gamepad->state.axes[5] = (int16_t)(event->value * 257 - 32768);
            break;
        case ABS_HAT0X: gamepad->state.hat_x = (int8_t)event->value; break;
        case ABS_HAT0Y: gamepad->state.hat_y = (int8_t)event->value; break;
        default: break;
        }
    } else if (event->type == EV_KEY) {
        index = button_index(event->code);
        if (index >= 0) {
            if (event->value)
                gamepad->state.buttons |= (uint16_t)(1U << index);
            else
                gamepad->state.buttons &= (uint16_t)~(1U << index);
        }
    } else if (event->type == EV_SYN && event->code == SYN_REPORT) {
        ++gamepad->state.packet_number;
        ++*reports;
    }
}

uint32_t termux_gamepad_get_abi_version(void)
{
    return TERMUX_GAMEPAD_ABI_VERSION;
}

termux_gamepad_backend termux_gamepad_get_backend(void)
{
    const char *value = getenv("TERMUX_GAMEPAD_BACKEND");

    if (!value || !*value || strcmp(value, "auto") == 0)
        return TERMUX_GAMEPAD_BACKEND_AUTO;
    if (strcmp(value, "udev") == 0)
        return TERMUX_GAMEPAD_BACKEND_UDEV;
    if (strcmp(value, "sdl") == 0)
        return TERMUX_GAMEPAD_BACKEND_SDL;
    if (strcmp(value, "off") == 0)
        return TERMUX_GAMEPAD_BACKEND_OFF;
    return TERMUX_GAMEPAD_BACKEND_AUTO;
}

termux_gamepad *termux_gamepad_create(const char *socket_path)
{
    const char *environment;
    termux_gamepad *gamepad = (termux_gamepad *)calloc(1, sizeof(*gamepad));

    if (!gamepad)
        return NULL;
    environment = getenv("TERMUX_GAMEPAD_EVDEV_SOCKET");
    if (!socket_path || !*socket_path)
        socket_path = (environment && *environment) ? environment : TG_DEFAULT_SOCKET;
    if (strlen(socket_path) >= sizeof(gamepad->socket_path)) {
        free(gamepad);
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(gamepad->socket_path, socket_path);
    gamepad->monitor_fd = -1;
    gamepad->event_fd = -1;
    gamepad->descriptor.abi_version = TERMUX_GAMEPAD_ABI_VERSION;
    gamepad->descriptor.struct_size = sizeof(gamepad->descriptor);
    gamepad->descriptor.device_id = -1;
    reset_state(&gamepad->state);
    (void)open_monitor(gamepad);
    return gamepad;
}

void termux_gamepad_destroy(termux_gamepad *gamepad)
{
    if (!gamepad)
        return;
    if (gamepad->event_fd >= 0) {
        (void)send_command(gamepad->event_fd, TG_CONTROL_STOP_RUMBLE, 0, 0, 0);
        close(gamepad->event_fd);
    }
    if (gamepad->monitor_fd >= 0)
        close(gamepad->monitor_fd);
    free(gamepad);
}

int termux_gamepad_refresh(termux_gamepad *gamepad)
{
    int changed = 0;

    if (!gamepad)
        return -EINVAL;
    if (gamepad->monitor_fd < 0)
        return open_monitor(gamepad);
    for (;;) {
        ssize_t result = recv(gamepad->monitor_fd,
            gamepad->monitor_buffer + gamepad->monitor_used,
            sizeof(gamepad->monitor_buffer) - gamepad->monitor_used,
            MSG_DONTWAIT);
        if (result > 0) {
            gamepad->monitor_used += (size_t)result;
            if (gamepad->monitor_used == sizeof(struct tg_hotplug_message)) {
                int applied = apply_hotplug(gamepad,
                    (const struct tg_hotplug_message *)gamepad->monitor_buffer);
                gamepad->monitor_used = 0;
                if (applied < 0)
                    return applied;
                if (applied > 0)
                    changed = 1;
            }
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return changed;
        close(gamepad->monitor_fd);
        gamepad->monitor_fd = -1;
        gamepad->monitor_used = 0;
        if (gamepad->descriptor.present) {
            gamepad->descriptor.present = 0;
            gamepad->descriptor.device_id = -1;
            close_event_stream(gamepad);
            return 1;
        }
        return result < 0 ? -errno : changed;
    }
}

int termux_gamepad_update(termux_gamepad *gamepad)
{
    int reports = 0;
    int refreshed;

    if (!gamepad)
        return -EINVAL;
    refreshed = termux_gamepad_refresh(gamepad);
    if (gamepad->event_fd < 0 && gamepad->descriptor.present)
        (void)open_event_stream(gamepad);
    if (refreshed < 0 && gamepad->event_fd < 0)
        return refreshed;
    if (gamepad->event_fd < 0)
        return 0;
    for (;;) {
        ssize_t result = recv(gamepad->event_fd,
            gamepad->event_buffer + gamepad->event_used,
            sizeof(gamepad->event_buffer) - gamepad->event_used,
            MSG_DONTWAIT);
        if (result > 0) {
            gamepad->event_used += (size_t)result;
            if (gamepad->event_used == sizeof(struct input_event)) {
                process_event(gamepad,
                    (const struct input_event *)gamepad->event_buffer, &reports);
                gamepad->event_used = 0;
            }
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return reports;
        close_event_stream(gamepad);
        return result < 0 ? -errno : reports;
    }
}

int termux_gamepad_get_descriptor(
    const termux_gamepad *gamepad, struct termux_gamepad_descriptor *descriptor)
{
    if (!gamepad || !descriptor)
        return -EINVAL;
    *descriptor = gamepad->descriptor;
    return 0;
}

int termux_gamepad_get_state(
    const termux_gamepad *gamepad, struct termux_gamepad_state *state)
{
    if (!gamepad || !state)
        return -EINVAL;
    *state = gamepad->state;
    return 0;
}

int termux_gamepad_get_fd(const termux_gamepad *gamepad)
{
    return gamepad ? gamepad->event_fd : -1;
}

int termux_gamepad_rumble(termux_gamepad *gamepad, uint16_t low,
                          uint16_t high, uint32_t duration_ms)
{
    if (!gamepad || gamepad->event_fd < 0)
        return -ENODEV;
    if (!low && !high)
        return termux_gamepad_stop_rumble(gamepad);
    return send_command(gamepad->event_fd, TG_CONTROL_RUMBLE,
                        low, high, duration_ms);
}

int termux_gamepad_stop_rumble(termux_gamepad *gamepad)
{
    if (!gamepad || gamepad->event_fd < 0)
        return -ENODEV;
    return send_command(gamepad->event_fd, TG_CONTROL_STOP_RUMBLE, 0, 0, 0);
}
