#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
int main(int argc,char **argv){
    int monitor=argc>1 && !strcmp(argv[1],"--monitor");
    int seconds=argc>2?atoi(argv[2]):10;
    struct stat st;
    if(stat("/dev/input/event99",&st)<0 || !S_ISCHR(st.st_mode)){
        fprintf(stderr,"stat failed: %s\n",strerror(errno));return 1;
    }
    int fd=open("/dev/input/event99",O_RDONLY|O_NONBLOCK);
    if(fd<0){fprintf(stderr,"open failed: %s\n",strerror(errno));return 1;}
    struct input_id id={0}; char name[128]={0};
    if(ioctl(fd,EVIOCGID,&id)<0){perror("EVIOCGID");return 2;}
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0){perror("EVIOCGNAME");return 3;}
    printf("name=%s vendor=%04x product=%04x fd=%d\n",name,id.vendor,id.product,fd);
    struct pollfd p={.fd=fd,.events=POLLIN};
    time_t deadline=time(NULL)+(monitor?seconds:2);
    int received=0;
    do {
        int timeout=monitor?250:1500;
        int pr=poll(&p,1,timeout);
        if(pr<0 && errno==EINTR) continue;
        if(pr<0){fprintf(stderr,"poll=%d errno=%s\n",pr,strerror(errno));return 4;}
        if(pr==0){if(!monitor){fprintf(stderr,"poll timeout\n");return 4;}continue;}
        struct input_event ev[64]; ssize_t n=read(fd,ev,sizeof(ev));
        if(n<0 && (errno==EAGAIN||errno==EINTR)) continue;
        if(n<(ssize_t)sizeof(struct input_event)){fprintf(stderr,"read=%zd\n",n);return 5;}
        int count=(int)(n/sizeof(struct input_event));
        received+=count;
        for(int i=0;i<count;i++) printf("event type=%u code=%u value=%d\n",ev[i].type,ev[i].code,ev[i].value);
        fflush(stdout);
        if(!monitor) break;
    } while(time(NULL)<deadline);
    if(monitor) printf("received=%d events\n",received);
    close(fd);return 0;
}
