#define _GNU_SOURCE

#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/XInput2.h>

#include "lorie_controller_proto.h"
#include "tg_evdev_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/data/data/com.termux/files/usr/tmp/termux-gamepad-evdev.sock"
#define MAX_CLIENTS 16
#define CLIENT_BUFFER_SIZE 16384

enum client_kind {
    CLIENT_UNINITIALIZED,
    CLIENT_EVENTS,
    CLIENT_MONITOR
};

struct client {
    int fd;
    size_t used;
    unsigned char output[CLIENT_BUFFER_SIZE];
    size_t control_used;
    unsigned char control[256];
    unsigned short rumble_low;
    unsigned short rumble_high;
    uint64_t rumble_expires_ms;
    enum client_kind kind;
};

struct gamepad_state {
    int axes[6];
    unsigned int buttons;
    int hat_x;
    int hat_y;
};

static volatile sig_atomic_t running = 1;
static struct client clients[MAX_CLIENTS];
static struct gamepad_state state;
static Display *x_display;
static int lorie_controller_opcode = -1;
static int lorie_device_id = -1;
static unsigned short lorie_capabilities;
static unsigned short active_rumble_low;
static unsigned short active_rumble_high;
static XErrorHandler previous_x_error_handler;
static struct tg_device_descriptor device_descriptor = {
    .magic = TG_DEVICE_MAGIC,
    .version = TG_CONTROL_VERSION,
    .device_id = -1,
};

static void update_rumble(int force);

static int handle_x_error(Display *display, XErrorEvent *event)
{
    if (event->request_code == lorie_controller_opcode) {
        char message[128] = {0};
        XGetErrorText(display, event->error_code, message, sizeof(message));
        fprintf(stderr,
                "Ignoring stale LORIE-CONTROLLER request: %s (minor=%u resource=%lu)\n",
                message, (unsigned int)event->minor_code,
                (unsigned long)event->resourceid);
        return 0;
    }
    return previous_x_error_handler ?
           previous_x_error_handler(display, event) : 0;
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
    return 0;
}

static void close_client(struct client *client)
{
    const int had_rumble = client->rumble_low || client->rumble_high;

    if (client->fd >= 0) {
        close(client->fd);
    }
    client->fd = -1;
    client->used = 0;
    client->control_used = 0;
    client->rumble_low = 0;
    client->rumble_high = 0;
    client->rumble_expires_ms = 0;
    client->kind = CLIENT_UNINITIALIZED;
    if (had_rumble && x_display) {
        update_rumble(1);
    }
}

static int flush_client(struct client *client)
{
    while (client->used > 0) {
        ssize_t sent = send(client->fd, client->output, client->used,
                            MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            client->used -= (size_t)sent;
            if (client->used > 0) {
                memmove(client->output, client->output + sent,
                        client->used);
            }
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return -1;
    }
    return 0;
}

static int queue_bytes(struct client *client, const void *data, size_t bytes)
{
    if (bytes > sizeof(client->output) - client->used) {
        /* A stalled consumer must not add latency for the other clients. */
        return -1;
    }
    memcpy(client->output + client->used, data, bytes);
    client->used += bytes;
    return flush_client(client);
}

static int queue_events(struct client *client, const struct input_event *events,
                        size_t count)
{
    return queue_bytes(client, events, count * sizeof(*events));
}

static void broadcast_events(const struct input_event *events, size_t count)
{
    int i;

    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0 && clients[i].kind == CLIENT_EVENTS &&
            queue_events(&clients[i], events, count) < 0) {
            close_client(&clients[i]);
        }
    }
}

static void timestamp_event(struct input_event *event)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    event->time.tv_sec = timestamp.tv_sec;
    event->time.tv_usec = timestamp.tv_nsec / 1000;
}

static uint64_t monotonic_ms(void)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint64_t)timestamp.tv_sec * 1000U +
           (uint64_t)timestamp.tv_nsec / 1000000U;
}

static int query_lorie_version(void)
{
    Display *dpy = x_display;
    xLorieControllerQueryVersionReq *request;
    xLorieControllerQueryVersionReply reply;
    Status status;

    LockDisplay(dpy);
    request = (xLorieControllerQueryVersionReq *)_XGetRequest(
        dpy, (CARD8)lorie_controller_opcode, sizeof(*request));
    request->lorieControllerReqType = X_LorieControllerQueryVersion;
    request->majorVersion = LORIE_CONTROLLER_MAJOR_VERSION;
    request->minorVersion = LORIE_CONTROLLER_MINOR_VERSION;
    status = _XReply(dpy, (xReply *)&reply, 0, xFalse);
    UnlockDisplay(dpy);
    SyncHandle();
    return status && reply.majorVersion == LORIE_CONTROLLER_MAJOR_VERSION;
}

static int query_lorie_capabilities(int device_id,
                                    xLorieControllerQueryCapabilitiesReply *result)
{
    Display *dpy = x_display;
    xLorieControllerQueryCapabilitiesReq *request;
    xLorieControllerQueryCapabilitiesReply reply;
    Status status;

    memset(result, 0, sizeof(*result));
    LockDisplay(dpy);
    request = (xLorieControllerQueryCapabilitiesReq *)_XGetRequest(
        dpy, (CARD8)lorie_controller_opcode, sizeof(*request));
    request->lorieControllerReqType = X_LorieControllerQueryCapabilities;
    request->deviceId = (CARD16)device_id;
    request->pad0 = 0;
    status = _XReply(dpy, (xReply *)&reply, 0, xFalse);
    UnlockDisplay(dpy);
    SyncHandle();
    if (!status || !reply.present || reply.deviceId != device_id) {
        return 0;
    }
    *result = reply;
    return 1;
}

static void broadcast_hotplug(unsigned char action)
{
    struct tg_hotplug_message message;
    int i;

    memset(&message, 0, sizeof(message));
    message.magic = TG_HOTPLUG_MAGIC;
    message.version = TG_CONTROL_VERSION;
    message.action = action;
    message.descriptor = device_descriptor;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0 && clients[i].kind == CLIENT_MONITOR &&
            queue_bytes(&clients[i], &message, sizeof(message)) < 0)
            close_client(&clients[i]);
    }
}

static int send_lorie_rumble(unsigned short low, unsigned short high,
                             uint32_t duration_ms)
{
    Display *dpy = x_display;

    if (!x_display || lorie_controller_opcode < 0 || lorie_device_id < 0 ||
        !(lorie_capabilities & LORIE_CONTROLLER_CAP_RUMBLE)) {
        return -1;
    }
    if (!low && !high) {
        xLorieControllerStopRumbleReq *request;
        LockDisplay(dpy);
        request = (xLorieControllerStopRumbleReq *)_XGetRequest(
            dpy, (CARD8)lorie_controller_opcode, sizeof(*request));
        request->lorieControllerReqType = X_LorieControllerStopRumble;
        request->deviceId = (CARD16)lorie_device_id;
        request->pad0 = 0;
        UnlockDisplay(dpy);
        SyncHandle();
        XFlush(dpy);
        return 0;
    } else {
        xLorieControllerRumbleReq *request;
        LockDisplay(dpy);
        request = (xLorieControllerRumbleReq *)_XGetRequest(
            dpy, (CARD8)lorie_controller_opcode, sizeof(*request));
        request->lorieControllerReqType = X_LorieControllerRumble;
        request->deviceId = (CARD16)lorie_device_id;
        request->effect = LORIE_CONTROLLER_EFFECT_MAIN;
        request->pad0 = 0;
        request->lowFrequency = low;
        request->highFrequency = high;
        request->durationMs = duration_ms;
        UnlockDisplay(dpy);
        SyncHandle();
        XFlush(dpy);
        return 0;
    }
}

static void update_rumble(int force)
{
    const uint64_t now = monotonic_ms();
    unsigned short low = 0;
    unsigned short high = 0;
    uint64_t longest_remaining = 0;
    int i;

    for (i = 0; i < MAX_CLIENTS; ++i) {
        uint64_t remaining;
        if (clients[i].fd < 0 ||
            (!clients[i].rumble_low && !clients[i].rumble_high)) {
            continue;
        }
        if (clients[i].rumble_expires_ms <= now) {
            clients[i].rumble_low = 0;
            clients[i].rumble_high = 0;
            clients[i].rumble_expires_ms = 0;
            continue;
        }
        remaining = clients[i].rumble_expires_ms - now;
        if (clients[i].rumble_low > low) {
            low = clients[i].rumble_low;
        }
        if (clients[i].rumble_high > high) {
            high = clients[i].rumble_high;
        }
        if (remaining > longest_remaining) {
            longest_remaining = remaining;
        }
    }
    if (!low && !high) {
        if (active_rumble_low || active_rumble_high) {
            (void)send_lorie_rumble(0, 0, 0);
        }
    } else if (force || low != active_rumble_low ||
               high != active_rumble_high) {
        uint32_t duration = longest_remaining > UINT32_MAX ? UINT32_MAX :
                            (uint32_t)longest_remaining;
        if (duration == 0) {
            duration = 1;
        }
        (void)send_lorie_rumble(low, high, duration);
    }
    active_rumble_low = low;
    active_rumble_high = high;
}

static void append_event(struct input_event *events, size_t *count,
                         unsigned short type, unsigned short code, int value)
{
    struct input_event *event = &events[(*count)++];

    memset(event, 0, sizeof(*event));
    timestamp_event(event);
    event->type = type;
    event->code = code;
    event->value = value;
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int convert_axis(int axis, double value)
{
    if (axis < 4) {
        return clamp_int((int)value, -32768, 32767);
    }
    return (clamp_int((int)value, 0, 32767) * 255 + 16383) / 32767;
}

static unsigned short axis_code(int axis)
{
    static const unsigned short codes[6] = {
        ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ
    };
    return codes[axis];
}

static unsigned short button_code(unsigned int detail)
{
    static const unsigned short codes[17] = {
        0,
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_SELECT, BTN_START,
        BTN_THUMBL, BTN_THUMBR,
        0, 0, 0, 0,
        BTN_MODE, BTN_C
    };

    return detail < sizeof(codes) / sizeof(codes[0]) ? codes[detail] : 0;
}

static int hat_value(unsigned int negative_detail, unsigned int positive_detail)
{
    const int negative = (state.buttons & (1U << (negative_detail - 1))) != 0;
    const int positive = (state.buttons & (1U << (positive_detail - 1))) != 0;
    return positive - negative;
}

static void send_snapshot(struct client *client)
{
    static const unsigned short buttons[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_SELECT, BTN_START,
        BTN_THUMBL, BTN_THUMBR, BTN_MODE, BTN_C
    };
    static const unsigned int details[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16
    };
    struct input_event events[24];
    size_t count = 0;
    int i;

    for (i = 0; i < 6; ++i) {
        append_event(events, &count, EV_ABS, axis_code(i), state.axes[i]);
    }
    append_event(events, &count, EV_ABS, ABS_HAT0X, state.hat_x);
    append_event(events, &count, EV_ABS, ABS_HAT0Y, state.hat_y);
    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); ++i) {
        append_event(events, &count, EV_KEY, buttons[i],
                     (state.buttons & (1U << (details[i] - 1))) != 0);
    }
    append_event(events, &count, EV_SYN, SYN_REPORT, 0);
    if (queue_events(client, events, count) < 0) {
        close_client(client);
    }
}

static void accept_clients(int server_fd)
{
    for (;;) {
        int fd = accept(server_fd, NULL, NULL);
        int slot;

        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept");
            }
            return;
        }
        if (set_nonblocking(fd) < 0) {
            close(fd);
            continue;
        }
        for (slot = 0; slot < MAX_CLIENTS; ++slot) {
            if (clients[slot].fd < 0) {
                clients[slot].fd = fd;
                clients[slot].used = 0;
                clients[slot].control_used = 0;
                clients[slot].rumble_low = 0;
                clients[slot].rumble_high = 0;
                clients[slot].rumble_expires_ms = 0;
                clients[slot].kind = CLIENT_UNINITIALIZED;
                fprintf(stderr, "evdev client connected (fd=%d)\n", fd);
                break;
            }
        }
        if (slot == MAX_CLIENTS) {
            close(fd);
        }
    }
}

static int receive_control_messages(struct client *client)
{
    ssize_t bytes;

    if (client->control_used == sizeof(client->control)) {
        return -1;
    }
    bytes = recv(client->fd, client->control + client->control_used,
                 sizeof(client->control) - client->control_used,
                 MSG_DONTWAIT);
    if (bytes == 0) {
        return -1;
    }
    if (bytes < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ?
               0 : -1;
    }
    client->control_used += (size_t)bytes;
    while (client->control_used >= sizeof(struct tg_control_message)) {
        struct tg_control_message message;

        memcpy(&message, client->control, sizeof(message));
        if (message.magic != TG_CONTROL_MAGIC ||
            message.version != TG_CONTROL_VERSION) {
            fprintf(stderr, "invalid evdev control message\n");
            return -1;
        }
        client->control_used -= sizeof(message);
        if (client->control_used > 0) {
            memmove(client->control, client->control + sizeof(message),
                    client->control_used);
        }
        if (message.command == TG_CONTROL_RUMBLE) {
            const uint32_t duration = message.duration_ms ?
                                      message.duration_ms : 2500;
            client->rumble_low = message.low_frequency;
            client->rumble_high = message.high_frequency;
            client->rumble_expires_ms = monotonic_ms() + duration;
            fprintf(stderr, "rumble low=%u high=%u duration=%u ms\n",
                    message.low_frequency, message.high_frequency, duration);
            update_rumble(1);
        } else if (message.command == TG_CONTROL_STOP_RUMBLE) {
            client->rumble_low = 0;
            client->rumble_high = 0;
            client->rumble_expires_ms = 0;
            update_rumble(1);
        } else if (message.command == TG_CONTROL_QUERY_DESCRIPTOR) {
            client->kind = CLIENT_EVENTS;
            if (queue_bytes(client, &device_descriptor,
                            sizeof(device_descriptor)) < 0)
                return -1;
            if (device_descriptor.present) send_snapshot(client);
        } else if (message.command == TG_CONTROL_MONITOR) {
            struct tg_hotplug_message hotplug;
            client->kind = CLIENT_MONITOR;
            memset(&hotplug, 0, sizeof(hotplug));
            hotplug.magic = TG_HOTPLUG_MAGIC;
            hotplug.version = TG_CONTROL_VERSION;
            hotplug.action = device_descriptor.present ?
                             TG_HOTPLUG_ADD : TG_HOTPLUG_REMOVE;
            hotplug.descriptor = device_descriptor;
            if (queue_bytes(client, &hotplug, sizeof(hotplug)) < 0)
                return -1;
        } else {
            fprintf(stderr, "unknown evdev control command=%u\n",
                    message.command);
            return -1;
        }
    }
    return 0;
}

static int find_lorie_device(Display *display,
                             xLorieControllerQueryCapabilitiesReply *capabilities,
                             char *name, size_t name_size)
{
    XIDeviceInfo *devices;
    int count = 0;
    int device_id = -1;
    int i;

    devices = XIQueryDevice(display, XIAllDevices, &count);
    if (!devices) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if ((devices[i].use == XIFloatingSlave ||
             devices[i].use == XISlavePointer) &&
            query_lorie_capabilities(devices[i].deviceid, capabilities)) {
            device_id = devices[i].deviceid;
            snprintf(name, name_size, "%s",
                     devices[i].name ? devices[i].name : "Lorie gamepad");
            break;
        }
    }
    XIFreeDeviceInfo(devices);
    return device_id;
}

static int select_device(Display *display, int device_id)
{
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    XIEventMask mask;

    mask.deviceid = device_id;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    XISetMask(bits, XI_ButtonPress);
    XISetMask(bits, XI_ButtonRelease);
    XISetMask(bits, XI_Motion);
    if (XISelectEvents(display, DefaultRootWindow(display), &mask, 1) != Success) {
        return -1;
    }
    XFlush(display);
    return 0;
}

static int select_hierarchy(Display *display)
{
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    XIEventMask mask;

    mask.deviceid = XIAllDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    XISetMask(bits, XI_HierarchyChanged);
    if (XISelectEvents(display, DefaultRootWindow(display), &mask, 1) != Success)
        return -1;
    XFlush(display);
    return 0;
}

static int refresh_lorie_device(Display *display)
{
    xLorieControllerQueryCapabilitiesReply capabilities;
    char name[64] = {0};
    int old_device_id = lorie_device_id;
    int device_id = find_lorie_device(display, &capabilities,
                                      name, sizeof(name));

    if (device_id == old_device_id && device_id >= 0 &&
        lorie_capabilities == capabilities.capabilities &&
        device_descriptor.input_mode == capabilities.inputMode &&
        device_descriptor.vendor_id == capabilities.vendorId &&
        device_descriptor.product_id == capabilities.productId &&
        strncmp(device_descriptor.name, name,
                sizeof(device_descriptor.name)) == 0)
        return 0;
    if (old_device_id >= 0) {
        lorie_device_id = -1;
        lorie_capabilities = 0;
        memset(&state, 0, sizeof(state));
        device_descriptor.present = 0;
        device_descriptor.device_id = -1;
        broadcast_hotplug(TG_HOTPLUG_REMOVE);
        fprintf(stderr, "Lorie gamepad removed (XI2 device=%d)\n",
                old_device_id);
    }
    if (device_id < 0) return 0;
    if (select_device(display, device_id) < 0) return -1;

    lorie_device_id = device_id;
    lorie_capabilities = capabilities.capabilities;
    memset(&device_descriptor, 0, sizeof(device_descriptor));
    device_descriptor.magic = TG_DEVICE_MAGIC;
    device_descriptor.version = TG_CONTROL_VERSION;
    device_descriptor.present = 1;
    device_descriptor.capabilities =
        (lorie_capabilities & LORIE_CONTROLLER_CAP_RUMBLE) ?
        TG_DEVICE_CAP_RUMBLE : 0;
    device_descriptor.input_mode = capabilities.inputMode;
    device_descriptor.vendor_id = capabilities.vendorId;
    device_descriptor.product_id = capabilities.productId;
    device_descriptor.device_id = device_id;
    snprintf(device_descriptor.name, sizeof(device_descriptor.name), "%s", name);
    broadcast_hotplug(TG_HOTPLUG_ADD);
    fprintf(stderr,
            "Lorie gamepad added: XI2=%d vid=%04x pid=%04x mode=%u name=%s rumble=%s\n",
            device_id, capabilities.vendorId, capabilities.productId,
            capabilities.inputMode, name,
            (lorie_capabilities & LORIE_CONTROLLER_CAP_RUMBLE) ? "yes" : "no");
    return 1;
}

static void process_xi_event(const XIDeviceEvent *event, int device_id)
{
    struct input_event events[16];
    size_t count = 0;

    if (event->deviceid != device_id && event->sourceid != device_id) {
        return;
    }
    if (event->evtype == XI_Motion) {
        const double *value = event->valuators.values;
        int axis;

        for (axis = 0; axis < event->valuators.mask_len * 8; ++axis) {
            if (XIMaskIsSet(event->valuators.mask, axis)) {
                if (axis < 6) {
                    const int converted = convert_axis(axis, *value);
                    if (converted != state.axes[axis]) {
                        state.axes[axis] = converted;
                        append_event(events, &count, EV_ABS,
                                     axis_code(axis), converted);
                    }
                }
                ++value;
            }
        }
    } else if ((event->evtype == XI_ButtonPress ||
                event->evtype == XI_ButtonRelease) &&
               event->detail >= 1 && event->detail <= 16) {
        const unsigned int detail = event->detail;
        const unsigned int bit = 1U << (detail - 1);
        const int pressed = event->evtype == XI_ButtonPress;
        const int was_pressed = (state.buttons & bit) != 0;

        if (pressed != was_pressed) {
            const unsigned short code = button_code(detail);
            if (pressed) {
                state.buttons |= bit;
            } else {
                state.buttons &= ~bit;
            }
            if (detail >= 11 && detail <= 14) {
                const int new_x = hat_value(13, 14);
                const int new_y = hat_value(11, 12);
                if (new_x != state.hat_x) {
                    state.hat_x = new_x;
                    append_event(events, &count, EV_ABS, ABS_HAT0X, new_x);
                }
                if (new_y != state.hat_y) {
                    state.hat_y = new_y;
                    append_event(events, &count, EV_ABS, ABS_HAT0Y, new_y);
                }
            } else if (code != 0) {
                append_event(events, &count, EV_KEY, code, pressed);
            }
        }
    }
    if (count > 0) {
        append_event(events, &count, EV_SYN, SYN_REPORT, 0);
        broadcast_events(events, count);
    }
}

static void pump_x_events(Display *display, int xi_opcode)
{
    while (XPending(display) > 0) {
        XEvent event;
        XGenericEventCookie *cookie;

        XNextEvent(display, &event);
        cookie = &event.xcookie;
        if (cookie->type == GenericEvent && cookie->extension == xi_opcode &&
            XGetEventData(display, cookie)) {
            if (cookie->evtype == XI_HierarchyChanged) {
                (void)refresh_lorie_device(display);
            } else if (cookie->evtype == XI_Motion ||
                cookie->evtype == XI_ButtonPress ||
                cookie->evtype == XI_ButtonRelease) {
                process_xi_event((const XIDeviceEvent *)cookie->data,
                                 lorie_device_id);
            }
            XFreeEventData(display, cookie);
        }
    }
}

static int create_server(const char *path)
{
    struct sockaddr_un address;
    int fd;

    if (strlen(path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "socket path is too long: %s\n", path);
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    if (set_nonblocking(fd) < 0) {
        perror("fcntl");
        close(fd);
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, MAX_CLIENTS) < 0) {
        perror("bind/listen");
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    const char *socket_path = argc > 1 ? argv[1] : DEFAULT_SOCKET;
    Display *display;
    int xi_opcode = 0;
    int xi_event = 0;
    int xi_error = 0;
    int xi_major = 2;
    int xi_minor = 0;
    int server_fd;
    int i;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    for (i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].fd = -1;
    }

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "cannot open X display; set DISPLAY=:0\n");
        return 1;
    }
    x_display = display;
    if (!XQueryExtension(display, "XInputExtension", &xi_opcode,
                         &xi_event, &xi_error) ||
        XIQueryVersion(display, &xi_major, &xi_minor) == BadRequest ||
        xi_major < 2) {
        fprintf(stderr, "XInput2 is unavailable\n");
        XCloseDisplay(display);
        return 1;
    }
    {
        int controller_event = 0;
        int controller_error = 0;
        if (XQueryExtension(display, LORIE_CONTROLLER_NAME,
                            &lorie_controller_opcode, &controller_event,
                            &controller_error) && !query_lorie_version()) {
            lorie_controller_opcode = -1;
        }
    }
    previous_x_error_handler = XSetErrorHandler(handle_x_error);
    if (select_hierarchy(display) < 0) {
        fprintf(stderr, "unable to select XI2 hierarchy events\n");
        XCloseDisplay(display);
        return 1;
    }
    if (lorie_controller_opcode >= 0) (void)refresh_lorie_device(display);
    server_fd = create_server(socket_path);
    if (server_fd < 0) {
        XCloseDisplay(display);
        return 1;
    }
    fprintf(stderr,
            "Lorie evdev bridge: XI2 device=%d socket=%s rumble=%s\n",
            lorie_device_id, socket_path,
            (lorie_capabilities & LORIE_CONTROLLER_CAP_RUMBLE) ? "yes" : "no");

    while (running) {
        struct pollfd pollfds[2 + MAX_CLIENTS];
        int result;

        pollfds[0].fd = ConnectionNumber(display);
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;
        pollfds[1].fd = server_fd;
        pollfds[1].events = POLLIN;
        pollfds[1].revents = 0;
        for (i = 0; i < MAX_CLIENTS; ++i) {
            pollfds[2 + i].fd = clients[i].fd;
            pollfds[2 + i].events = POLLIN;
            if (clients[i].used > 0) {
                pollfds[2 + i].events |= POLLOUT;
            }
            pollfds[2 + i].revents = 0;
        }
        result = poll(pollfds, 2 + MAX_CLIENTS, 50);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (pollfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            pump_x_events(display, xi_opcode);
        }
        if (pollfds[1].revents & POLLIN) {
            accept_clients(server_fd);
        }
        for (i = 0; i < MAX_CLIENTS; ++i) {
            const short revents = pollfds[2 + i].revents;
            if (clients[i].fd < 0) {
                continue;
            }
            if (revents & POLLOUT) {
                if (flush_client(&clients[i]) < 0) {
                    close_client(&clients[i]);
                    continue;
                }
            }
            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_client(&clients[i]);
            } else if (revents & POLLIN) {
                if (receive_control_messages(&clients[i]) < 0) {
                    close_client(&clients[i]);
                }
            }
        }
        pump_x_events(display, xi_opcode);
        update_rumble(0);
    }

    (void)send_lorie_rumble(0, 0, 0);
    for (i = 0; i < MAX_CLIENTS; ++i) {
        close_client(&clients[i]);
    }
    close(server_fd);
    unlink(socket_path);
    XCloseDisplay(display);
    x_display = NULL;
    return 0;
}
