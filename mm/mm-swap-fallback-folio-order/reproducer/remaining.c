#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif
enum { PAGE=4096, PMD=2*1024*1024, TOTAL=8*1024*1024, NP=TOTAL/PAGE, ROUNDS=4 };
static int pm=-1, kpf=-1;
static void need(int ok,const char *s)
{if(!ok){fprintf(stderr,"FAIL %s errno=%d pid=%d\n",s,errno,getpid());exit(1);}}
/* One write <= PIPE_BUF prevents two workers interleaving numeric records. */
static void report(const char *format,...)
{
    char line[1024];va_list args;va_start(args,format);
    int n=vsnprintf(line,sizeof(line),format,args);va_end(args);
    need(n>0&&n<(int)sizeof(line),"record bound");
    need(write(STDOUT_FILENO,line,(size_t)n)==n,"atomic record write");
}
static uint64_t clock_ns(clockid_t id)
{struct timespec t;need(!clock_gettime(id,&t),"clock");return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void pause_ms(unsigned n)
{struct timespec t={n/1000,(long)(n%1000)*1000000};while(nanosleep(&t,&t))need(errno==EINTR,"sleep");}
static void affinity(int cpu)
{cpu_set_t c;CPU_ZERO(&c);CPU_SET(cpu,&c);need(!sched_setaffinity(0,sizeof(c),&c),"affinity");}
static void send_byte(int fd,char c){need(write(fd,&c,1)==1,"pipe send");}
static char recv_byte(int fd){char c;need(read(fd,&c,1)==1,"pipe receive");return c;}
__attribute__((noinline,noclone)) void ks_sw_phase(unsigned phase,unsigned round,unsigned worker)
{asm volatile(""::"r"(phase),"r"(round),"r"(worker):"memory");}
__attribute__((noinline,noclone)) void ks_sw_cache(unsigned phase)
{asm volatile(""::"r"(phase):"memory");}
static unsigned char *mapping(size_t len,int huge)
{
    unsigned char *raw=mmap(NULL,len+PMD,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    need(raw!=MAP_FAILED,"mapping");unsigned char *a=(unsigned char *)(((uintptr_t)raw+PMD-1)&~(uintptr_t)(PMD-1));
    size_t pre=a-raw,post=PMD-pre;
    if(pre)need(!munmap(raw,pre),"alignment prefix");
    if(post)need(!munmap(a+len,post),"alignment suffix");
    need(!madvise(a,len,huge?MADV_HUGEPAGE:MADV_NOHUGEPAGE),"per VMA page policy");return a;
}
static void read_entries(unsigned char *a,unsigned n,uint64_t *v)
{need(pread(pm,v,(size_t)n*8,((uintptr_t)a/PAGE)*8)==(ssize_t)n*8,"own pagemap");}
static int is_swap(uint64_t v){return !!(v&(1ULL<<62))&&!(v&(1ULL<<63));}
static uint64_t swp(uint64_t v){return v&((1ULL<<55)-1);}
static unsigned char pattern(unsigned j,unsigned r){return (unsigned char)(1+(j*13+r*17)%251);}
static void fill(unsigned char *a,unsigned n,unsigned r)
{for(unsigned j=0;j<n;j++)memset(a+j*PAGE,pattern(j,r),PAGE);}
static void verify(unsigned char *a,unsigned n,unsigned r)
{for(unsigned j=0;j<n;j++)for(unsigned k=0;k<PAGE;k++)need(a[j*PAGE+k]==pattern(j,r),"content");}
static unsigned thp_pages(unsigned char *a,unsigned n)
{
    uint64_t v[NP];read_entries(a,n,v);unsigned count=0;
    for(unsigned j=0;j<n;j++){
        need((v[j]&(1ULL<<63))&&!(v[j]&(1ULL<<62)),"resident page");
        uint64_t pfn=swp(v[j]),flags;need(pfn&&pread(kpf,&flags,8,pfn*8)==8,"owned PFN flags");
        if(flags&(1ULL<<22))count++;
    }return count;
}
static unsigned swapcache_pages(unsigned char *a,unsigned n)
{
    uint64_t v[NP];read_entries(a,n,v);unsigned count=0;
    for(unsigned j=0;j<n;j++){
        need((v[j]&(1ULL<<63))&&!(v[j]&(1ULL<<62)),"resident cache check");
        uint64_t flags;need(pread(kpf,&flags,8,swp(v[j])*8)==8,"cache flags");count+=!!(flags&(1ULL<<13));
    }return count;
}
static void swapped(unsigned char *a,unsigned n,uint64_t *v)
{
    int all=0;
    for(unsigned t=0;t<100;t++){
        read_entries(a,n,v);all=1;for(unsigned j=0;j<n;j++)if(!is_swap(v[j]))all=0;
        if(all)break;
        pause_ms(20);
    }
    need(all,"all pages swapped within2s");
    for(unsigned j=0;j<n;j++)need(!(swp(v[j])&31),"existing own swap type0");
}
static unsigned cluster_count(uint64_t *v,unsigned n)
{
    uint64_t clusters[NP];unsigned count=0;
    for(unsigned j=0;j<n;j++){
        if(!is_swap(v[j]))continue;
        uint64_t c=(swp(v[j])>>5)/512;unsigned k=0;while(k<count&&clusters[k]!=c)k++;
        if(k==count)clusters[count++]=c;
    }return count;
}
static void join_group(void)
{
    const char *cg=getenv("KS_SW_CGROUP");need(cg&&!strncmp(cg,"/sys/fs/cgroup/ks-swap-remain-",strlen("/sys/fs/cgroup/ks-swap-remain-")),"owned cgroup");
    char path[1024],buf[64];need(snprintf(path,sizeof(path),"%s/cgroup.procs",cg)<(int)sizeof(path),"group path");
    int fd=open(path,O_WRONLY|O_CLOEXEC);need(fd>=0,"group fd");int n=snprintf(buf,sizeof(buf),"%d",getpid());
    need(write(fd,buf,n)==n&&!close(fd),"join group");
}
static void open_meta(void)
{pm=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC);kpf=open("/proc/kpageflags",O_RDONLY|O_CLOEXEC);need(pm>=0&&kpf>=0,"metadata");}
static void worker(unsigned id,unsigned workers,int huge,int in,int out)
{
    need(!prctl(PR_SET_PDEATHSIG,SIGKILL)&&getppid()!=1,"parent death guard");alarm(25);affinity(id?4:2);open_meta();
    unsigned n=NP/workers;size_t len=(size_t)n*PAGE;unsigned char *a=mapping(len,huge);uint64_t v[NP];
    for(unsigned r=0;r<ROUNDS;r++){
        fill(a,n,r);if(huge)need(!madvise(a,len,MADV_COLLAPSE),"collapse once");
        unsigned hp=thp_pages(a,n);need(hp==(huge?n:0),"actual physical page size");
        send_byte(out,'R');need(recv_byte(in)=='G',"go");
        ks_sw_phase(1,r,id);uint64_t s=clock_ns(CLOCK_MONOTONIC_RAW),c=clock_ns(CLOCK_THREAD_CPUTIME_ID);
        need(!madvise(a,len,MADV_PAGEOUT),"pageout once");
        uint64_t pc=clock_ns(CLOCK_THREAD_CPUTIME_ID)-c,pw=clock_ns(CLOCK_MONOTONIC_RAW)-s;ks_sw_phase(0,r,id);
        swapped(a,n,v);pause_ms(20);read_entries(a,n,v);
        for(unsigned j=0;j<n;j++)need(is_swap(v[j]),"stable swapped pages");
        unsigned char mc[NP];need(!mincore(a,len,mc),"cache readiness");unsigned ready=0;
        for(unsigned j=0;j<n;j++)ready+=mc[j]&1;
        ks_sw_phase(2,r,id);s=clock_ns(CLOCK_MONOTONIC_RAW);c=clock_ns(CLOCK_THREAD_CPUTIME_ID);
        unsigned sum=0;for(unsigned j=0;j<n;j++)sum+=*(volatile unsigned char *)(a+j*PAGE);
        uint64_t rc=clock_ns(CLOCK_THREAD_CPUTIME_ID)-c,rw=clock_ns(CLOCK_MONOTONIC_RAW)-s;ks_sw_phase(0,r,id);
        unsigned expect=0;for(unsigned j=0;j<n;j++)expect+=pattern(j,r);need(sum==expect,"refault sum");verify(a,n,r);
        unsigned after=thp_pages(a,n);report("CYCLE worker=%u workers=%u huge=%d round=%u pages=%u thp_before=%u thp_after=%u swapped=%u ready=%u clusters=%u pageout_ns=%"PRIu64" pageout_cpu_ns=%"PRIu64" refault_ns=%"PRIu64" refault_cpu_ns=%"PRIu64" cpu=%d content_ok=1\n",id,workers,huge,r,n,hp,after,n,ready,cluster_count(v,n),pw,pc,rw,rc,sched_getcpu());
        ks_sw_phase(3,r,id);need(!madvise(a,len,MADV_DONTNEED),"discard");ks_sw_phase(0,r,id);send_byte(out,'D');
    }
    need(!munmap(a,len)&&!close(pm)&&!close(kpf),"worker cleanup");_exit(0);
}
static void concurrent(unsigned workers,int huge)
{
    int cmd[2][2],reply[2][2];pid_t pids[2];
    for(unsigned i=0;i<workers;i++)need(!pipe2(cmd[i],O_CLOEXEC)&&!pipe2(reply[i],O_CLOEXEC),"pipes");
    for(unsigned i=0;i<workers;i++){
        pids[i]=fork();need(pids[i]>=0,"fork");
        if(!pids[i]){
            for(unsigned j=0;j<workers;j++){
                close(cmd[j][1]);close(reply[j][0]);
                if(i!=j){close(cmd[j][0]);close(reply[j][1]);}
            }
            worker(i,workers,huge,cmd[i][0],reply[i][1]);
        }
    }
    for(unsigned i=0;i<workers;i++){close(cmd[i][0]);close(reply[i][1]);}
    for(unsigned r=0;r<ROUNDS;r++){
        for(unsigned i=0;i<workers;i++)need(recv_byte(reply[i][0])=='R',"worker ready");
        uint64_t s=clock_ns(CLOCK_MONOTONIC_RAW);
        for(unsigned i=0;i<workers;i++)send_byte(cmd[i][1],'G');
        for(unsigned i=0;i<workers;i++)need(recv_byte(reply[i][0])=='D',"worker done");
        report("BARRIER round=%u workers=%u wall_ns=%"PRIu64" includes_fixed_wait=1 includes_checks=1\n",r,workers,clock_ns(CLOCK_MONOTONIC_RAW)-s);
    }
    for(unsigned i=0;i<workers;i++){int status;need(waitpid(pids[i],&status,0)==pids[i]&&WIFEXITED(status)&&!WEXITSTATUS(status),"worker status");close(cmd[i][1]);close(reply[i][0]);}
    report("RESULT family=F02 workers=%u huge=%d rounds=4 total_bytes=8388608 reaped=%u content_ok=1\n",workers,huge,workers);
}
static void lifecycle(int anchors)
{
    affinity(2);open_meta();unsigned char *a=mapping(TOTAL,0);uint64_t v[NP],original[NP];unsigned char keep[NP]={0};
    unsigned nanchors=0;uint64_t total_start=clock_ns(CLOCK_MONOTONIC_RAW);
    fill(a,NP,0);ks_sw_phase(1,0,0);uint64_t start=clock_ns(CLOCK_MONOTONIC_RAW),cpu_start=clock_ns(CLOCK_THREAD_CPUTIME_ID);
    need(!madvise(a,TOTAL,MADV_PAGEOUT),"prime pageout");
    uint64_t cpu_ns=clock_ns(CLOCK_THREAD_CPUTIME_ID)-cpu_start,wall_ns=clock_ns(CLOCK_MONOTONIC_RAW)-start;ks_sw_phase(0,0,0);
    report("LIFE_OUT round=0 calls=1 pages=2048 wall_ns=%"PRIu64" cpu_ns=%"PRIu64"\n",wall_ns,cpu_ns);
    swapped(a,NP,v);pause_ms(20);memcpy(original,v,sizeof(v));
    for(unsigned j=0;j<NP;j++){
        unsigned k=0;while(k<j&&((swp(v[k])>>5)/512!=(swp(v[j])>>5)/512))k++;
        if(k==j){keep[j]=1;nanchors++;report("ANCHOR page=%u offset=%"PRIu64" cluster=%"PRIu64"\n",j,swp(v[j])>>5,(swp(v[j])>>5)/512);}
    }
    need(nanchors>0&&nanchors<=8,"bounded cluster spread");
    if(!anchors){ks_sw_phase(4,0,0);for(unsigned j=0;j<NP;j++)if(keep[j])need(!madvise(a+j*PAGE,PAGE,MADV_DONTNEED),"remove control anchor");ks_sw_phase(0,0,0);}
    for(unsigned r=0;r<4;r++){
        unsigned anchored=0;
        for(unsigned j=0;j<NP;j++)if(!keep[j]){
            need(is_swap(v[j]),"payload swapped");
            for(unsigned k=0;k<NP;k++)if(keep[k]&&(swp(original[k])>>5)/512==(swp(v[j])>>5)/512){anchored++;break;}
        }
        report("LIFE_STATE round=%u anchors=%d anchor_slots=%u payload_pages=%u original_cluster_pages=%u clusters=%u\n",r,anchors,nanchors,NP-nanchors,anchored,cluster_count(v,NP));
        ks_sw_phase(3,r,0);uint64_t s=clock_ns(CLOCK_MONOTONIC_RAW);cpu_start=clock_ns(CLOCK_THREAD_CPUTIME_ID);
        for(unsigned j=0;j<NP;j++)if(!keep[j])need(!madvise(a+j*PAGE,PAGE,MADV_DONTNEED),"payload discard");
        cpu_ns=clock_ns(CLOCK_THREAD_CPUTIME_ID)-cpu_start;uint64_t ns=clock_ns(CLOCK_MONOTONIC_RAW)-s;ks_sw_phase(0,r,0);
        report("LIFE_RELEASE round=%u calls=%u wall_ns=%"PRIu64" cpu_ns=%"PRIu64"\n",r,NP-nanchors,ns,cpu_ns);
        read_entries(a,NP,v);
        for(unsigned j=0;j<NP;j++){
            if(keep[j]&&anchors)need(is_swap(v[j])&&swp(v[j])==swp(original[j]),"same untouched anchors");
            else need(!(v[j]&((1ULL<<63)|(1ULL<<62))),"released PTE");
        }
        if(r==3)break;
        for(unsigned j=0;j<NP;j++)if(!keep[j])memset(a+j*PAGE,pattern(j,r+1),PAGE);
        ks_sw_phase(1,r+1,0);start=clock_ns(CLOCK_MONOTONIC_RAW);cpu_start=clock_ns(CLOCK_THREAD_CPUTIME_ID);unsigned calls=0;
        for(unsigned j=0;j<NP;){
            if(keep[j]){j++;continue;}unsigned k=j;while(k<NP&&!keep[k])k++;
            need(!madvise(a+j*PAGE,(k-j)*PAGE,MADV_PAGEOUT),"refill pageout");calls++;j=k;
        }
        cpu_ns=clock_ns(CLOCK_THREAD_CPUTIME_ID)-cpu_start;wall_ns=clock_ns(CLOCK_MONOTONIC_RAW)-start;ks_sw_phase(0,r+1,0);
        report("LIFE_OUT round=%u calls=%u pages=%u wall_ns=%"PRIu64" cpu_ns=%"PRIu64"\n",r+1,calls,NP-nanchors,wall_ns,cpu_ns);
        pause_ms(20);read_entries(a,NP,v);
        for(unsigned j=0;j<NP;j++)if(!keep[j])need(is_swap(v[j]),"refill swapped");
    }
    if(anchors)for(unsigned j=0;j<NP;j++)if(keep[j])for(unsigned k=0;k<PAGE;k++)need(a[j*PAGE+k]==pattern(j,0),"anchor data");
    for(unsigned j=0;j<NP;j++)if(!keep[j])for(unsigned k=0;k<PAGE;k++)need(a[j*PAGE+k]==0,"discarded payload zero");
    ks_sw_phase(5,0,0);need(!munmap(a,TOTAL),"final anchor release");ks_sw_phase(0,0,0);
    report("LIFE_TOTAL wall_ns=%"PRIu64" includes_wait_checks=1 excludes_callback_tail=1\n",clock_ns(CLOCK_MONOTONIC_RAW)-total_start);
    /* Fixed observations are NOT proof that all RCU callbacks have completed. */
    pause_ms(1000);puts("TAIL observation_ms=1000");pause_ms(4000);puts("TAIL observation_ms=5000");
    close(pm);close(kpf);report("RESULT family=F03 anchors=%d anchor_slots=%u rounds=4 content_ok=1\n",anchors,nanchors);
}
static void large_cache(int huge)
{
    affinity(2);open_meta();unsigned char *a=mapping(PMD,huge);uint64_t v[512],orig[512];fill(a,512,0);
    if(huge)need(!madvise(a,PMD,MADV_COLLAPSE),"large cache collapse");
    unsigned hp=thp_pages(a,512);need(hp==(huge?512:0),"initial page shape");
    ks_sw_phase(1,0,0);need(!madvise(a,PMD,MADV_PAGEOUT),"one large cache pageout");ks_sw_phase(0,0,0);
    swapped(a,512,v);pause_ms(20);memcpy(orig,v,sizeof(v));
    int cmd[2],reply[2];need(!pipe2(cmd,O_CLOEXEC)&&!pipe2(reply,O_CLOEXEC),"cache pipes");
    pid_t child=fork();need(child>=0,"cache fork");
    if(!child){
        need(!prctl(PR_SET_PDEATHSIG,SIGKILL)&&getppid()!=1,"child parent guard");alarm(20);close(cmd[1]);close(reply[0]);
        close(pm);pm=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC);need(pm>=0,"child pagemap");affinity(4);
        ks_sw_phase(2,0,1);verify(a,512,0);ks_sw_phase(0,0,1);
        report("CHILD huge=%d thp_pages=%u swapcache_pages=%u resident_pages=512 content_ok=1\n",huge,thp_pages(a,512),swapcache_pages(a,512));
        send_byte(reply[1],'R');need(recv_byte(cmd[0])=='X',"cache exit");verify(a,512,0);need(!munmap(a,PMD),"child cleanup");_exit(0);
    }
    close(cmd[0]);close(reply[1]);need(recv_byte(reply[0])=='R',"child restored");read_entries(a,512,v);
    for(unsigned j=0;j<512;j++)need(is_swap(v[j])&&swp(v[j])==swp(orig[j]),"parent still same swap PTE");
    unsigned char mc[512];ks_sw_cache(1);need(!mincore(a,PMD,mc),"cache snapshot");ks_sw_cache(0);
    unsigned ready=0;for(unsigned j=0;j<512;j++)ready+=mc[j]&1;
    report("PARENT huge=%d swapped_pages=512 ready=%u cluster_count=%u\n",huge,ready,cluster_count(v,512));
    need(ready==512,"warm swap cache");send_byte(cmd[1],'X');int status;
    need(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"child reaped");
    verify(a,512,0);need(!munmap(a,PMD),"parent cleanup");close(pm);close(kpf);close(cmd[1]);close(reply[0]);
    report("RESULT family=F04 huge=%d preparation_only=1 swapoff=0 content_ok=1 reaped=1\n",huge);
}
static void selftest(void)
{
    uint64_t v[NP];for(unsigned i=0;i<NP;i++)v[i]=(1ULL<<62)|((uint64_t)(512+i)<<5);
    need(cluster_count(v,NP)==4&&is_swap(v[0])&&swp(v[0])>>5==512,"slot arithmetic");
    need(!is_swap(1ULL<<63)&&!is_swap(0),"PTE classification");
    for(unsigned j=0;j<NP;j++)for(unsigned r=0;r<ROUNDS;r++)need(pattern(j,r)>0,"nonzero data");
    puts("selftest=pass no_runtime_changes=1");
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--selftest")){selftest();return 0;}
    need(argc==2&&getenv("KS_SWAP_REMAIN_RUN")&&!strcmp(getenv("KS_SWAP_REMAIN_RUN"),"1"),"explicit gate");
    need(geteuid()==0&&sysconf(_SC_PAGESIZE)==PAGE,"root4K");alarm(35);setvbuf(stdout,NULL,_IONBF,0);affinity(16);join_group();
    if(!strcmp(argv[1],"serial-base"))concurrent(1,0);
    else if(!strcmp(argv[1],"parallel-base"))concurrent(2,0);
    else if(!strcmp(argv[1],"serial-thp"))concurrent(1,1);
    else if(!strcmp(argv[1],"parallel-thp"))concurrent(2,1);
    else if(!strcmp(argv[1],"life-empty"))lifecycle(0);
    else if(!strcmp(argv[1],"life-anchor"))lifecycle(1);
    else if(!strcmp(argv[1],"cache-base"))large_cache(0);
    else if(!strcmp(argv[1],"cache-thp"))large_cache(1);
    else need(0,"unknown case");
    alarm(0);return 0;
}
