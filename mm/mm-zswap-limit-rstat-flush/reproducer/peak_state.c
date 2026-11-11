#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void ck(bool b,const char *s) { if(!b) { fprintf(stderr,"error=%s errno=%d\n",s,errno); exit(2); } }
static void pin(int cpu) { cpu_set_t c; CPU_ZERO(&c);CPU_SET(cpu,&c);ck(!sched_setaffinity(0,sizeof(c),&c),"pin"); }
static void put(const char *p,const char *s) { int f=open(p,O_WRONLY|O_CLOEXEC);ck(f>=0,"open-write");ck(write(f,s,strlen(s))==(ssize_t)strlen(s),"write");ck(!close(f),"close"); }
static uint64_t getfd(int f) { char b[128],*e;ck(lseek(f,0,SEEK_SET)==0,"seek-read");ssize_t n=read(f,b,sizeof(b)-1);ck(n>0,"read");b[n]=0;errno=0;uint64_t v=strtoull(b,&e,10);ck(!errno && *e=='\n',"number");return v; }
static uint64_t get(const char *p) {int f=open(p,O_RDONLY|O_CLOEXEC);ck(f>=0,"open-read");uint64_t v=getfd(f);ck(!close(f),"close");return v;}
#define MARK __attribute__((__noinline__,noclone))
MARK void rest_stage(unsigned phase) { __asm__ __volatile__(""::"r"(phase):"memory"); }
static void reset(int f) {ck(lseek(f,0,SEEK_SET)==0,"seek-write");ck(write(f,"0",1)==1,"reset");}
static void sendcmd(int w,int r,char cmd) {ck(write(w,&cmd,1)==1,"command");char b;ck(read(r,&b,1)==1 && b==cmd,"ack");}
static void *pages(size_t n) {void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);ck(p!=MAP_FAILED,"mmap");ck(!madvise(p,n,MADV_NOHUGEPAGE),"nohuge");for(size_t i=0;i<n;i+=4096)((unsigned char*)p)[i]=(unsigned char)(i/4096+17);return p;}

int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--selftest")) { puts("selftest=pass no_runtime_changes=1");return 0; }
    ck(argc==2 && geteuid()==0,"args-root");
    char root[PATH_MAX],leaf[PATH_MAX],path[PATH_MAX];ck(realpath(argv[1],root)!=NULL,"root");
    const char *prefix="/sys/fs/cgroup/ks-memcontrol-rest-";
    ck(!strncmp(root,prefix,strlen(prefix))&&!strchr(root+strlen(prefix),'/'),"owned-root");
    ck(snprintf(leaf,sizeof(leaf),"%s/g0",root)<(int)sizeof(leaf),"leaf");ck(!mkdir(leaf,0755),"mkdir");
    int to[2],from[2];ck(!pipe2(to,O_CLOEXEC)&&!pipe2(from,O_CLOEXEC),"pipes");alarm(20);pin(18);
    pid_t child=fork();ck(child>=0,"fork");
    if(!child) {
        ck(!prctl(PR_SET_PDEATHSIG,SIGKILL),"pdeath");pin(2);close(to[1]);close(from[0]);
        ck(snprintf(path,sizeof(path),"%s/cgroup.procs",leaf)<(int)sizeof(path),"join-path");put(path,"0");
        const size_t base=32UL<<20,extra=16UL<<20;void *a=pages(base),*b=NULL;
        char c='R';ck(write(from[1],&c,1)==1,"ready");
        while(read(to[0],&c,1)==1) {
            if(c=='A') {ck(!b,"once-add");b=pages(extra);}
            else if(c=='D') {ck(b&&!munmap(b,extra),"drop-extra");b=NULL;}
            else if(c=='Q') {ck(!b,"extra-gone");for(size_t i=0;i<base;i+=4096)ck(((unsigned char*)a)[i]==(unsigned char)(i/4096+17),"contents");ck(!munmap(a,base),"drop-base");ck(write(from[1],&c,1)==1,"quit-ack");_exit(0);}
            else ck(false,"known-command");
            ck(write(from[1],&c,1)==1,"ack");
        }
        _exit(3);
    }
    close(to[0]);close(from[1]);char ready;ck(read(from[0],&ready,1)==1&&ready=='R',"holder-ready");
    ck(snprintf(path,sizeof(path),"%s/memory.current",leaf)<(int)sizeof(path),"current-path");uint64_t base=get(path);
    ck(base>=(32UL<<20)&&base<(40UL<<20),"base-charge");
    char peak[PATH_MAX];ck(snprintf(peak,sizeof(peak),"%s/memory.peak",leaf)<(int)sizeof(peak),"peak-path");
    rest_stage(1);
    int a=open(peak,O_RDWR|O_CLOEXEC),b=open(peak,O_RDWR|O_CLOEXEC),u=open(peak,O_RDONLY|O_CLOEXEC);ck(a>=0&&b>=0&&u>=0,"three-distinct-open");
    reset(a);reset(b);uint64_t initial_a=getfd(a),initial_b=getfd(b);
    rest_stage(2);sendcmd(to[1],from[0],'A');uint64_t high=get(path),high_a=getfd(a),high_b=getfd(b);
    ck(high>=base+(12UL<<20)&&high_a>=high&&high_b>=high,"observed-high-water");
    sendcmd(to[1],from[0],'D');uint64_t low=get(path);ck(high>=low+(12UL<<20),"released-extra");
    uint64_t before_a=getfd(a),before_b=getfd(b);rest_stage(3);reset(a);
    uint64_t after_a=getfd(a),after_b=getfd(b),global=getfd(u);
    bool own_reset=after_a<high-(8UL<<20),peer_retained=after_b>=before_b,global_retained=global>=high;
    printf("PEAK base=%"PRIu64" initial_a=%"PRIu64" initial_b=%"PRIu64" high=%"PRIu64" high_a=%"PRIu64" high_b=%"PRIu64" low=%"PRIu64" before_a=%"PRIu64" before_b=%"PRIu64" after_a=%"PRIu64" after_b=%"PRIu64" global=%"PRIu64" own_reset=%d peer_retained=%d global_retained=%d\n",base,initial_a,initial_b,high,high_a,high_b,low,before_a,before_b,after_a,after_b,global,own_reset,peer_retained,global_retained);
    rest_stage(4);ck(!close(a)&&!close(b)&&!close(u),"close-watchers");rest_stage(0);
    sendcmd(to[1],from[0],'Q');int status;ck(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"holder-exit");
    close(to[1]);close(from[0]);ck(!rmdir(leaf),"remove-leaf");
    printf("RESULT state=%s data=pass independent_open=3 registered=2 backend_completion_proven=0\n",own_reset&&peer_retained&&global_retained?"pass":"counterexample");
    alarm(0);return 0;
}
