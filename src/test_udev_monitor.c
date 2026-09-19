#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *(*fn0)(void);
typedef void *(*fn1)(void *);
typedef void *(*fn2s)(void *, const char *);
typedef int (*fn1i)(void *);
typedef const char *(*fnstr1)(void *);
typedef const char *(*fnstr2)(void *, const char *);

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./libudev.so.1";
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    fn0 udev_new;
    fn2s monitor_new;
    fn1i monitor_enable, monitor_fd;
    fn1 monitor_receive;
    fnstr1 device_action, device_node;
    fnstr2 device_property;
    struct pollfd pollfd;
    void *udev, *monitor, *device;
    int count = argc > 2 ? atoi(argv[2]) : 1;
    int i;

    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    udev_new = (fn0)dlsym(library, "udev_new");
    monitor_new = (fn2s)dlsym(library, "udev_monitor_new_from_netlink");
    monitor_enable = (fn1i)dlsym(library, "udev_monitor_enable_receiving");
    monitor_fd = (fn1i)dlsym(library, "udev_monitor_get_fd");
    monitor_receive = (fn1)dlsym(library, "udev_monitor_receive_device");
    device_action = (fnstr1)dlsym(library, "udev_device_get_action");
    device_node = (fnstr1)dlsym(library, "udev_device_get_devnode");
    device_property = (fnstr2)dlsym(library, "udev_device_get_property_value");
    if (!udev_new || !monitor_new || !monitor_enable || !monitor_fd ||
        !monitor_receive || !device_action || !device_node || !device_property)
        return 2;
    udev = udev_new();
    monitor = monitor_new(udev, "udev");
    if (!monitor || monitor_enable(monitor) < 0) return 3;
    pollfd.fd = monitor_fd(monitor);
    pollfd.events = POLLIN;
    pollfd.revents = 0;
    if (count < 1) count = 1;
    for (i = 0; i < count; ++i) {
        pollfd.revents = 0;
        if (poll(&pollfd, 1, 30000) <= 0) return 4;
        device = monitor_receive(monitor);
        if (!device) return 5;
        printf("action=%s devnode=%s name=%s vid=%s pid=%s\n",
               device_action(device), device_node(device),
               device_property(device, "NAME"),
               device_property(device, "ID_VENDOR_ID"),
               device_property(device, "ID_MODEL_ID"));
        fflush(stdout);
    }
    return 0;
}
