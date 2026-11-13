/* Exercise the actual bridge event mapper with two SDL virtual devices. */
#include "xi2_sdl_bridge.c"
#include <assert.h>
int main(void) {
    assert(SDL_Init(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=6; desc.nbuttons=16; desc.axis_mask=0x3f; desc.button_mask=0xffff;
    desc.name="Carbon mapping test";
    for(int i=0;i<2;i++) {
        pads[i].id=SDL_AttachVirtualJoystick(&desc); assert(pads[i].id);
        pads[i].joy=SDL_OpenJoystick(pads[i].id); assert(pads[i].joy);
        pads[i].xi=100+i;
    }
    XIRawEvent e={0}; e.deviceid=100; e.sourceid=100;
    e.evtype=XI_RawButtonPress; e.detail=1; process_raw(&e);
    SDL_UpdateJoysticks();
    assert(SDL_GetJoystickButton(pads[0].joy,0));
    assert(!SDL_GetJoystickButton(pads[1].joy,0));
    e.sourceid=101; e.deviceid=101; e.detail=8; process_raw(&e);
    SDL_UpdateJoysticks();
    assert(SDL_GetJoystickButton(pads[0].joy,0));
    assert(SDL_GetJoystickButton(pads[1].joy,6));
    assert(!SDL_GetJoystickButton(pads[0].joy,6));
    e.sourceid=100; e.deviceid=100; e.detail=1;
    e.evtype=XI_RawButtonRelease; process_raw(&e); SDL_UpdateJoysticks();
    assert(!SDL_GetJoystickButton(pads[0].joy,0));
    assert(SDL_GetJoystickButton(pads[1].joy,6));
    e.sourceid=101; e.deviceid=101;
    unsigned char mask=1u<<4; double axis=32767;
    e.evtype=XI_RawMotion; e.valuators.mask=&mask; e.valuators.mask_len=1; e.valuators.values=&axis;
    process_raw(&e); SDL_UpdateJoysticks();
    assert(SDL_GetJoystickAxis(pads[1].joy,4)==32766);
    mask=1u; axis=1024;
    e.sourceid=100; e.deviceid=100; process_raw(&e); SDL_UpdateJoysticks();
    assert(SDL_GetJoystickAxis(pads[0].joy,0)==0);
    axis=-12000; process_raw(&e); SDL_UpdateJoysticks();
    assert(SDL_GetJoystickAxis(pads[0].joy,0)==-12000);
    assert(SDL_GetJoystickAxis(pads[1].joy,0)==0);
    for(int i=0;i<2;i++) remove_pad(&pads[i]);
    SDL_Quit(); return 0;
}
