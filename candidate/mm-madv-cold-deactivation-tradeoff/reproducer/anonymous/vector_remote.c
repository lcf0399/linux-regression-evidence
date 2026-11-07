#include "timing.h"
#define measured_main earlier_pilot_main
#define KS_NO_MAIN_WRAPPER
#include "madvise_pilot.c"
#undef KS_NO_MAIN_WRAPPER
#undef measured_main
#include <grp.h>
#include <linux/capability.h>
#include <sys/wait.h>

/* Same uid, same requester capability in all successful comparisons.
 * The child drops every capability and owns pages allocated after fork. */
struct geometry { const char *name; unsigned vectors,remote,denied; };
static const struct geometry choices[]={
    {"vector-self-one",1,0,0},{"vector-self-many",64,0,0},
    {"vector-child-many",64,1,0},{"vector-child-denied",64,1,1}};
static const struct geometry *cfg;
static struct region ranges[64],control;
struct packet { long pid; unsigned vectors,cpu; uint64_t caps; size_t resident_pages,rss_kb; struct iovec iov[64]; };
static const size_t total_bytes=16UL*1024*1024;

__attribute__((__noinline__)) void vector_range(unsigned long lo,unsigned long hi,int add)
{__asm__ volatile("" : : "r"(lo),"r"(hi),"r"(add) : "memory");}
__attribute__((__noinline__)) void vector_begin(unsigned count,long target)
{ks_begin(30); __asm__ volatile("" : : "r"(count),"r"(target) : "memory");}
__attribute__((__noinline__)) void vector_end(void)
{ks_end(30); __asm__ volatile("" : : : "memory");}

static uint64_t capabilities(void)
{
    struct __user_cap_header_struct h={.version=_LINUX_CAPABILITY_VERSION_3};
    struct __user_cap_data_struct c[2]={{0}};
    check(syscall(SYS_capget,&h,c)==0,"read capabilities");
    check(c[0].effective==c[0].permitted && c[1].effective==c[1].permitted && !c[0].inheritable && !c[1].inheritable,"exact capability sets");
    return ((uint64_t)c[1].effective<<32)|c[0].effective;
}
static void set_caps(uint64_t mask)
{
    struct __user_cap_header_struct h={.version=_LINUX_CAPABILITY_VERSION_3};
    struct __user_cap_data_struct c[2]={{0}};
    c[0].effective=c[0].permitted=(uint32_t)mask;
    c[1].effective=c[1].permitted=(uint32_t)(mask>>32);
    check(syscall(SYS_capset,&h,c)==0 && capabilities()==mask,"only declared capability");
}
static void pin(unsigned cpu)
{
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(cpu,&mask);
    check(sched_setaffinity(0,sizeof(mask),&mask)==0 && sched_getcpu()==(int)cpu,"target CPU");
}
static void transfer(int fd,void *data,size_t bytes,int writing)
{
    unsigned char *p=data;
    while(bytes){ssize_t n=writing?write(fd,p,bytes):read(fd,p,bytes);
        if(n<0 && errno==EINTR)continue;
        check(n>0,"owned child pipe");p+=n;bytes-=(size_t)n;}
}
static void send_cmd(int fd,char c){transfer(fd,&c,1,1);}
static void expect_cmd(int fd,char want){char c;transfer(fd,&c,1,0);check(c==want,"child protocol");}
static void allocate_target(void)
{
    control=allocate(PAGE);memset(control.data,0x19,PAGE);
    for(unsigned i=0;i<cfg->vectors;i++)ranges[i]=allocate(total_bytes/cfg->vectors);
}
static void prepare_target(struct packet *p)
{
    memset(p,0,sizeof(*p));p->pid=getpid();p->vectors=cfg->vectors;p->cpu=sched_getcpu();p->caps=capabilities();
    for(unsigned i=0;i<cfg->vectors;i++){
        struct region r=ranges[i];check(madvise(r.data,r.bytes,MADV_DONTNEED)==0,"fresh target pages");
        memset(r.data,0x3d,r.bytes);struct state s=inspect(r);
        check(s.vmas==1 && s.huge_kb==0 && s.lazy_kb==0 && s.rss_kb==r.bytes/1024,"one base-page VMA per vector");
        p->resident_pages+=resident(r.data,r.bytes);p->rss_kb+=s.rss_kb;
        p->iov[i]=(struct iovec){r.data,r.bytes};
    }
    check(madvise(control.data,PAGE,MADV_COLD)==0,"target CPU initial LRU batch drain");
    check(p->resident_pages==total_bytes/PAGE && p->rss_kb==total_bytes/1024,"complete resident target");
}
static void verify_target(void)
{
    for(unsigned i=0;i<cfg->vectors;i++){
        struct region r=ranges[i];struct state s=inspect(r);
        check(s.vmas==1 && s.huge_kb==0 && s.rss_kb==r.bytes/1024 && resident(r.data,r.bytes)==r.bytes/PAGE,"COLD retains target mappings/pages");
        contents(r.data,r.bytes,0x3d);memset(r.data,0x7b,r.bytes);contents(r.data,r.bytes,0x7b);
    }
    contents(control.data,PAGE,0x19);
}
static void release_target(void)
{for(unsigned i=0;i<cfg->vectors;i++)release(ranges[i]);release(control);}
static void child(int from_parent,int to_parent,pid_t expected_parent)
{
    set_caps(0);check(prctl(PR_SET_DUMPABLE,1,0,0,0)==0,"same-uid readable child");
    check(prctl(PR_SET_PDEATHSIG,SIGKILL)==0 && getppid()==expected_parent,"child parent-death cleanup");
    pin(4);alarm(25);allocate_target();struct packet p;
    for(unsigned round=0;round<2;round++){
        expect_cmd(from_parent,'P');prepare_target(&p);transfer(to_parent,&p,sizeof(p),1);
        expect_cmd(from_parent,'V');verify_target();send_cmd(to_parent,'V');
    }
    expect_cmd(from_parent,'X');release_target();send_cmd(to_parent,'X');
    close(from_parent);close(to_parent);_exit(0);
}

int measured_main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--selftest")){
        check(sizeof(struct iovec)==16 && CAP_SYS_NICE==23 && sizeof(choices)/sizeof(choices[0])==4,"native ABI");
        puts("selftest=pass runtime_changes=0");return 0;
    }
    const char *gate=getenv("KS_ALLOW_REMOTE_VECTOR");
    check(argc==2 && gate && !strcmp(gate,"1") && getuid()==0,"bounded privileged launcher");
    for(size_t i=0;i<sizeof(choices)/sizeof(choices[0]);i++)if(!strcmp(argv[1],choices[i].name))cfg=&choices[i];
    check(cfg!=NULL,"declared vector condition");alarm(25);
    struct rlimit lim={256UL*1024*1024,256UL*1024*1024};check(setrlimit(RLIMIT_AS,&lim)==0,"address-space ceiling");
    check(prctl(PR_SET_KEEPCAPS,1,0,0,0)==0 && setgroups(0,NULL)==0 && setresgid(1000,1000,1000)==0 && setresuid(1000,1000,1000)==0,"ordinary same uid/gid");
    uint64_t mask=cfg->denied?0:1ULL<<CAP_SYS_NICE;set_caps(mask);
    check(prctl(PR_SET_KEEPCAPS,0,0,0,0)==0 && prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)==0,"no capability regain");
    pin(2);pid_t requester=getpid(),target=requester;int up[2]={-1,-1},down[2]={-1,-1};
    if(cfg->remote){
        check(pipe2(up,O_CLOEXEC)==0 && pipe2(down,O_CLOEXEC)==0,"private protocol pipes");
        target=fork();check(target>=0,"fork target");
        if(target==0){close(up[0]);close(down[1]);child(down[0],up[1],requester);}
        close(up[1]);close(down[0]);
    }else allocate_target();
    int fd=syscall(SYS_pidfd_open,target,0);check(fd>=0,"owned pidfd");
    long returns[2];int errors[2];struct packet p;
    for(unsigned round=0;round<2;round++){
        if(cfg->remote){send_cmd(down[1],'P');transfer(up[0],&p,sizeof(p),0);}else prepare_target(&p);
        check(p.pid==target && p.vectors==cfg->vectors && p.cpu==(cfg->remote?4U:2U) && p.caps==(cfg->remote?0:mask),"target identity and privileges");
        check(p.resident_pages==4096 && p.rss_kb==16384 && capabilities()==mask,"target initial state");
        for(unsigned i=0;i<cfg->vectors;i++){
            uintptr_t lo=(uintptr_t)p.iov[i].iov_base,hi=lo+p.iov[i].iov_len;
            check(lo%PAGE==0 && p.iov[i].iov_len==total_bytes/cfg->vectors,"vector shape");
            for(unsigned j=0;j<i;j++){
                uintptr_t other=(uintptr_t)p.iov[j].iov_base,end=other+p.iov[j].iov_len;
                check(hi<other || lo>end,"independent non-contiguous vectors");
            }
            vector_range(lo,hi,1);
        }
        errno=0;vector_begin(cfg->vectors,target);
        returns[round]=syscall(SYS_process_madvise,fd,p.iov,cfg->vectors,MADV_COLD,0);
        errors[round]=errno;vector_end();
        check(cfg->denied?(returns[round]==-1 && errors[round]==EPERM):(returns[round]==(long)total_bytes && errors[round]==0),"complete result or deliberate permission denial");
        for(unsigned i=0;i<cfg->vectors;i++)vector_range((uintptr_t)p.iov[i].iov_base,(uintptr_t)p.iov[i].iov_base+p.iov[i].iov_len,0);
        if(cfg->remote){send_cmd(down[1],'V');expect_cmd(up[0],'V');}else verify_target();
    }
    check(close(fd)==0,"close pidfd");
    if(cfg->remote){
        send_cmd(down[1],'X');expect_cmd(up[0],'X');close(down[1]);close(up[0]);int status;
        check(waitpid(target,&status,0)==target && WIFEXITED(status) && WEXITSTATUS(status)==0,"target joined after unmapping");
    }else release_target();
    set_caps(0);
    printf("{\"case\":\"%s\",\"requester_pid\":%d,\"target_pid\":%d,\"remote\":%u,\"denied\":%u,\"vectors\":%u,\"bytes\":%zu,\"rounds\":2,\"uid\":%u,\"gid\":%u,\"requester_cap_mask\":%" PRIu64 ",\"target_cap_mask\":%" PRIu64 ",\"requester_cpu\":2,\"target_cpu\":%u,\"resident_pages\":4096,\"rss_kb\":16384,\"content_pass\":true,\"topology_pass\":true,\"unmapped\":true,\"joined\":true,\"caps_cleared\":true,\"timing\":true,\"returns\":[%ld,%ld],\"errors\":[%d,%d]}\n",
        cfg->name,requester,target,cfg->remote,cfg->denied,cfg->vectors,total_bytes,getuid(),getgid(),mask,p.caps,p.cpu,returns[0],returns[1],errors[0],errors[1]);
    return 0;
}

#ifndef KS_NO_MAIN_WRAPPER
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--selftest"))return measured_main(argc,argv);
    ks_init();
    uint64_t child=ks_children_cpu(),cpu=ks_clock(CLOCK_PROCESS_CPUTIME_ID),wall=ks_clock(CLOCK_MONOTONIC_RAW);
    int rc=measured_main(argc,argv);
    wall=ks_clock(CLOCK_MONOTONIC_RAW)-wall;cpu=ks_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu;
    child=ks_children_cpu()-child;
    if(rc)ks_fail("nonzero workload return");
    ks_emit(wall,cpu,child);return rc;
}
#endif
