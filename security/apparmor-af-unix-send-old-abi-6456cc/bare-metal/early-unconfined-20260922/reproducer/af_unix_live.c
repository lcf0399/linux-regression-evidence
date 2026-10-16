// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <grp.h>
#include <inttypes.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { BATCH=32, SIZE=128 };
struct reply { unsigned received; int denied; uint64_t inode; char context[512]; };
struct request { unsigned count; int connect; };
static int sock=-1, commands[2], replies[2];
static pid_t child_pid=-1;
static uint64_t next_send, pending;

static void die(const char *what)
{
    perror(what);
    if (child_pid>0) { kill(child_pid,SIGKILL); (void)waitpid(child_pid,NULL,0); }
    exit(1);
}
static void need(int ok,const char *what) { if (!ok) { errno=EINVAL; die(what); } }
static uint64_t now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC_RAW,&t)) die("clock");
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static void exact(int fd,void *data,size_t len,int writing)
{
    size_t off=0;
    while (off<len) {
        ssize_t n=writing?write(fd,(char *)data+off,len-off):read(fd,(char *)data+off,len-off);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) die("control pipe");
        off+=(size_t)n;
    }
}
static void context(char *buf,size_t size)
{
    FILE *f=fopen("/proc/self/attr/current","r");
    if (!f || !fgets(buf,(int)size,f)) die("context");
    fclose(f); buf[strcspn(buf,"\n")]=0;
    need(!strchr(buf,'"')&&!strchr(buf,'\\'),"context JSON encoding");
}
static void enter(const char *name,unsigned cpu)
{
    cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(cpu,&mask);
    if (sched_setaffinity(0,sizeof(mask),&mask)) die("affinity");
    need(geteuid()==0 && sched_getscheduler(0)==SCHED_OTHER && getpriority(PRIO_PROCESS,0)==0,"setup identity");
    if (strcmp(name,"none")) {
        void *lib=dlopen("libapparmor.so.1",RTLD_NOW);
        need(lib!=NULL,"libapparmor");
        int (*change)(const char *)=dlsym(lib,"aa_change_profile");
        need(change!=NULL,"aa_change_profile");
        if (change(name)) die("change_profile");
        dlclose(lib);
    }
    /* Enter exactly one label before dropping privilege. Unprivileged
     * change_profile can be converted into stacking by the host policy. */
    if (setgroups(0,NULL) || setgid(1000) || setuid(1000)) die("drop privilege");
    if (prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)) die("no new privileges");
    uid_t real,effective,saved;
    if (getresuid(&real,&effective,&saved)) die("getresuid");
    need(real==1000 && effective==1000 && saved==1000 && getgid()==1000,"actor identity");
    char label[512]; context(label,sizeof(label));
    need(!strstr(label,"//&"),"unexpected stacked label");
    need(strcmp(name,"none") ? strstr(label,name)==label : !strcmp(label,"unconfined"),"initial label");
}
static socklen_t address(struct sockaddr_un *a,pid_t id,const char *side)
{
    memset(a,0,sizeof(*a)); a->sun_family=AF_UNIX;
    int n=snprintf(a->sun_path+1,sizeof(a->sun_path)-1,"ks_aa_live_%ld_%s",(long)id,side);
    need(n>0 && n<(int)sizeof(a->sun_path)-1,"address");
    return (socklen_t)(offsetof(struct sockaddr_un,sun_path)+1+n);
}
static int socket_setup(pid_t id,const char *side)
{
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0),size=1<<20;
    if (fd<0) die("socket");
    struct sockaddr_un a; socklen_t n=address(&a,id,side);
    if (bind(fd,(struct sockaddr *)&a,n)) die("bind");
    if (setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size)) ||
        setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size))) die("buffers");
    return fd;
}
static void connect_to(int fd,pid_t id,const char *side)
{
    struct sockaddr_un a; socklen_t n=address(&a,id,side);
    if (connect(fd,(struct sockaddr *)&a,n)) die("connect");
}
static uint64_t inode(int fd) { struct stat s; if (fstat(fd,&s)) die("fstat"); return s.st_ino; }
static void pattern(unsigned char *p,uint64_t seq)
{
    memset(p,0xa5,SIZE); memcpy(p,&seq,sizeof(seq));
}
static void receiver(pid_t parent,const char *label)
{
    close(commands[1]); close(replies[0]);
    if (prctl(PR_SET_PDEATHSIG,SIGKILL)) die("pdeathsig");
    need(getppid()==parent,"parent already exited"); alarm(120);
    enter(label,4); int fd=socket_setup(parent,"rx");
    struct reply r={.inode=inode(fd)}; context(r.context,sizeof(r.context));
    exact(replies[1],&r,sizeof(r),1);
    uint64_t sequence=0;
    for (;;) {
        struct request q; exact(commands[0],&q,sizeof(q),0);
        if (q.connect==-1) break;
        r.received=0; r.denied=0;
        if (q.connect) connect_to(fd,parent,"tx");
        for (unsigned i=0;i<q.count;i++) {
            unsigned char got[SIZE],expected[SIZE];
            ssize_t n=recv(fd,got,sizeof(got),MSG_DONTWAIT);
            if (n<0 && (errno==EACCES || errno==EPERM)) { r.denied=errno; break; }
            need(n==SIZE,"receive length/queue mismatch");
            pattern(expected,sequence++); need(!memcmp(got,expected,SIZE),"payload/order mismatch");
            r.received++;
        }
        r.inode=inode(fd); context(r.context,sizeof(r.context));
        exact(replies[1],&r,sizeof(r),1);
    }
    close(fd); _exit(0);
}
static struct reply drain(unsigned count,int connect)
{
    struct request q={.count=count,.connect=connect}; struct reply r;
    exact(commands[1],&q,sizeof(q),1); exact(replies[0],&r,sizeof(r),0);
    return r;
}
__attribute__((noinline,noclone)) void aa_window_begin(void) { __asm__ volatile("":::"memory"); }
__attribute__((noinline,noclone)) void aa_window_end(void) { __asm__ volatile("":::"memory"); }
static void print_contexts(const char *kind,struct reply r)
{
    char tx[512]; context(tx,sizeof(tx));
    printf("{\"kind\":\"%s\",\"pid\":%d,\"rx_pid\":%d,\"uid\":%u,\"tx_inode\":%"PRIu64",\"rx_inode\":%"PRIu64",\"tx_context\":\"%s\",\"rx_context\":\"%s\"}\n",
           kind,getpid(),child_pid,getuid(),inode(sock),r.inode,tx,r.context); fflush(stdout);
}
static void check_one(int report,uint64_t *allowed,uint64_t *denied)
{
    need(!pending,"flush pending packets before check");
    unsigned char data[SIZE]; pattern(data,next_send);
    ssize_t n=send(sock,data,SIZE,MSG_DONTWAIT|MSG_NOSIGNAL);
    int send_error=0; struct reply r={0};
    if (n==SIZE) { next_send++; pending++; (*allowed)++; r=drain(1,0); pending-=r.received; }
    else { need(n<0 && (errno==EACCES||errno==EPERM),"unexpected send failure"); send_error=errno; (*denied)++; }
    if (report) {
        if (send_error) r=drain(0,0);
        char tx[512]; context(tx,sizeof(tx));
        printf("{\"kind\":\"check\",\"send_errno\":%d,\"received\":%u,\"receive_errno\":%d,\"pending\":%"PRIu64",\"tx_inode\":%"PRIu64",\"rx_inode\":%"PRIu64",\"tx_context\":\"%s\"}\n",
               send_error,r.received,r.denied,pending,inode(sock),r.inode,tx); fflush(stdout);
    }
}
static void flush_queue(void)
{
    unsigned count=(unsigned)pending; struct reply r=drain(count,0);
    need(r.received<=count && (r.denied || r.received==count),"flush missing data"); pending-=r.received;
    printf("{\"kind\":\"flush\",\"received\":%u,\"receive_errno\":%d,\"pending\":%"PRIu64"}\n",r.received,r.denied,pending); fflush(stdout);
}
static void measure(unsigned messages,unsigned warmups,unsigned rounds)
{
    need(!pending && messages && messages%BATCH==0 && messages<=65536 && warmups<=3 && rounds<=15 && rounds,"measurement arguments");
    unsigned char data[BATCH][SIZE]; struct iovec iov[BATCH]; struct mmsghdr msg[BATCH];
    memset(msg,0,sizeof(msg));
    for (unsigned i=0;i<BATCH;i++) { iov[i]=(struct iovec){data[i],SIZE}; msg[i].msg_hdr.msg_iov=&iov[i]; msg[i].msg_hdr.msg_iovlen=1; }
    for (unsigned r=0;r<warmups+rounds;r++) {
        uint64_t elapsed=0; aa_window_begin();
        for (unsigned j=0;j<messages;j+=BATCH) {
            for (unsigned i=0;i<BATCH;i++) { pattern(data[i],next_send+i); msg[i].msg_len=0; }
            uint64_t start=now(); int sent=sendmmsg(sock,msg,BATCH,MSG_DONTWAIT|MSG_NOSIGNAL); elapsed+=now()-start;
            need(sent==BATCH,"short sendmmsg"); next_send+=BATCH;
            for (unsigned i=0;i<BATCH;i++) need(msg[i].msg_len==SIZE,"sent length");
            struct reply receipt=drain(BATCH,0); need(receipt.received==BATCH && !receipt.denied,"batch receive");
        }
        aa_window_end();
        printf("{\"kind\":\"measurement\",\"round\":%u,\"phase\":\"%s\",\"messages\":%u,\"batch\":32,\"payload\":128,\"elapsed_ns\":%"PRIu64",\"semantic_pass\":true}\n",r,r<warmups?"warmup":"measured",messages,elapsed); fflush(stdout);
    }
}
static void race(unsigned ms)
{
    need(ms==2000 && !pending,"race bound");
    uint64_t allow=0,deny=0,started=now(),until=started+(uint64_t)ms*1000000;
    printf("{\"kind\":\"race-start\",\"time_ns\":%"PRIu64"}\n",started); fflush(stdout);
    while (now()<until && allow+deny<20000000) check_one(0,&allow,&deny);
    need(!pending,"race receiver unexpectedly denied");
    printf("{\"kind\":\"race-end\",\"start_ns\":%"PRIu64",\"end_ns\":%"PRIu64",\"allowed\":%"PRIu64",\"denied\":%"PRIu64",\"semantic_pass\":true}\n",started,now(),allow,deny); fflush(stdout);
}
int main(int argc,char **argv)
{
    need(argc==4,"usage: af_unix_live TX_LABEL RX_LABEL interactive|probe|timing");
    alarm(120); if (pipe(commands)||pipe(replies)) die("pipe");
    pid_t parent=getpid(); child_pid=fork(); if(child_pid<0) die("fork");
    if (!child_pid) receiver(parent,argv[2]);
    close(commands[0]); close(replies[1]);
    struct reply ready; exact(replies[0],&ready,sizeof(ready),0);
    enter(argv[1],2); sock=socket_setup(parent,"tx"); connect_to(sock,parent,"rx");
    ready=drain(0,1); print_contexts("ready",ready);
    if (!strcmp(argv[3],"interactive")) {
        char line[128];
        while (fgets(line,sizeof(line),stdin)) {
            alarm(120);
            if (!strcmp(line,"check\n")) { uint64_t a=0,d=0; check_one(1,&a,&d); }
            else if (!strcmp(line,"flush\n")) flush_queue();
            else if (!strcmp(line,"race\n")) race(2000);
            else if (!strcmp(line,"quit\n")) break;
            else need(0,"unknown command");
        }
    } else {
        need(!strcmp(argv[3],"probe")||!strcmp(argv[3],"timing"),"mode");
        uint64_t a=0,d=0; check_one(0,&a,&d); need(a==1&&!d&&!pending,"initial semantic check");
        if (!strcmp(argv[3],"probe")) measure(1024,0,1); else measure(65536,3,15);
    }
    struct request q={.connect=-1}; exact(commands[1],&q,sizeof(q),1);
    close(sock); int status; need(waitpid(child_pid,&status,0)==child_pid && WIFEXITED(status)&&WEXITSTATUS(status)==0,"receiver exit");
    child_pid=-1; puts("{\"kind\":\"complete\",\"status\":\"pass\"}"); return 0;
}
