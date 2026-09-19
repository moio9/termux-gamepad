#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static void send_ev(int c,unsigned short type,unsigned short code,int value){
    struct input_event e; memset(&e,0,sizeof(e)); clock_gettime(CLOCK_REALTIME,(struct timespec*)&(struct timespec){0});
    struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); e.time.tv_sec=ts.tv_sec; e.time.tv_usec=ts.tv_nsec/1000;
    e.type=type;e.code=code;e.value=value; if(write(c,&e,sizeof(e))!=(ssize_t)sizeof(e)) perror("write");
}
int main(int argc,char**argv){
    const char *path=argc>1?argv[1]:"/tmp/termux-gamepad-evdev.sock";
    int s=socket(AF_UNIX,SOCK_STREAM,0); if(s<0){perror("socket");return 1;}
    struct sockaddr_un un; memset(&un,0,sizeof(un)); un.sun_family=AF_UNIX; strncpy(un.sun_path,path,sizeof(un.sun_path)-1); unlink(path);
    if(bind(s,(struct sockaddr*)&un,sizeof(un))<0){perror("bind");return 1;} if(listen(s,4)<0){perror("listen");return 1;}
    printf("listening %s\n",path); fflush(stdout); int c=accept(s,0,0); if(c<0){perror("accept");return 1;} puts("client connected");
    /* One A press/release after connection, useful for poll/read tests. */
    usleep(250000); send_ev(c,EV_KEY,BTN_SOUTH,1); send_ev(c,EV_SYN,SYN_REPORT,0);
    usleep(150000); send_ev(c,EV_KEY,BTN_SOUTH,0); send_ev(c,EV_SYN,SYN_REPORT,0);
    sleep(2); close(c);close(s);unlink(path);return 0;
}
