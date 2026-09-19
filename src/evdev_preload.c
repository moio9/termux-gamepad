#include "tg_common.h"
#include <termux_gamepad.h>
#include "tg_evdev_protocol.h"

/* Minimal Bionic declarations keep the shim small and its hooked ABI explicit. */
extern void *dlsym(void *handle, const char *name);
extern char *getenv(const char *name);
extern int socket(int domain, int type, int protocol);
extern int connect(int fd, const void *addr, unsigned int len);
extern tg_ssize_t send(int fd, const void *buf, tg_size_t len, int flags);
extern tg_ssize_t recv(int fd, void *buf, tg_size_t len, int flags);
extern tg_ssize_t read(int fd, void *buf, tg_size_t len);
extern int close(int fd);
extern int fcntl(int fd, int command, ...);
extern int *__errno(void); /* Bionic */

#ifndef RTLD_NEXT
#define RTLD_NEXT ((void*)(tg_long)-1)
#endif

struct tg_sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

typedef int (*tg_open_fn)(const char*, int, unsigned int);
typedef int (*tg_openat_fn)(int, const char*, int, unsigned int);
typedef int (*tg_ioctl_fn)(int, tg_ulong, tg_ulong);
typedef int (*tg_close_fn)(int);
typedef tg_ssize_t (*tg_write_fn)(int, const void *, tg_size_t);
typedef tg_ssize_t (*tg_read_fn)(int, void *, tg_size_t);
typedef int (*tg_stat_fn)(const char *, void *);

static tg_open_fn real_open_fn;
static tg_open_fn real_open64_fn;
static tg_open_fn real___open_2_fn;
static tg_openat_fn real_openat_fn;
static tg_openat_fn real___openat_2_fn;
static tg_ioctl_fn real_ioctl_fn;

static int tg_backend_enabled(void) {
    termux_gamepad_backend backend=termux_gamepad_get_backend();
    return backend==TERMUX_GAMEPAD_BACKEND_AUTO ||
           backend==TERMUX_GAMEPAD_BACKEND_UDEV;
}
static tg_close_fn real_close_fn;
static tg_write_fn real_write_fn;
static tg_read_fn real_read_fn;
static tg_stat_fn real_stat_fn;
static tg_stat_fn real_stat64_fn;

#define TG_MAX_FAKE_FDS 128
struct tg_fake_fd {
    int fd;
    int joydev;
    int initializing;
    tg_size_t raw_used;
    union {
        struct tg_input_event event;
        tg_u8 bytes[sizeof(struct tg_input_event)];
    } raw;
    struct tg_device_descriptor descriptor;
};
static struct tg_fake_fd fake_fds[TG_MAX_FAKE_FDS];
static int fake_count;
static int init_done;
static int mode_override = -1;

#define TG_MAX_EFFECTS 16
#define TG_F_SETFL 4
#define TG_F_SETFD 2
#define TG_FD_CLOEXEC 1

struct tg_effect_slot {
    int used;
    int fd;
    tg_u16 low_frequency;
    tg_u16 high_frequency;
    tg_u16 duration_ms;
};

static struct tg_effect_slot effect_slots[TG_MAX_EFFECTS];
static tg_u16 ff_gain = 0xffff;

struct tg_stat_layout {
    tg_ulong st_dev;
    tg_ulong st_ino;
    tg_u32 st_mode;
    tg_u32 st_nlink;
    tg_u32 st_uid;
    tg_u32 st_gid;
    tg_ulong st_rdev;
    tg_ulong pad1;
    tg_long st_size;
    tg_s32 st_blksize;
    tg_s32 pad2;
    tg_long st_blocks;
    tg_long st_atime_sec;
    tg_long st_atime_nsec;
    tg_long st_mtime_sec;
    tg_long st_mtime_nsec;
    tg_long st_ctime_sec;
    tg_long st_ctime_nsec;
    tg_u32 unused4;
    tg_u32 unused5;
};

typedef char tg_stat_layout_must_be_128_bytes[
    sizeof(struct tg_stat_layout) == 128 ? 1 : -1];

static void tg_init(void) {
    const char *mode;
    if (init_done) return;
    init_done=1;
    mode=getenv("TERMUX_GAMEPAD_MODE");
    if (tg_streq(mode,"dinput")) mode_override=1;
    else if (tg_streq(mode,"xinput")) mode_override=0;
    real_open_fn=(tg_open_fn)dlsym(RTLD_NEXT,"open");
    real_open64_fn=(tg_open_fn)dlsym(RTLD_NEXT,"open64");
    real___open_2_fn=(tg_open_fn)dlsym(RTLD_NEXT,"__open_2");
    real_openat_fn=(tg_openat_fn)dlsym(RTLD_NEXT,"openat");
    real___openat_2_fn=(tg_openat_fn)dlsym(RTLD_NEXT,"__openat_2");
    real_ioctl_fn=(tg_ioctl_fn)dlsym(RTLD_NEXT,"ioctl");
    real_close_fn=(tg_close_fn)dlsym(RTLD_NEXT,"close");
    real_write_fn=(tg_write_fn)dlsym(RTLD_NEXT,"write");
    real_read_fn=(tg_read_fn)dlsym(RTLD_NEXT,"read");
    real_stat_fn=(tg_stat_fn)dlsym(RTLD_NEXT,"stat");
    real_stat64_fn=(tg_stat_fn)dlsym(RTLD_NEXT,"stat64");
}

static int tg_is_dinput(const struct tg_fake_fd *fake) {
    if (mode_override >= 0) return mode_override;
    return fake && fake->descriptor.input_mode == TG_INPUT_MODE_DINPUT;
}

static const char *tg_device_name(const struct tg_fake_fd *fake) {
    if (fake && fake->descriptor.name[0]) return fake->descriptor.name;
    if (tg_is_dinput(fake)) return TG_DINPUT_NAME;
    return TG_XINPUT_NAME;
}

static tg_u16 tg_dinput_button_code(tg_u16 code) {
    switch (code) {
        case TG_BTN_SOUTH: return TG_BTN_TRIGGER;
        case TG_BTN_EAST: return TG_BTN_THUMB;
        case TG_BTN_WEST: return TG_BTN_THUMB2;
        case TG_BTN_NORTH: return TG_BTN_TOP;
        case TG_BTN_TL: return TG_BTN_TOP2;
        case TG_BTN_TR: return TG_BTN_PINKIE;
        case TG_BTN_SELECT: return TG_BTN_BASE;
        case TG_BTN_START: return TG_BTN_BASE2;
        case TG_BTN_MODE: return TG_BTN_BASE3;
        case TG_BTN_THUMBL: return TG_BTN_BASE4;
        case TG_BTN_THUMBR: return TG_BTN_BASE5;
        case 0x132: return TG_BTN_BASE6;
        default: return code;
    }
}

static struct tg_fake_fd *tg_find_fake_fd(int fd) {
    int i; for (i=0;i<fake_count;i++) if (fake_fds[i].fd==fd) return &fake_fds[i]; return 0;
}
static int tg_is_fake_fd(int fd) { return tg_find_fake_fd(fd)!=0; }
static void tg_add_fake_fd(int fd, int joydev,
                           const struct tg_device_descriptor *descriptor) {
    struct tg_fake_fd *slot;
    if (fd<0 || tg_is_fake_fd(fd) || fake_count>=TG_MAX_FAKE_FDS) return;
    slot=&fake_fds[fake_count++];
    tg_memzero(slot,sizeof(*slot));
    slot->fd=fd;
    slot->joydev=joydev;
    slot->initializing=joydev;
    if (descriptor) slot->descriptor=*descriptor;
}
static void tg_del_fake_fd(int fd) {
    int i; for (i=0;i<fake_count;i++) if (fake_fds[i].fd==fd) { fake_fds[i]=fake_fds[--fake_count]; return; }
}

static int tg_connect_device(int flags, int joydev) {
    const char *path=getenv("TERMUX_GAMEPAD_EVDEV_SOCKET");
    struct tg_sockaddr_un un;
    int fd;
    struct tg_control_message request;
    struct tg_device_descriptor descriptor;
    if (!path || !*path) path=TG_DEFAULT_SOCKET;
    if (tg_strlen(path) >= sizeof(un.sun_path)) return -1;
    fd=socket(TG_AF_UNIX,TG_SOCK_STREAM|TG_SOCK_CLOEXEC,0);
    if (fd<0) return -1;
    tg_memzero(&un,sizeof(un));
    un.sun_family=TG_AF_UNIX;
    tg_copy(un.sun_path,path,sizeof(un.sun_path));
    if (connect(fd,&un,(unsigned int)sizeof(un)) != 0) {
        close(fd);
        return -1;
    }
    tg_memzero(&request,sizeof(request));
    request.magic=TG_CONTROL_MAGIC;
    request.version=TG_CONTROL_VERSION;
    request.command=TG_CONTROL_QUERY_DESCRIPTOR;
    if (send(fd,&request,sizeof(request),TG_MSG_NOSIGNAL)!=(tg_ssize_t)sizeof(request) ||
        recv(fd,&descriptor,sizeof(descriptor),TG_MSG_WAITALL)!=(tg_ssize_t)sizeof(descriptor) ||
        descriptor.magic!=TG_DEVICE_MAGIC ||
        descriptor.version!=TG_CONTROL_VERSION || !descriptor.present) {
        close(fd);
        *__errno()=TG_ENODEV;
        return -1;
    }
    if (flags & TG_O_NONBLOCK) (void)fcntl(fd,TG_F_SETFL,TG_O_NONBLOCK);
    (void)fcntl(fd,TG_F_SETFD,TG_FD_CLOEXEC);
    tg_add_fake_fd(fd,joydev,&descriptor);
    return fd;
}

static void tg_debug_open_path(const char *path) {
    static const char prefix[]="/dev/input/";
    const char *enabled=getenv("TERMUX_GAMEPAD_DEBUG");
    tg_size_t i;
    if (!enabled || !*enabled || !path || !real_write_fn) return;
    for (i=0;prefix[i];i++) if (path[i]!=prefix[i]) return;
    (void)real_write_fn(2,"termux-gamepad open: ",21);
    (void)real_write_fn(2,path,tg_strlen(path));
    (void)real_write_fn(2,"\n",1);
}

static int tg_open_common(const char *path, int flags, unsigned int mode, tg_open_fn fallback) {
    tg_init();
    if (!tg_backend_enabled()) return fallback(path,flags,mode);
    tg_debug_open_path(path);
    if (tg_streq(path,TG_FAKE_DEVNODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_HAPTIC_ALIAS_NODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_JOYDEV_NODE)) return tg_connect_device(flags,1);
    if (!fallback) return -1;
    return fallback(path,flags,mode);
}

/* SDL uses this marker to prefer the evdev route only when this shim was
 * actually accepted by the dynamic loader. Secure executables that discard
 * LD_PRELOAD do not expose the symbol and fall back to libtermux-gamepad. */
__attribute__((visibility("default"))) int termux_evdev_shim_active(void) {
    return tg_backend_enabled();
}

__attribute__((visibility("default"))) int open(const char *path, int flags, unsigned int mode) {
    return tg_open_common(path,flags,mode,real_open_fn);
}
__attribute__((visibility("default"))) int open64(const char *path, int flags, unsigned int mode) {
    tg_init();
    return tg_open_common(path,flags,mode,real_open64_fn ? real_open64_fn : real_open_fn);
}
__attribute__((visibility("default"))) int __open_2(const char *path, int flags, unsigned int ignored) {
    (void)ignored; tg_init();
    return tg_open_common(path,flags,0,real___open_2_fn ? real___open_2_fn : real_open_fn);
}
__attribute__((visibility("default"))) int openat(int dirfd, const char *path, int flags, unsigned int mode) {
    tg_init();
    if (tg_streq(path,TG_FAKE_DEVNODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_HAPTIC_ALIAS_NODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_JOYDEV_NODE)) return tg_connect_device(flags,1);
    if (!real_openat_fn) return -1;
    return real_openat_fn(dirfd,path,flags,mode);
}
__attribute__((visibility("default"))) int __openat_2(int dirfd, const char *path, int flags, unsigned int ignored) {
    (void)ignored; tg_init();
    if (tg_streq(path,TG_FAKE_DEVNODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_HAPTIC_ALIAS_NODE)) return tg_connect_device(flags,0);
    if (tg_streq(path,TG_FAKE_JOYDEV_NODE)) return tg_connect_device(flags,1);
    if (real___openat_2_fn) return real___openat_2_fn(dirfd,path,flags,0);
    if (real_openat_fn) return real_openat_fn(dirfd,path,flags,0);
    return -1;
}

static void tg_fill_bits(tg_u8 *dst, tg_size_t len, unsigned ev,
                         int is_dinput) {
    tg_memzero(dst,len);
    if (ev==TG_EV_KEY) {
        if (is_dinput) {
            unsigned code;
            for (code=TG_BTN_TRIGGER;code<=TG_BTN_BASE6;code++)
                tg_set_bit(dst,len,code);
        } else {
            tg_set_bit(dst,len,TG_BTN_SOUTH); tg_set_bit(dst,len,TG_BTN_EAST);
            tg_set_bit(dst,len,TG_BTN_WEST); tg_set_bit(dst,len,TG_BTN_NORTH);
            tg_set_bit(dst,len,TG_BTN_TL); tg_set_bit(dst,len,TG_BTN_TR);
            tg_set_bit(dst,len,TG_BTN_SELECT); tg_set_bit(dst,len,TG_BTN_START);
            tg_set_bit(dst,len,TG_BTN_MODE); tg_set_bit(dst,len,TG_BTN_THUMBL);
            tg_set_bit(dst,len,TG_BTN_THUMBR);
            tg_set_bit(dst,len,0x132); /* BTN_C: Android controller misc button. */
        }
    } else if (ev==TG_EV_ABS) {
        tg_set_bit(dst,len,TG_ABS_X); tg_set_bit(dst,len,TG_ABS_Y);
        tg_set_bit(dst,len,TG_ABS_RX); tg_set_bit(dst,len,TG_ABS_RY);
        tg_set_bit(dst,len,TG_ABS_Z); tg_set_bit(dst,len,TG_ABS_RZ);
        tg_set_bit(dst,len,TG_ABS_HAT0X); tg_set_bit(dst,len,TG_ABS_HAT0Y);
    } else if (ev==TG_EV_FF) {
        tg_set_bit(dst,len,TG_FF_RUMBLE); tg_set_bit(dst,len,TG_FF_GAIN);
        if (is_dinput) {
            tg_set_bit(dst,len,TG_FF_CONSTANT);
            tg_set_bit(dst,len,TG_FF_SINE);
            tg_set_bit(dst,len,TG_FF_TRIANGLE);
            tg_set_bit(dst,len,TG_FF_SAW_UP);
            tg_set_bit(dst,len,TG_FF_SAW_DOWN);
            tg_set_bit(dst,len,TG_FF_RAMP);
            tg_set_bit(dst,len,TG_FF_SPRING);
            tg_set_bit(dst,len,TG_FF_DAMPER);
            tg_set_bit(dst,len,TG_FF_INERTIA);
            tg_set_bit(dst,len,TG_FF_FRICTION);
        }
    } else if (ev==0) {
        tg_set_bit(dst,len,TG_EV_KEY); tg_set_bit(dst,len,TG_EV_ABS);
        tg_set_bit(dst,len,TG_EV_SYN); tg_set_bit(dst,len,TG_EV_FF);
    }
}

static int tg_find_effect(int fd, int id) {
    int i;
    if (id<0 || id>=TG_MAX_EFFECTS) return -1;
    i=id;
    return effect_slots[i].used && effect_slots[i].fd==fd ? i : -1;
}

static tg_u16 tg_scaled_abs(tg_s32 value) {
    if (value<0) value=-value;
    if (value>32767) value=32767;
    return (tg_u16)(value*2);
}

static int tg_is_condition_effect(tg_u16 type) {
    return type==TG_FF_SPRING || type==TG_FF_DAMPER ||
           type==TG_FF_INERTIA || type==TG_FF_FRICTION;
}

static tg_u16 tg_condition_strength(const struct tg_ff_condition_effect *condition) {
    tg_u16 strength=condition->right_saturation;
    tg_u16 coefficient;
    if (condition->left_saturation>strength) strength=condition->left_saturation;
    coefficient=tg_scaled_abs(condition->right_coeff);
    if (coefficient>strength) strength=coefficient;
    coefficient=tg_scaled_abs(condition->left_coeff);
    if (coefficient>strength) strength=coefficient;
    return strength;
}

static int tg_upload_effect(int fd, struct tg_ff_effect *effect) {
    int id=effect->id;
    int i;
    if (effect->type!=TG_FF_RUMBLE && effect->type!=TG_FF_CONSTANT &&
        effect->type!=TG_FF_PERIODIC && effect->type!=TG_FF_RAMP &&
        !tg_is_condition_effect(effect->type)) return -1;
    if (id==-1) {
        for (i=0;i<TG_MAX_EFFECTS;i++) if (!effect_slots[i].used) { id=i; break; }
        if (id==-1) return -1;
    } else if (id<0 || id>=TG_MAX_EFFECTS ||
               (effect_slots[id].used && effect_slots[id].fd!=fd)) {
        return -1;
    }
    effect_slots[id].used=1;
    effect_slots[id].fd=fd;
    if (effect->type==TG_FF_CONSTANT) {
        effect_slots[id].low_frequency=tg_scaled_abs(effect->u.constant.level);
        effect_slots[id].high_frequency=effect_slots[id].low_frequency;
    } else if (effect->type==TG_FF_PERIODIC) {
        effect_slots[id].low_frequency=tg_scaled_abs(effect->u.periodic.magnitude);
        effect_slots[id].high_frequency=effect_slots[id].low_frequency;
    } else if (effect->type==TG_FF_RAMP) {
        tg_u16 start=tg_scaled_abs(effect->u.ramp.start_level);
        tg_u16 end=tg_scaled_abs(effect->u.ramp.end_level);
        effect_slots[id].low_frequency=start>end ? start : end;
        effect_slots[id].high_frequency=effect_slots[id].low_frequency;
    } else if (tg_is_condition_effect(effect->type)) {
        tg_u16 x=tg_condition_strength(&effect->u.condition[0]);
        tg_u16 y=tg_condition_strength(&effect->u.condition[1]);
        effect_slots[id].low_frequency=x;
        effect_slots[id].high_frequency=y;
    } else {
        effect_slots[id].low_frequency=effect->u.rumble.strong_magnitude;
        effect_slots[id].high_frequency=effect->u.rumble.weak_magnitude;
    }
    effect_slots[id].duration_ms=effect->replay_length;
    effect->id=(tg_s16)id;
    return 0;
}

static int tg_remove_effect(int fd, int id) {
    int slot=tg_find_effect(fd,id);
    if (slot<0) return -1;
    effect_slots[slot].used=0;
    return 0;
}

static int tg_send_control(int fd, tg_u16 command, tg_u16 low, tg_u16 high,
                           tg_u32 duration_ms) {
    struct tg_control_message message;
    tg_ssize_t sent;
    message.magic=TG_CONTROL_MAGIC;
    message.version=TG_CONTROL_VERSION;
    message.command=command;
    message.low_frequency=low;
    message.high_frequency=high;
    message.duration_ms=duration_ms;
    sent=send(fd,&message,sizeof(message),TG_MSG_NOSIGNAL);
    return sent==(tg_ssize_t)sizeof(message) ? 0 : -1;
}

static void tg_clear_effects(int fd) {
    int i;
    for (i=0;i<TG_MAX_EFFECTS;i++) {
        if (effect_slots[i].used && effect_slots[i].fd==fd) effect_slots[i].used=0;
    }
}

static int tg_fake_ioctl(const struct tg_fake_fd *fake, tg_ulong req, tg_ulong arg) {
    tg_u32 nr=(tg_u32)((req >> TG_IOC_NRSHIFT) & 0xffu);
    tg_u32 type=(tg_u32)((req >> TG_IOC_TYPESHIFT) & 0xffu);
    tg_u32 size=(tg_u32)((req >> TG_IOC_SIZESHIFT) & ((1u<<TG_IOC_SIZEBITS)-1));
    void *p=(void*)arg;
    if (type!='E') return -1;

    if (nr==0x01 && p && size>=sizeof(int)) { *(int*)p=0x010001; return 0; }
    if (nr==0x02 && p && size>=sizeof(struct tg_input_id)) {
        struct tg_input_id *id=(struct tg_input_id*)p;
        id->bustype=TG_BUS_USB;
        id->vendor=tg_is_dinput(fake) ? TG_DINPUT_VENDOR : (tg_u16)fake->descriptor.vendor_id;
        id->product=tg_is_dinput(fake) ? TG_DINPUT_PRODUCT : (tg_u16)fake->descriptor.product_id;
        id->version=0x0114;
        return 0;
    }
    if (nr==0x06 && p && size) {
        const char *name=tg_device_name(fake);
        tg_copy((char*)p,name,size);
        return (int)tg_strlen(name)+1;
    }
    if ((nr==0x20 || nr==0x20+TG_EV_KEY || nr==0x20+TG_EV_REL ||
         nr==0x20+TG_EV_ABS ||
         nr==0x20+TG_EV_FF) && p) {
        unsigned ev=nr-0x20;
        tg_fill_bits((tg_u8*)p,size,ev,tg_is_dinput(fake));
        return 0;
    }
    if (nr>=0x40 && nr<=0x40+TG_ABS_MAX && p && size>=sizeof(struct tg_input_absinfo)) {
        unsigned code=nr-0x40;
        struct tg_input_absinfo *a=(struct tg_input_absinfo*)p;
        tg_memzero(a,sizeof(*a));
        if (code==TG_ABS_Z || code==TG_ABS_RZ) { a->minimum=0; a->maximum=255; a->flat=0; }
        else if (code==TG_ABS_HAT0X || code==TG_ABS_HAT0Y) { a->minimum=-1; a->maximum=1; }
        else { a->minimum=-32768; a->maximum=32767; a->flat=4096; }
        return 0;
    }
    if (nr==0x84 && p && size>=sizeof(int)) { *(int*)p=TG_MAX_EFFECTS; return 0; }
    return -1;
}

static int tg_fake_joy_ioctl(const struct tg_fake_fd *fake, tg_ulong req, tg_ulong arg) {
    tg_u32 nr=(tg_u32)((req >> TG_IOC_NRSHIFT) & 0xffu);
    tg_u32 type=(tg_u32)((req >> TG_IOC_TYPESHIFT) & 0xffu);
    tg_u32 size=(tg_u32)((req >> TG_IOC_SIZESHIFT) & ((1u<<TG_IOC_SIZEBITS)-1));
    void *p=(void*)arg;
    if (type!='j' || !p) return -1;
    if (nr==0x01 && size>=sizeof(tg_u32)) { *(tg_u32*)p=0x020100; return 0; }
    if (nr==0x11 && size>=sizeof(tg_u8)) { *(tg_u8*)p=8; return 0; }
    if (nr==0x12 && size>=sizeof(tg_u8)) { *(tg_u8*)p=12; return 0; }
    if (nr==0x13 && size) {
        const char *name=tg_device_name(fake);
        tg_copy((char*)p,name,size);
        return (int)tg_strlen(name)+1;
    }
    return -1;
}

/* Fixed third argument deliberately preserves the dynamic ABI and safely forwards even 2-arg ioctl callers. */
__attribute__((visibility("default"))) int ioctl(int fd, tg_ulong req, tg_ulong arg) {
    struct tg_fake_fd *fake;
    tg_init();
    fake=tg_find_fake_fd(fd);
    if (fake) {
        tg_u32 nr=(tg_u32)((req >> TG_IOC_NRSHIFT) & 0xffu);
        tg_u32 size=(tg_u32)((req >> TG_IOC_SIZESHIFT) & ((1u<<TG_IOC_SIZEBITS)-1));
        void *p=(void*)arg;
        if (fake->joydev) return tg_fake_joy_ioctl(fake,req,arg);
        if (nr==0x80 && p && size>=sizeof(struct tg_ff_effect))
            return tg_upload_effect(fd,(struct tg_ff_effect*)p);
        if (nr==0x81 && size>=sizeof(int)) return tg_remove_effect(fd,(int)arg);
        return tg_fake_ioctl(fake,req,arg);
    }
    if (!real_ioctl_fn) return -1;
    return real_ioctl_fn(fd,req,arg);
}

static int tg_joy_axis_number(tg_u16 code) {
    switch (code) {
        case TG_ABS_X: return 0;
        case TG_ABS_Y: return 1;
        case TG_ABS_Z: return 2;
        case TG_ABS_RX: return 3;
        case TG_ABS_RY: return 4;
        case TG_ABS_RZ: return 5;
        case TG_ABS_HAT0X: return 6;
        case TG_ABS_HAT0Y: return 7;
        default: return -1;
    }
}

static int tg_joy_button_number(tg_u16 code) {
    switch (code) {
        case TG_BTN_SOUTH: return 0;
        case TG_BTN_EAST: return 1;
        case TG_BTN_NORTH: return 2;
        case TG_BTN_WEST: return 3;
        case TG_BTN_TL: return 4;
        case TG_BTN_TR: return 5;
        case TG_BTN_SELECT: return 6;
        case TG_BTN_START: return 7;
        case TG_BTN_MODE: return 8;
        case TG_BTN_THUMBL: return 9;
        case TG_BTN_THUMBR: return 10;
        case 0x132: return 11; /* BTN_C / misc */
        default: return -1;
    }
}

static tg_s16 tg_joy_axis_value(tg_u16 code, tg_s32 value) {
    if (code==TG_ABS_Z || code==TG_ABS_RZ) {
        if (value<0) value=0; else if (value>255) value=255;
        return (tg_s16)(value*257-32767);
    }
    if (code==TG_ABS_HAT0X || code==TG_ABS_HAT0Y) {
        if (value<0) return -32767;
        if (value>0) return 32767;
        return 0;
    }
    if (value<-32767) value=-32767; else if (value>32767) value=32767;
    return (tg_s16)value;
}

static int tg_convert_joy_event(struct tg_fake_fd *fake,
                                const struct tg_input_event *input,
                                struct tg_js_event *output) {
    int number;
    tg_memzero(output,sizeof(*output));
    output->time=(tg_u32)(input->time.tv_sec*1000L+input->time.tv_usec/1000L);
    if (input->type==TG_EV_SYN && input->code==TG_SYN_REPORT) {
        fake->initializing=0;
        return 0;
    }
    if (input->type==TG_EV_ABS) {
        number=tg_joy_axis_number(input->code);
        if (number<0) return 0;
        output->type=TG_JS_EVENT_AXIS;
        output->number=(tg_u8)number;
        output->value=tg_joy_axis_value(input->code,input->value);
    } else if (input->type==TG_EV_KEY) {
        number=tg_joy_button_number(input->code);
        if (number<0) return 0;
        output->type=TG_JS_EVENT_BUTTON;
        output->number=(tg_u8)number;
        output->value=input->value ? 1 : 0;
    } else {
        return 0;
    }
    if (fake->initializing) output->type|=TG_JS_EVENT_INIT;
    return 1;
}

__attribute__((visibility("default"))) tg_ssize_t read(int fd, void *buffer, tg_size_t length) {
    struct tg_fake_fd *fake;
    struct tg_js_event *events=(struct tg_js_event*)buffer;
    tg_size_t capacity;
    tg_size_t produced=0;
    tg_init();
    fake=tg_find_fake_fd(fd);
    if (!fake)
        return real_read_fn ? real_read_fn(fd,buffer,length) : -1;
    if (!fake->joydev) {
        tg_ssize_t got=real_read_fn ? real_read_fn(fd,buffer,length) : -1;
        tg_size_t i;
        if (!tg_is_dinput(fake) || got<=0 || !buffer) return got;
        for (i=0;i+(tg_size_t)sizeof(struct tg_input_event)<=(tg_size_t)got;
             i+=sizeof(struct tg_input_event)) {
            struct tg_input_event *event=(struct tg_input_event*)((tg_u8*)buffer+i);
            if (event->type==TG_EV_KEY)
                event->code=tg_dinput_button_code(event->code);
        }
        return got;
    }
    if (!buffer || length<sizeof(*events)) return -1;
    capacity=length/sizeof(*events);
    while (produced<capacity) {
        while (fake->raw_used<sizeof(fake->raw.bytes)) {
            tg_ssize_t got;
            if (!real_read_fn) return produced ? (tg_ssize_t)(produced*sizeof(*events)) : -1;
            got=real_read_fn(fd,fake->raw.bytes+fake->raw_used,
                             sizeof(fake->raw.bytes)-fake->raw_used);
            if (got<=0) return produced ? (tg_ssize_t)(produced*sizeof(*events)) : got;
            fake->raw_used+=(tg_size_t)got;
        }
        fake->raw_used=0;
        if (tg_convert_joy_event(fake,&fake->raw.event,&events[produced]))
            produced++;
    }
    return (tg_ssize_t)(produced*sizeof(*events));
}

__attribute__((visibility("default"))) tg_ssize_t write(int fd, const void *buffer, tg_size_t length) {
    const struct tg_input_event *events=(const struct tg_input_event*)buffer;
    tg_size_t count;
    tg_size_t i;
    tg_init();
    {
        struct tg_fake_fd *fake=tg_find_fake_fd(fd);
        if (!fake) return real_write_fn ? real_write_fn(fd,buffer,length) : -1;
        if (fake->joydev) return -1;
    }
    if (!buffer || length%sizeof(*events)!=0) return -1;
    count=length/sizeof(*events);
    for (i=0;i<count;i++) {
        if (events[i].type!=TG_EV_FF) continue;
        if (events[i].code==TG_FF_GAIN) {
            int gain=events[i].value;
            if (gain<0) gain=0; else if (gain>0xffff) gain=0xffff;
            ff_gain=(tg_u16)gain;
        } else {
            int slot=tg_find_effect(fd,events[i].code);
            if (slot<0) return -1;
            if (events[i].value<=0) {
                if (tg_send_control(fd,TG_CONTROL_STOP_RUMBLE,0,0,0)<0) return -1;
            } else {
                tg_u16 low=(tg_u16)(((tg_u32)effect_slots[slot].low_frequency*ff_gain)/0xffffU);
                tg_u16 high=(tg_u16)(((tg_u32)effect_slots[slot].high_frequency*ff_gain)/0xffffU);
                tg_u32 duration=effect_slots[slot].duration_ms;
                if (!duration) duration=2500;
                if (tg_send_control(fd,TG_CONTROL_RUMBLE,low,high,duration)<0) return -1;
            }
        }
    }
    return (tg_ssize_t)length;
}

static int tg_fake_stat(void *output, int joydev) {
    struct tg_stat_layout *st=(struct tg_stat_layout*)output;
    if (!st) return -1;
    tg_memzero(st,sizeof(*st));
    st->st_dev=1;
    st->st_ino=joydev ? 199 : 99;
    st->st_mode=0020000|0660; /* S_IFCHR | rw-rw---- */
    st->st_nlink=1;
    st->st_rdev=(13U<<8)|99U;
    st->st_blksize=4096;
    return 0;
}

__attribute__((visibility("default"))) int stat(const char *path, void *output) {
    tg_init();
    if (tg_streq(path,TG_FAKE_DEVNODE)) return tg_fake_stat(output,0);
    if (tg_streq(path,TG_FAKE_HAPTIC_ALIAS_NODE)) return tg_fake_stat(output,0);
    if (tg_streq(path,TG_FAKE_JOYDEV_NODE)) return tg_fake_stat(output,1);
    return real_stat_fn ? real_stat_fn(path,output) : -1;
}

__attribute__((visibility("default"))) int stat64(const char *path, void *output) {
    tg_init();
    if (tg_streq(path,TG_FAKE_DEVNODE)) return tg_fake_stat(output,0);
    if (tg_streq(path,TG_FAKE_HAPTIC_ALIAS_NODE)) return tg_fake_stat(output,0);
    if (tg_streq(path,TG_FAKE_JOYDEV_NODE)) return tg_fake_stat(output,1);
    if (real_stat64_fn) return real_stat64_fn(path,output);
    return real_stat_fn ? real_stat_fn(path,output) : -1;
}

__attribute__((visibility("default"))) int close(int fd) {
    struct tg_fake_fd *fake;
    tg_init();
    fake=tg_find_fake_fd(fd);
    if (fake && !fake->joydev) {
        (void)tg_send_control(fd,TG_CONTROL_STOP_RUMBLE,0,0,0);
        tg_clear_effects(fd);
    }
    tg_del_fake_fd(fd);
    if (real_close_fn) return real_close_fn(fd);
    return -1;
}
