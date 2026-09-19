#ifndef LORIE_CONTROLLER_PROTO_H
#define LORIE_CONTROLLER_PROTO_H

#include <X11/Xmd.h>

#define LORIE_CONTROLLER_NAME "LORIE-CONTROLLER"
#define LORIE_CONTROLLER_MAJOR_VERSION 1
#define LORIE_CONTROLLER_MINOR_VERSION 2

#define X_LorieControllerQueryVersion      0
#define X_LorieControllerQueryCapabilities 1
#define X_LorieControllerRumble            2
#define X_LorieControllerStopRumble        3

#define LORIE_CONTROLLER_CAP_RUMBLE         (1U << 0)
#define LORIE_CONTROLLER_CAP_TRIGGER_RUMBLE (1U << 1)
#define LORIE_CONTROLLER_EFFECT_MAIN 0

#define LORIE_CONTROLLER_INPUT_NONE    0
#define LORIE_CONTROLLER_INPUT_XINPUT  1
#define LORIE_CONTROLLER_INPUT_DINPUT  2
#define LORIE_CONTROLLER_INPUT_XDINPUT 3

typedef struct {
    CARD8 reqType;
    CARD8 lorieControllerReqType;
    CARD16 length;
    CARD16 majorVersion;
    CARD16 minorVersion;
} xLorieControllerQueryVersionReq;

typedef struct {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD16 majorVersion;
    CARD16 minorVersion;
    CARD32 pad1[5];
} xLorieControllerQueryVersionReply;

typedef struct {
    CARD8 reqType;
    CARD8 lorieControllerReqType;
    CARD16 length;
    CARD16 deviceId;
    CARD16 pad0;
} xLorieControllerQueryCapabilitiesReq;

typedef struct {
    CARD8 type;
    CARD8 pad0;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD16 deviceId;
    CARD16 capabilities;
    CARD8 present;
    CARD8 numAxes;
    CARD8 numButtons;
    CARD8 numHats;
    CARD32 mapping;
    CARD32 vendorId;
    CARD32 productId;
    CARD8 inputMode;
    CARD8 pad1[3];
} xLorieControllerQueryCapabilitiesReply;

typedef struct {
    CARD8 reqType;
    CARD8 lorieControllerReqType;
    CARD16 length;
    CARD16 deviceId;
    CARD8 effect;
    CARD8 pad0;
    CARD16 lowFrequency;
    CARD16 highFrequency;
    CARD32 durationMs;
} xLorieControllerRumbleReq;

typedef struct {
    CARD8 reqType;
    CARD8 lorieControllerReqType;
    CARD16 length;
    CARD16 deviceId;
    CARD16 pad0;
} xLorieControllerStopRumbleReq;

#endif
