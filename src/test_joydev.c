#include <errno.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/dev/input/js99";
    int monitor = argc > 2 ? atoi(argv[2]) : 0;
    unsigned int version = 0;
    unsigned char axes = 0, buttons = 0;
    char name[128] = {0};
    struct stat st;
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode)) {
        /* The backing fd is a Unix socket; pathname stat() is separately
           spoofed as a character device for device discovery. */
        fprintf(stderr, "unexpected backing fd type\n");
        return 2;
    }
    if (ioctl(fd, JSIOCGVERSION, &version) != 0 ||
        ioctl(fd, JSIOCGAXES, &axes) != 0 ||
        ioctl(fd, JSIOCGBUTTONS, &buttons) != 0 ||
        ioctl(fd, JSIOCGNAME(sizeof(name)), name) < 0) {
        fprintf(stderr, "joydev ioctl: %s\n", strerror(errno));
        return 3;
    }
    printf("name=%s version=0x%06x axes=%u buttons=%u\n",
           name, version, axes, buttons);

    do {
        struct pollfd pollfd = {fd, POLLIN, 0};
        struct js_event events[32];
        ssize_t got;
        if (poll(&pollfd, 1, monitor ? 1000 : 100) <= 0) {
            if (!monitor) break;
            continue;
        }
        got = read(fd, events, sizeof(events));
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (got < 0) {
            fprintf(stderr, "read: %s\n", strerror(errno));
            return 4;
        }
        for (ssize_t i = 0; i < got / (ssize_t)sizeof(events[0]); ++i) {
            printf("type=0x%02x number=%u value=%d time=%u\n",
                   events[i].type, events[i].number, events[i].value,
                   events[i].time);
        }
        fflush(stdout);
    } while (monitor);

    close(fd);
    return 0;
}
