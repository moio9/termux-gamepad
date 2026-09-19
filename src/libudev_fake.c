#include "tg_common.h"
#include <termux_gamepad.h>
#include "tg_evdev_protocol.h"

/* No libudev headers required. Firefox resolves these with dlopen+dlsym. */
struct udev { int refs; };
enum fake_device_kind { FAKE_EVDEV, FAKE_JOYDEV, FAKE_PARENT };
struct udev_device { int refs; int action; enum fake_device_kind kind; };
struct udev_enumerate { int refs; int matched_input; };
struct udev_list_entry { int index; };
struct udev_monitor { int refs; int pipefd[2]; };

extern int close(int fd);
extern char *getenv(const char *name);
extern int socket(int domain, int type, int protocol);
extern int connect(int fd, const void *addr, unsigned int len);
extern tg_ssize_t send(int fd, const void *buf, tg_size_t len, int flags);
extern tg_ssize_t recv(int fd, void *buf, tg_size_t len, int flags);

struct tg_sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

static struct tg_device_descriptor g_descriptor;
static char vendor_string[5] = "0000";
static char product_string[5] = "0000";

static int udev_backend_enabled(void) {
    termux_gamepad_backend backend=termux_gamepad_get_backend();
    return backend==TERMUX_GAMEPAD_BACKEND_AUTO ||
           backend==TERMUX_GAMEPAD_BACKEND_UDEV;
}

static void format_hex4(char output[5], tg_u32 value) {
    static const char digits[]="0123456789abcdef";
    int i;
    for (i=3;i>=0;i--) { output[i]=digits[value&15U]; value>>=4; }
    output[4]=0;
}

static void update_descriptor(const struct tg_device_descriptor *descriptor) {
    if (!udev_backend_enabled()) {
        g_descriptor.present=0;
        return;
    }
    g_descriptor=*descriptor;
    format_hex4(vendor_string,g_descriptor.vendor_id);
    format_hex4(product_string,g_descriptor.product_id);
}

static int connect_bridge(tg_u16 command) {
    const char *path;
    if (!udev_backend_enabled()) return -1;
    path=getenv("TERMUX_GAMEPAD_EVDEV_SOCKET");
    struct tg_sockaddr_un address;
    struct tg_control_message message;
    int fd;
    if (!path || !*path) path=TG_DEFAULT_SOCKET;
    if (tg_strlen(path)>=sizeof(address.sun_path)) return -1;
    fd=socket(TG_AF_UNIX,TG_SOCK_STREAM|TG_SOCK_CLOEXEC,0);
    if (fd<0) return -1;
    tg_memzero(&address,sizeof(address));
    address.sun_family=TG_AF_UNIX;
    tg_copy(address.sun_path,path,sizeof(address.sun_path));
    if (connect(fd,&address,(unsigned int)sizeof(address))!=0) {
        close(fd); return -1;
    }
    tg_memzero(&message,sizeof(message));
    message.magic=TG_CONTROL_MAGIC;
    message.version=TG_CONTROL_VERSION;
    message.command=command;
    if (send(fd,&message,sizeof(message),TG_MSG_NOSIGNAL)!=(tg_ssize_t)sizeof(message)) {
        close(fd); return -1;
    }
    return fd;
}

static int refresh_descriptor(void) {
    struct tg_device_descriptor descriptor;
    int fd=connect_bridge(TG_CONTROL_QUERY_DESCRIPTOR);
    if (fd<0) return -1;
    if (recv(fd,&descriptor,sizeof(descriptor),TG_MSG_WAITALL)!=(tg_ssize_t)sizeof(descriptor) ||
        descriptor.magic!=TG_DEVICE_MAGIC || descriptor.version!=TG_CONTROL_VERSION) {
        close(fd); return -1;
    }
    close(fd);
    update_descriptor(&descriptor);
    return descriptor.present ? 0 : -1;
}

static int dinput_mode(void) {
    const char *override=getenv("TERMUX_GAMEPAD_MODE");
    if (tg_streq(override,"dinput")) return 1;
    if (tg_streq(override,"xinput")) return 0;
    return g_descriptor.input_mode==TG_INPUT_MODE_DINPUT;
}

static const char *device_name(void) {
    if (g_descriptor.name[0]) return g_descriptor.name;
    return dinput_mode() ? TG_DINPUT_NAME : TG_XINPUT_NAME;
}

static struct udev g_udev = {1};
static struct udev_device g_event_dev = {1, 0, FAKE_EVDEV};
static struct udev_device g_joy_dev = {1, 0, FAKE_JOYDEV};
static struct udev_device g_parent_dev = {1, 0, FAKE_PARENT};
static struct udev_list_entry g_entries[1] = {{0}};

static struct udev_device *device_for_syspath(const char *path) {
    if (tg_streq(path, TG_FAKE_SYSPATH)) return &g_event_dev;
    if (tg_streq(path, TG_FAKE_JOYDEV_SYSPATH)) return &g_joy_dev;
    if (tg_streq(path, TG_FAKE_PARENT_SYSPATH)) return &g_parent_dev;
    return 0;
}

__attribute__((visibility("default"))) struct udev *udev_new(void) {
    g_udev.refs++;
    return &g_udev;
}
__attribute__((visibility("default"))) struct udev *udev_ref(struct udev *u) {
    if (u) u->refs++;
    return u;
}
__attribute__((visibility("default"))) struct udev *udev_unref(struct udev *u) {
    if (u && u->refs>0) u->refs--;
    return 0;
}

__attribute__((visibility("default"))) struct udev_device *udev_device_new_from_syspath(struct udev *u, const char *path) {
    struct udev_device *d;
    (void)u;
    if (!udev_backend_enabled()) return 0;
    d=device_for_syspath(path);
    if (!d) return 0;
    d->refs++;
    return d;
}
__attribute__((visibility("default"))) struct udev_device *udev_device_ref(struct udev_device *d) {
    if (d) d->refs++;
    return d;
}
__attribute__((visibility("default"))) struct udev_device *udev_device_unref(struct udev_device *d) {
    if (d && d->refs>0) d->refs--;
    return 0;
}
__attribute__((visibility("default"))) const char *udev_device_get_devnode(struct udev_device *d) {
    if (!d) return 0;
    if (d->kind==FAKE_EVDEV) return TG_FAKE_DEVNODE;
    if (d->kind==FAKE_JOYDEV) return TG_FAKE_JOYDEV_NODE;
    return 0;
}
__attribute__((visibility("default"))) const char *udev_device_get_syspath(struct udev_device *d) {
    if (!d) return 0;
    if (d->kind==FAKE_EVDEV) return TG_FAKE_SYSPATH;
    if (d->kind==FAKE_JOYDEV) return TG_FAKE_JOYDEV_SYSPATH;
    return TG_FAKE_PARENT_SYSPATH;
}
__attribute__((visibility("default"))) const char *udev_device_get_sysname(struct udev_device *d) {
    if (!d) return 0;
    if (d->kind==FAKE_EVDEV) return "event99";
    if (d->kind==FAKE_JOYDEV) return "js99";
    return "input99";
}
__attribute__((visibility("default"))) const char *udev_device_get_subsystem(struct udev_device *d) {
    return d ? "input" : 0;
}
__attribute__((visibility("default"))) const char *udev_device_get_driver(struct udev_device *d) {
    (void)d;
    /* Avoid pretending we are xpad; Firefox will select mapping from VID/PID. */
    return 0;
}
__attribute__((visibility("default"))) struct udev_device *udev_device_get_parent(struct udev_device *d) {
    if (!d || d->kind==FAKE_PARENT) return 0;
    return &g_parent_dev;
}
__attribute__((visibility("default"))) struct udev_device *udev_device_get_parent_with_subsystem_devtype(struct udev_device *d, const char *s, const char *t) {
    (void)t;
    if (!d || d->kind==FAKE_PARENT || !tg_streq(s,"input")) return 0;
    return &g_parent_dev;
}
__attribute__((visibility("default"))) const char *udev_device_get_property_value(struct udev_device *d, const char *key) {
    if (!d || !key) return 0;
    if (tg_streq(key, "ID_INPUT_JOYSTICK")) return "1";
    if (tg_streq(key, "ID_INPUT")) return "1";
    if (tg_streq(key, "DEVNAME")) return udev_device_get_devnode(d);
    if (tg_streq(key, "SUBSYSTEM")) return "input";
    if (tg_streq(key, "NAME")) return device_name();
    if (tg_streq(key, "ID_VENDOR_ID")) return dinput_mode() ? "1209" : vendor_string;
    if (tg_streq(key, "ID_MODEL_ID")) return dinput_mode() ? "0001" : product_string;
    if (tg_streq(key, "ID_REVISION")) return "0114";
    return 0;
}
__attribute__((visibility("default"))) const char *udev_device_get_action(struct udev_device *d) {
    if (!d) return 0;
    return d->action == 2 ? "remove" : "add";
}
__attribute__((visibility("default"))) const char *udev_device_get_sysattr_value(struct udev_device *d, const char *key) {
    if (!d || !key) return 0;
    if (tg_streq(key,"id/vendor")) return dinput_mode() ? "1209" : vendor_string;
    if (tg_streq(key,"id/product")) return dinput_mode() ? "0001" : product_string;
    if (tg_streq(key,"id/version")) return "0114";
    if (tg_streq(key,"name")) return device_name();
    return 0;
}

static struct udev_enumerate g_enum;
__attribute__((visibility("default"))) struct udev_enumerate *udev_enumerate_new(struct udev *u) {
    (void)u; g_enum.refs=1; g_enum.matched_input=0; return &g_enum;
}
__attribute__((visibility("default"))) struct udev_enumerate *udev_enumerate_ref(struct udev_enumerate *e) {
    if (e) e->refs++; return e;
}
__attribute__((visibility("default"))) struct udev_enumerate *udev_enumerate_unref(struct udev_enumerate *e) {
    if (e && e->refs>0) e->refs--; return 0;
}
__attribute__((visibility("default"))) int udev_enumerate_add_match_subsystem(struct udev_enumerate *e, const char *subsystem) {
    if (!e) return -1;
    e->matched_input = tg_streq(subsystem,"input") ? 1 : 0;
    return 0;
}
__attribute__((visibility("default"))) int udev_enumerate_add_match_property(struct udev_enumerate *e, const char *key, const char *value) {
    if (!e) return -1;
    if (tg_streq(key,"ID_INPUT_JOYSTICK") && tg_streq(value,"1")) e->matched_input=1;
    return 0;
}
__attribute__((visibility("default"))) int udev_enumerate_scan_devices(struct udev_enumerate *e) {
    if (!e) return -1;
    if (!udev_backend_enabled()) { g_descriptor.present=0; return 0; }
    (void)refresh_descriptor();
    return 0;
}
__attribute__((visibility("default"))) struct udev_list_entry *udev_enumerate_get_list_entry(struct udev_enumerate *e) {
    if (!udev_backend_enabled()) return 0;
    if (!e || !e->matched_input || !g_descriptor.present) return 0;
    return &g_entries[0];
}
__attribute__((visibility("default"))) struct udev_list_entry *udev_list_entry_get_next(struct udev_list_entry *e) {
    (void)e;
    return 0;
}
__attribute__((visibility("default"))) const char *udev_list_entry_get_name(struct udev_list_entry *e) {
    if (!e) return 0;
    return TG_FAKE_SYSPATH;
}

static struct udev_monitor g_monitor;
__attribute__((visibility("default"))) struct udev_monitor *udev_monitor_new_from_netlink(struct udev *u, const char *name) {
    if (!udev_backend_enabled()) return 0;
    (void)u; (void)name;
    g_monitor.refs=1;
    g_monitor.pipefd[0]=-1; g_monitor.pipefd[1]=-1;
    g_monitor.pipefd[0]=connect_bridge(TG_CONTROL_MONITOR);
    if (g_monitor.pipefd[0]<0) return 0;
    return &g_monitor;
}
__attribute__((visibility("default"))) struct udev_monitor *udev_monitor_ref(struct udev_monitor *m) {
    if (m) m->refs++; return m;
}
__attribute__((visibility("default"))) int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *m, const char *s, const char *t) {
    (void)m; (void)s; (void)t; return 0;
}
__attribute__((visibility("default"))) int udev_monitor_enable_receiving(struct udev_monitor *m) {
    if (!udev_backend_enabled()) return -1;
    return m ? 0 : -1;
}
__attribute__((visibility("default"))) int udev_monitor_get_fd(struct udev_monitor *m) {
    if (!udev_backend_enabled()) return -1;
    return m ? m->pipefd[0] : -1;
}
__attribute__((visibility("default"))) struct udev_device *udev_monitor_receive_device(struct udev_monitor *m) {
    struct tg_hotplug_message message;
    if (!udev_backend_enabled()) return 0;
    if (!m || m->pipefd[0]<0) return 0;
    if (recv(m->pipefd[0],&message,sizeof(message),TG_MSG_WAITALL)!=(tg_ssize_t)sizeof(message) ||
        message.magic!=TG_HOTPLUG_MAGIC || message.version!=TG_CONTROL_VERSION)
        return 0;
    update_descriptor(&message.descriptor);
    g_event_dev.action=message.action==TG_HOTPLUG_REMOVE ? 2 : 1;
    g_event_dev.refs++;
    return &g_event_dev;
}
__attribute__((visibility("default"))) struct udev_monitor *udev_monitor_unref(struct udev_monitor *m) {
    if (!m) return 0;
    if (m->pipefd[0] >= 0) { close(m->pipefd[0]); m->pipefd[0]=-1; }
    if (m->pipefd[1] >= 0) { close(m->pipefd[1]); m->pipefd[1]=-1; }
    if (m->refs>0) m->refs--;
    return 0;
}

__attribute__((visibility("default"))) struct udev_device *udev_device_new_from_devnum(struct udev *u, char type, tg_ulong devnum) {
    if (!udev_backend_enabled()) return 0;
    (void)u; (void)type; (void)devnum;
    g_event_dev.refs++;
    return &g_event_dev;
}
__attribute__((visibility("default"))) tg_ulong udev_device_get_devnum(struct udev_device *d) {
    (void)d;
    return 0;
}
