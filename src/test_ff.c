#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int has_bit(const unsigned char *bits, unsigned int bit)
{
    return (bits[bit / 8] & (1U << (bit % 8))) != 0;
}

int main(void)
{
    unsigned char ff_bits[(FF_MAX + 8) / 8];
    struct ff_effect effect;
    struct input_event event;
    int fd;

    fd = open("/dev/input/event99", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }
    memset(ff_bits, 0, sizeof(ff_bits));
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) < 0 ||
        !has_bit(ff_bits, FF_RUMBLE)) {
        fprintf(stderr, "FF_RUMBLE is unavailable\n");
        close(fd);
        return 2;
    }
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.replay.length = 500;
    effect.u.rumble.strong_magnitude = 0x6000;
    effect.u.rumble.weak_magnitude = 0xffff;
    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        fprintf(stderr, "EVIOCSFF failed: %s\n", strerror(errno));
        close(fd);
        return 3;
    }
    memset(&event, 0, sizeof(event));
    event.type = EV_FF;
    event.code = (unsigned short)effect.id;
    event.value = 1;
    if (write(fd, &event, sizeof(event)) != (ssize_t)sizeof(event)) {
        fprintf(stderr, "start write failed: %s\n", strerror(errno));
        close(fd);
        return 4;
    }
    printf("FF_RUMBLE effect=%d started for 500 ms\n", effect.id);
    usleep(550000);
    event.value = 0;
    if (write(fd, &event, sizeof(event)) != (ssize_t)sizeof(event)) {
        fprintf(stderr, "stop write failed: %s\n", strerror(errno));
        close(fd);
        return 5;
    }
    if (ioctl(fd, EVIOCRMFF, effect.id) < 0) {
        fprintf(stderr, "EVIOCRMFF failed: %s\n", strerror(errno));
        close(fd);
        return 6;
    }
    close(fd);
    return 0;
}
