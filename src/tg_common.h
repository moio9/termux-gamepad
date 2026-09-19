#ifndef TG_COMMON_H
#define TG_COMMON_H

typedef unsigned char      tg_u8;
typedef unsigned short     tg_u16;
typedef unsigned int       tg_u32;
typedef signed int         tg_s32;
typedef signed short       tg_s16;
typedef unsigned long      tg_ulong;
typedef signed long        tg_long;
typedef unsigned long      tg_size_t;
typedef signed long        tg_ssize_t;

struct tg_timeval { tg_long tv_sec; tg_long tv_usec; };
struct tg_input_event {
    struct tg_timeval time;
    tg_u16 type;
    tg_u16 code;
    tg_s32 value;
};
struct tg_input_id {
    tg_u16 bustype;
    tg_u16 vendor;
    tg_u16 product;
    tg_u16 version;
};
struct tg_input_absinfo {
    tg_s32 value;
    tg_s32 minimum;
    tg_s32 maximum;
    tg_s32 fuzz;
    tg_s32 flat;
    tg_s32 resolution;
};

struct tg_js_event {
    tg_u32 time;
    tg_s16 value;
    tg_u8 type;
    tg_u8 number;
};

typedef char tg_js_event_must_be_8_bytes[
    sizeof(struct tg_js_event) == 8 ? 1 : -1];

#define TG_EV_SYN 0x00
#define TG_EV_KEY 0x01
#define TG_EV_REL 0x02
#define TG_EV_ABS 0x03
#define TG_EV_FF  0x15
#define TG_SYN_REPORT 0

#define TG_ABS_X      0x00
#define TG_ABS_Y      0x01
#define TG_ABS_Z      0x02
#define TG_ABS_RX     0x03
#define TG_ABS_RY     0x04
#define TG_ABS_RZ     0x05
#define TG_ABS_HAT0X  0x10
#define TG_ABS_HAT0Y  0x11
#define TG_ABS_MAX    0x3f
#define TG_ABS_CNT    (TG_ABS_MAX + 1)

#define TG_BTN_SOUTH   0x130
#define TG_BTN_EAST    0x131
#define TG_BTN_NORTH   0x133
#define TG_BTN_WEST    0x134
#define TG_BTN_TL      0x136
#define TG_BTN_TR      0x137
#define TG_BTN_TL2     0x138
#define TG_BTN_TR2     0x139
#define TG_BTN_SELECT  0x13a
#define TG_BTN_START   0x13b
#define TG_BTN_MODE    0x13c
#define TG_BTN_THUMBL  0x13d
#define TG_BTN_THUMBR  0x13e
#define TG_BTN_DPAD_UP    0x220
#define TG_BTN_DPAD_DOWN  0x221
#define TG_BTN_DPAD_LEFT  0x222
#define TG_BTN_DPAD_RIGHT 0x223
#define TG_BTN_TRIGGER  0x120
#define TG_BTN_THUMB    0x121
#define TG_BTN_THUMB2   0x122
#define TG_BTN_TOP      0x123
#define TG_BTN_TOP2     0x124
#define TG_BTN_PINKIE   0x125
#define TG_BTN_BASE     0x126
#define TG_BTN_BASE2    0x127
#define TG_BTN_BASE3    0x128
#define TG_BTN_BASE4    0x129
#define TG_BTN_BASE5    0x12a
#define TG_BTN_BASE6    0x12b
#define TG_KEY_MAX     0x2ff
#define TG_KEY_CNT     (TG_KEY_MAX + 1)

#define TG_FF_RUMBLE 0x50
#define TG_FF_PERIODIC 0x51
#define TG_FF_CONSTANT 0x52
#define TG_FF_SPRING 0x53
#define TG_FF_FRICTION 0x54
#define TG_FF_DAMPER 0x55
#define TG_FF_INERTIA 0x56
#define TG_FF_RAMP 0x57
#define TG_FF_SQUARE 0x58
#define TG_FF_TRIANGLE 0x59
#define TG_FF_SINE 0x5a
#define TG_FF_SAW_UP 0x5b
#define TG_FF_SAW_DOWN 0x5c
#define TG_FF_GAIN   0x60

struct tg_ff_envelope {
    tg_u16 attack_length;
    tg_u16 attack_level;
    tg_u16 fade_length;
    tg_u16 fade_level;
};

struct tg_ff_condition_effect {
    tg_u16 right_saturation;
    tg_u16 left_saturation;
    tg_s16 right_coeff;
    tg_s16 left_coeff;
    tg_u16 deadband;
    tg_s16 center;
};

struct tg_ff_effect {
    tg_u16 type;
    tg_s16 id;
    tg_u16 direction;
    tg_u16 trigger_button;
    tg_u16 trigger_interval;
    tg_u16 replay_length;
    tg_u16 replay_delay;
    tg_u16 alignment_padding;
    union {
        struct {
            tg_u16 strong_magnitude;
            tg_u16 weak_magnitude;
        } rumble;
        struct {
            tg_s16 level;
            struct tg_ff_envelope envelope;
        } constant;
        struct {
            tg_u16 waveform;
            tg_u16 period;
            tg_s16 magnitude;
            tg_s16 offset;
            tg_u16 phase;
            struct tg_ff_envelope envelope;
            tg_u16 custom_padding;
            tg_u32 custom_len;
            tg_s16 *custom_data;
        } periodic;
        struct tg_ff_condition_effect condition[2];
        struct {
            tg_s16 start_level;
            tg_s16 end_level;
            struct tg_ff_envelope envelope;
        } ramp;
        tg_u8 storage[32];
    } u;
};

typedef char tg_ff_effect_must_be_48_bytes[
    sizeof(struct tg_ff_effect) == 48 ? 1 : -1];

#define TG_BUS_USB     0x03

#define TG_JS_EVENT_BUTTON 0x01
#define TG_JS_EVENT_AXIS   0x02
#define TG_JS_EVENT_INIT   0x80

/* Linux generic ioctl encoding (asm-generic/ioctl.h). */
#define TG_IOC_NRBITS   8
#define TG_IOC_TYPEBITS 8
#define TG_IOC_SIZEBITS 14
#define TG_IOC_DIRBITS  2
#define TG_IOC_NRSHIFT   0
#define TG_IOC_TYPESHIFT (TG_IOC_NRSHIFT + TG_IOC_NRBITS)
#define TG_IOC_SIZESHIFT (TG_IOC_TYPESHIFT + TG_IOC_TYPEBITS)
#define TG_IOC_DIRSHIFT  (TG_IOC_SIZESHIFT + TG_IOC_SIZEBITS)
#define TG_IOC_NONE  0U
#define TG_IOC_WRITE 1U
#define TG_IOC_READ  2U
#define TG_IOC(dir,type,nr,size) \
    (((dir)  << TG_IOC_DIRSHIFT) | ((type) << TG_IOC_TYPESHIFT) | \
     ((nr)   << TG_IOC_NRSHIFT)  | ((size) << TG_IOC_SIZESHIFT))
#define TG_IOR(type,nr,sz) TG_IOC(TG_IOC_READ,(type),(nr),sizeof(sz))
#define TG_EVIOCGVERSION       TG_IOR('E',0x01,int)
#define TG_EVIOCGID            TG_IOR('E',0x02,struct tg_input_id)
#define TG_EVIOCGNAME(len)      TG_IOC(TG_IOC_READ,'E',0x06,(len))
#define TG_EVIOCGBIT(ev,len)    TG_IOC(TG_IOC_READ,'E',0x20+(ev),(len))
#define TG_EVIOCGABS(abs)       TG_IOR('E',0x40+(abs),struct tg_input_absinfo)
#define TG_JSIOCGVERSION        TG_IOR('j',0x01,tg_u32)
#define TG_JSIOCGAXES           TG_IOR('j',0x11,tg_u8)
#define TG_JSIOCGBUTTONS        TG_IOR('j',0x12,tg_u8)
#define TG_JSIOCGNAME(len)      TG_IOC(TG_IOC_READ,'j',0x13,(len))

#define TG_O_CREAT  0100
#define TG_O_TMPFILE 020000000
#define TG_O_NONBLOCK 00004000
#define TG_AF_UNIX 1
#define TG_SOCK_STREAM 1
#define TG_SOCK_NONBLOCK 00004000
#define TG_SOCK_CLOEXEC 02000000
#define TG_MSG_NOSIGNAL 0x4000
#define TG_MSG_WAITALL  0x100
#define TG_ENODEV 19

#define TG_FAKE_DEVNODE "/dev/input/event99"
#define TG_FAKE_HAPTIC_ALIAS_NODE "/dev/input/event31"
#define TG_FAKE_JOYDEV_NODE "/dev/input/js99"
#define TG_FAKE_PARENT_SYSPATH "/sys/devices/virtual/termux-gamepad0/input/input99"
#define TG_FAKE_SYSPATH TG_FAKE_PARENT_SYSPATH "/event99"
#define TG_FAKE_JOYDEV_SYSPATH TG_FAKE_PARENT_SYSPATH "/js99"
#define TG_DEFAULT_SOCKET "/data/data/com.termux/files/usr/tmp/termux-gamepad-evdev.sock"

#define TG_XINPUT_VENDOR  0x045e
#define TG_XINPUT_PRODUCT 0x028e
#define TG_DINPUT_VENDOR  0x1209
#define TG_DINPUT_PRODUCT 0x0001
#define TG_XINPUT_NAME "Termux Virtual Xbox Controller"
#define TG_DINPUT_NAME "Termux Virtual DirectInput Controller"

static inline void tg_memzero(void *p, tg_size_t n) {
    tg_u8 *b = (tg_u8*)p;
    while (n--) *b++ = 0;
}
static inline tg_size_t tg_strlen(const char *s) {
    tg_size_t n=0; if (!s) return 0; while (s[n]) n++; return n;
}
static inline int tg_streq(const char *a, const char *b) {
    tg_size_t i=0; if (!a || !b) return 0;
    while (a[i] && b[i] && a[i]==b[i]) i++;
    return a[i]==0 && b[i]==0;
}
static inline void tg_copy(char *dst, const char *src, tg_size_t cap) {
    tg_size_t i=0; if (!cap) return;
    if (src) while (i+1<cap && src[i]) { dst[i]=src[i]; i++; }
    dst[i]=0;
}
static inline void tg_set_bit(tg_u8 *buf, tg_size_t len, unsigned bit) {
    unsigned byte = bit >> 3;
    unsigned off = bit & 7;
    if (byte < len) buf[byte] |= (tg_u8)(1u << off);
}

#endif
