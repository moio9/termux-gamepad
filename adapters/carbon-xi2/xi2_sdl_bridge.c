/* Experimental XI2 -> SDL3 multi-controller adapter for the Carbon runner. */
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/XInput2.h>
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lorie_controller_proto.h"

#define MAX_PADS 4
struct pad { int xi; SDL_JoystickID id; SDL_Joystick *joy; };
static struct pad pads[MAX_PADS];
static atomic_bool running;
static pthread_t thread;
static int started;
static Display *display;
static int controller_opcode, xi_opcode;

static int query_pad(int id, xLorieControllerQueryCapabilitiesReply *reply) {
    Display *dpy = display;
    LockDisplay(dpy);
    xLorieControllerQueryCapabilitiesReq *req = (void *)_XGetRequest(dpy, (CARD8)controller_opcode, sizeof(*req));
    if (!req) { UnlockDisplay(dpy); return 0; }
    req->lorieControllerReqType = X_LorieControllerQueryCapabilities;
    req->deviceId = (CARD16)id;
    req->pad0 = 0;
    Status status = _XReply(dpy, (xReply *)reply, 0, xFalse);
    UnlockDisplay(dpy);
    SyncHandle();
    return status && reply->present && reply->deviceId == id;
}
static void remove_pad(struct pad *p) {
    if (!p->id) return;
    SDL_CloseJoystick(p->joy);
    SDL_DetachVirtualJoystick(p->id);
    memset(p, 0, sizeof(*p));
}
static void refresh(void) {
    int count = 0; XIDeviceInfo *devices = XIQueryDevice(display, XIAllDevices, &count);
    if (!devices) return;
    int seen[MAX_PADS] = {0};
    for (int i=0; i<count; ++i) {
        if (devices[i].use != XIFloatingSlave && devices[i].use != XISlavePointer) continue;
        xLorieControllerQueryCapabilitiesReply info;
        if (!query_pad(devices[i].deviceid, &info)) continue;
        int slot=-1;
        for (int j=0; j<MAX_PADS; ++j) if (pads[j].id && pads[j].xi == devices[i].deviceid) { slot=j; break; }
        if (slot<0) for (int j=0; j<MAX_PADS; ++j) if (!pads[j].id) { slot=j; break; }
        if (slot<0) continue;
        seen[slot]=1;
        if (pads[slot].id) continue;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id=info.vendorId; desc.product_id=info.productId;
        desc.naxes=6; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask=0x3f; desc.button_mask=(1u << 16)-1;
        desc.name=devices[i].name;
        SDL_JoystickID joyid=SDL_AttachVirtualJoystick(&desc);
        if (!joyid) { fprintf(stderr,"XI2 input: attach failed: %s\n",SDL_GetError()); continue; }
        SDL_Joystick *joy=SDL_OpenJoystick(joyid);
        if (!joy) { SDL_DetachVirtualJoystick(joyid); continue; }
        pads[slot]=(struct pad){devices[i].deviceid,joyid,joy};
        SDL_SetJoystickVirtualAxis(joy,4,-32768); SDL_SetJoystickVirtualAxis(joy,5,-32768);
        fprintf(stderr,"XI2 input: player %d connected (XI2 %d SDL %u)\n",slot+1,devices[i].deviceid,joyid);
    }
    for (int i=0; i<MAX_PADS; ++i) if (pads[i].id && !seen[i]) remove_pad(&pads[i]);
    XIFreeDeviceInfo(devices);
}
static int button_index(int detail) {
    static const int map[16]={0,1,2,3,9,10,4,6,7,8,11,12,13,14,5,15};
    return detail >= 1 && detail <= 16 ? map[detail-1] : -1;
}
static void process_raw(XIRawEvent *e) {
    struct pad *p=NULL;
    for(int i=0;i<MAX_PADS;i++) if(pads[i].id && (pads[i].xi==e->sourceid || pads[i].xi==e->deviceid)) {p=&pads[i];break;}
    if(!p) return;
    if(e->evtype==XI_RawButtonPress || e->evtype==XI_RawButtonRelease) {
        int button=button_index(e->detail);
        if(button>=0) SDL_SetJoystickVirtualButton(p->joy,button,e->evtype==XI_RawButtonPress);
    } else if(e->evtype==XI_RawMotion) {
        const double *value=e->valuators.values;
        for(int axis=0; axis<e->valuators.mask_len*8; ++axis) if(XIMaskIsSet(e->valuators.mask,axis)) {
            double v=*value++;
            if(axis>=6) continue;
            if(axis>=4) v=v*2.0-32768.0;
            // Suppress small stick drift so a released stick returns to center.
            else if(v > -4096 && v < 4096) v=0;
            if(v>32767) v=32767; if(v< -32768) v= -32768;
            SDL_SetJoystickVirtualAxis(p->joy,axis,(Sint16)v);
        }
    }
}
static void *run(void *unused) {
    (void)unused;
    while (atomic_load(&running) && !SDL_WasInit(SDL_INIT_GAMEPAD)) usleep(10000);
    if (!atomic_load(&running)) return NULL;
    display=XOpenDisplay(NULL);
    if(!display) { fprintf(stderr,"XI2 input: cannot open X display\n"); return NULL; }
    // XI 2.1+ delivers raw events even while another X client has a grab.
    // Games commonly grab input, so XI 2.0 can leave releases unseen.
    int event,error,major=2,minor=2;
    if(!XQueryExtension(display,"XInputExtension",&xi_opcode,&event,&error) || XIQueryVersion(display,&major,&minor)!=Success ||
       !XQueryExtension(display,LORIE_CONTROLLER_NAME,&controller_opcode,&event,&error)) {
        fprintf(stderr,"XI2 input: required X extensions unavailable\n"); XCloseDisplay(display); return NULL;
    }
    unsigned char bits[XIMaskLen(XI_LASTEVENT)]={0};
    XISetMask(bits,XI_HierarchyChanged); XISetMask(bits,XI_RawMotion);
    XISetMask(bits,XI_RawButtonPress); XISetMask(bits,XI_RawButtonRelease);
    XIEventMask mask={XIAllDevices,sizeof(bits),bits};
    XISelectEvents(display,DefaultRootWindow(display),&mask,1); XFlush(display); refresh();
    while(atomic_load(&running)) {
        while(XPending(display)) {
            XEvent ev; XNextEvent(display,&ev); XGenericEventCookie *c=&ev.xcookie;
            if(c->type!=GenericEvent || c->extension!=xi_opcode || !XGetEventData(display,c)) continue;
            if(c->evtype==XI_HierarchyChanged) refresh();
            else if(c->evtype==XI_RawMotion || c->evtype==XI_RawButtonPress || c->evtype==XI_RawButtonRelease) process_raw(c->data);
            XFreeEventData(display,c);
        }
        usleep(5000);
    }
    for(int i=0;i<MAX_PADS;i++) remove_pad(&pads[i]);
    XCloseDisplay(display); display=NULL; return NULL;
}
int carbon_xi2_start(void) {
    if(started) return 0;
    atomic_store(&running,true);
    if(pthread_create(&thread,NULL,run,NULL)) { atomic_store(&running,false); return -1; }
    started=1; return 0;
}
void carbon_xi2_stop(void) {
    if(!started) return;
    atomic_store(&running,false); pthread_join(thread,NULL); started=0;
}
