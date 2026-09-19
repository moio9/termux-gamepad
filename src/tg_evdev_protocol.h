#ifndef TG_EVDEV_PROTOCOL_H
#define TG_EVDEV_PROTOCOL_H

#include "tg_common.h"

#define TG_CONTROL_MAGIC 0x54474546U /* TGEF */
#define TG_CONTROL_VERSION 3

#define TG_CONTROL_RUMBLE 1
#define TG_CONTROL_STOP_RUMBLE 2
#define TG_CONTROL_QUERY_DESCRIPTOR 3
#define TG_CONTROL_MONITOR 4

#define TG_DEVICE_MAGIC 0x54474456U /* TGDV */
#define TG_DEVICE_CAP_RUMBLE (1U << 0)

#define TG_INPUT_MODE_NONE    0
#define TG_INPUT_MODE_XINPUT  1
#define TG_INPUT_MODE_DINPUT  2
#define TG_INPUT_MODE_XDINPUT 3

#define TG_HOTPLUG_MAGIC 0x54474850U /* TGHP */
#define TG_HOTPLUG_ADD 1
#define TG_HOTPLUG_REMOVE 2

struct tg_device_descriptor {
    tg_u32 magic;
    tg_u16 version;
    tg_u8 present;
    tg_u8 capabilities;
    tg_u8 input_mode;
    tg_u8 pad0[3];
    tg_u32 vendor_id;
    tg_u32 product_id;
    tg_s32 device_id;
    char name[64];
};

struct tg_hotplug_message {
    tg_u32 magic;
    tg_u16 version;
    tg_u8 action;
    tg_u8 pad0;
    struct tg_device_descriptor descriptor;
};

struct tg_control_message {
    tg_u32 magic;
    tg_u16 version;
    tg_u16 command;
    tg_u16 low_frequency;
    tg_u16 high_frequency;
    tg_u32 duration_ms;
};

typedef char tg_control_message_must_be_16_bytes[
    sizeof(struct tg_control_message) == 16 ? 1 : -1];
typedef char tg_device_descriptor_must_be_88_bytes[
    sizeof(struct tg_device_descriptor) == 88 ? 1 : -1];
typedef char tg_hotplug_message_must_be_96_bytes[
    sizeof(struct tg_hotplug_message) == 96 ? 1 : -1];

#endif
