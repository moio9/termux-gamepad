#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *(*fn0)(void);
typedef void *(*fn1p)(void*);
typedef int (*fn2is)(void*,const char*);
typedef int (*fn1i)(void*);
typedef void *(*fn1)(void*);
typedef void *(*fn2p)(void*,const char*);
typedef const char *(*fnstr1)(void*);
typedef const char *(*fnstr2)(void*,const char*);

int main(int argc,char**argv){
    const char *lib=argc>1?argv[1]:"./libudev.so.1";
    void *h=dlopen(lib,RTLD_NOW|RTLD_LOCAL);
    if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    fn0 udev_new=(fn0)dlsym(h,"udev_new");
    fn1 udev_enumerate_new=(fn1)dlsym(h,"udev_enumerate_new");
    fn2is match=(fn2is)dlsym(h,"udev_enumerate_add_match_subsystem");
    fn1i scan=(fn1i)dlsym(h,"udev_enumerate_scan_devices");
    fn1 getlist=(fn1)dlsym(h,"udev_enumerate_get_list_entry");
    fn1 getnext=(fn1)dlsym(h,"udev_list_entry_get_next");
    fnstr1 getname=(fnstr1)dlsym(h,"udev_list_entry_get_name");
    fn2p newdev=(fn2p)dlsym(h,"udev_device_new_from_syspath");
    fnstr1 devnode=(fnstr1)dlsym(h,"udev_device_get_devnode");
    fnstr2 prop=(fnstr2)dlsym(h,"udev_device_get_property_value");
    if(!udev_new||!udev_enumerate_new||!match||!scan||!getlist||!getnext||!getname||!newdev||!devnode||!prop){puts("missing symbol");return 2;}
    void*u=udev_new(); void*e=udev_enumerate_new(u); match(e,"input"); scan(e); void*le=getlist(e);
    int count=0;
    for(;le;le=getnext(le)) {
        const char*p=getname(le); void*d=newdev(u,p);
        printf("syspath=%s\ndevnode=%s\nID_INPUT_JOYSTICK=%s\n",
               p,devnode(d),prop(d,"ID_INPUT_JOYSTICK"));
        printf("name=%s vid=%s pid=%s\n", prop(d,"NAME"),
               prop(d,"ID_VENDOR_ID"), prop(d,"ID_MODEL_ID"));
        count++;
    }
    /* Enumerate only evdev so clients do not see a duplicate controller. */
    return count==1?0:3;
}
