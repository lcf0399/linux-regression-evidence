#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Prepare once from 33 private pages; discard 16 before any fork or timer.
 * This avoids relying on the global swap allocator's starting cluster offset.
 * The measured state is still17pages / three16-page removals. No retry. */
enum { PAGE=4096, PAGES=17, CANDIDATES=33, PMD=2*1024*1024, MAX_CHILDREN=14 };
static pid_t owner,children[MAX_CHILDREN];
static int commands[MAX_CHILDREN][2],replies[MAX_CHILDREN][2];
static unsigned char *area;
static uint64_t slots[PAGES];
static unsigned nr_children;
static unsigned selected_first;
__attribute__((noinline,noclone)) void ks_slot(unsigned long type,unsigned long off,unsigned long page)
{asm volatile(""::"r"(type),"r"(off),"r"(page):"memory");}
__attribute__((noinline,noclone)) void ks_owner(unsigned long pid)
{asm volatile(""::"r"(pid):"memory");}
__attribute__((noinline,noclone)) void ks_step(unsigned long stage,unsigned long address,unsigned long length)
{asm volatile(""::"r"(stage),"r"(address),"r"(length):"memory");}
__attribute__((noinline,noclone)) void ks_end(void){asm volatile("":::"memory");}
static void cleanup(void)
{
    if(getpid()!=owner)return;
    for(unsigned i=0;i<MAX_CHILDREN;i++)if(children[i]>0)kill(children[i],SIGKILL);
    for(unsigned i=0;i<MAX_CHILDREN;i++)if(children[i]>0){while(waitpid(children[i],NULL,0)<0&&errno==EINTR){}children[i]=0;}
}
static void expired(int sig)
{(void)sig;if(getpid()==owner)for(unsigned i=0;i<MAX_CHILDREN;i++)if(children[i]>0)kill(children[i],SIGKILL);_exit(124);}
static void need(int ok,const char *why)
{if(!ok){fprintf(stderr,"FAIL %s errno=%d\n",why,errno);exit(1);}}
static uint64_t now(void)
{struct timespec t;need(!clock_gettime(CLOCK_MONOTONIC_RAW,&t),"clock");return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void pin(void){cpu_set_t set;CPU_ZERO(&set);CPU_SET(2,&set);need(!sched_setaffinity(0,sizeof(set),&set),"CPU2");}
static void send_byte(int fd,char c){need(write(fd,&c,1)==1,"pipe write");}
static char receive(int fd)
{
    struct pollfd p={.fd=fd,.events=POLLIN};int ret;
    do{ret=poll(&p,1,5000);}while(ret<0&&errno==EINTR);
    need(ret==1&&(p.revents&POLLIN),"bounded pipe wait");char c;need(read(fd,&c,1)==1,"pipe read");return c;
}
static uint64_t entry(pid_t pid,unsigned page)
{
    char name[80];snprintf(name,sizeof(name),"/proc/%ld/pagemap",(long)pid);
    int fd=open(name,O_RDONLY|O_CLOEXEC);need(fd>=0,"owned pagemap open");uint64_t value;
    off_t offset=((uintptr_t)(area+page*PAGE)/PAGE)*sizeof(value);
    need(pread(fd,&value,sizeof(value),offset)==sizeof(value),"owned pagemap read");need(!close(fd),"pagemap close");return value;
}
static int swapped(uint64_t value){return (value&(1ULL<<62))&&!(value&(1ULL<<63));}
static uint64_t slot(uint64_t value){return value&((1ULL<<55)-1);}
static int choose_window(const uint64_t values[CANDIDATES])
{
    for(unsigned start=0;start<=CANDIDATES-PAGES;start++){
        uint64_t first=slot(values[start]),off=first>>5;
        if(off/512!=(off+PAGES-1)/512)continue;
        int valid=1;
        for(unsigned j=0;j<PAGES;j++)if(!swapped(values[start+j])||slot(values[start+j])!=first+((uint64_t)j<<5))valid=0;
        if(valid)return (int)start;
    }
    return -1;
}
static void selftest(void)
{
    uint64_t values[CANDIDATES];
    for(unsigned off=1;off<=1024;off++){
        for(unsigned j=0;j<CANDIDATES;j++)values[j]=(1ULL<<62)|((uint64_t)(off+j)<<5);
        int expected=off%512<=512-PAGES?0:512-off%512;
        need(choose_window(values)==expected,"deterministic window at every boundary");
    }
    for(unsigned j=0;j<CANDIDATES;j++)values[j]=(1ULL<<62)|((uint64_t)(1+2*j)<<5);
    need(choose_window(values)==-1,"reject noncontiguous slots");
    memset(values,0,sizeof(values));need(choose_window(values)==-1,"reject resident or empty pages");
    puts("selftest=pass no_runtime_changes=1");
}
static void validate(pid_t pid,int removed)
{
    for(unsigned j=0;j<PAGES;j++){
        uint64_t v=entry(pid,j);
        if(removed&&j)need(!(v&((1ULL<<63)|(1ULL<<62)))&&!slot(v),"removed target absent");
        else need(swapped(v)&&slot(v)==slots[j],"same owned swapped entry");
    }
}
static void child(unsigned index)
{
    need(!prctl(PR_SET_PDEATHSIG,SIGKILL),"parent death protection");need(getppid()==owner,"parent still alive");pin();
    for(unsigned i=0;i<=index;i++){
        if(i!=index){close(commands[i][0]);close(replies[i][1]);}
        close(commands[i][1]);close(replies[i][0]);
    }
    /* Warm clock code before reporting readiness, outside the measured call. */
    (void)now();send_byte(replies[index][1],'R');
    for(;;){
        char c=receive(commands[index][0]);
        if(c=='Q')break;
        need(c>='1'&&c<='3',"one removal command");
        struct rusage a,b;need(!getrusage(RUSAGE_SELF,&a),"rusage before");
        ks_step((unsigned)(c-'0'),(uintptr_t)(area+PAGE),(PAGES-1)*PAGE);
        uint64_t begin=now();
        int ret=munmap(area+PAGE,(PAGES-1)*PAGE);
        uint64_t end=now();
        ks_end();need(!ret&&end>begin,"owned target unmap and clock");
        need(!getrusage(RUSAGE_SELF,&b),"rusage after");
        printf("TIME stage=%u wall_ns=%"PRIu64" nvcsw=%ld nivcsw=%ld minflt=%ld majflt=%ld cpu=%d\n",
            (unsigned)(c-'0'),end-begin,b.ru_nvcsw-a.ru_nvcsw,b.ru_nivcsw-a.ru_nivcsw,
            b.ru_minflt-a.ru_minflt,b.ru_majflt-a.ru_majflt,sched_getcpu());
        send_byte(replies[index][1],'D');
    }
    close(commands[index][0]);close(replies[index][1]);_exit(0);
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--selftest")){selftest();return 0;}
    need(argc==2&&(!strcmp(argv[1],"anchor")||!strcmp(argv[1],"low"))&&getenv("KS_SWAP_COUNT_RUN")&&!strcmp(getenv("KS_SWAP_COUNT_RUN"),"1"),"explicit bounded mode");
    need(sysconf(_SC_PAGESIZE)==PAGE&&geteuid()==0,"root owned-page metadata and4K pages");
    nr_children=!strcmp(argv[1],"anchor")?14:7;owner=getpid();setvbuf(stdout,NULL,_IONBF,0);
    atexit(cleanup);signal(SIGALRM,expired);signal(SIGPIPE,SIG_IGN);alarm(40);pin();
    size_t length=CANDIDATES*PAGE,reserved=PMD+length;
    unsigned char *raw=mmap(NULL,reserved,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    need(raw!=MAP_FAILED,"owned alignment reservation");
    area=(unsigned char *)(((uintptr_t)raw+PMD-1)&~(uintptr_t)(PMD-1));
    size_t prefix=(size_t)(area-raw),suffix=reserved-prefix-length;
    if(prefix)need(!munmap(raw,prefix),"unused prefix");
    if(suffix)need(!munmap(area+length,suffix),"unused suffix");
    need((uintptr_t)area%PMD==0&&((uintptr_t)(area+length-1)/PMD)==(uintptr_t)area/PMD,"one PMD span");
    need(!madvise(area,length,MADV_NOHUGEPAGE),"owned no THP");
    for(unsigned j=0;j<CANDIDATES;j++)memset(area+j*PAGE,0x30+j,PAGE);
    need(!madvise(area,length,MADV_PAGEOUT),"owned pageout");
    int all=0;
    for(unsigned attempt=0;attempt<100;attempt++){
        all=1;for(unsigned j=0;j<CANDIDATES;j++)if(!swapped(entry(owner,j)))all=0;
        if(all)break;
        struct timespec wait={0,20000000};nanosleep(&wait,NULL);
    }
    need(all,"all pages swapped within2s without pressure");
    uint64_t values[CANDIDATES];for(unsigned j=0;j<CANDIDATES;j++)values[j]=entry(owner,j);
    int first=choose_window(values);need(first>=0,"one deterministic eligible window without retry");selected_first=(unsigned)first;
    if(selected_first)need(!munmap(area,selected_first*PAGE),"discard unused candidate prefix");
    unsigned tail=CANDIDATES-selected_first-PAGES;
    if(tail)need(!munmap(area+(selected_first+PAGES)*PAGE,tail*PAGE),"discard unused candidate suffix");
    area+=selected_first*PAGE;length=PAGES*PAGE;
    need((uintptr_t)area/PMD==(uintptr_t)(area+length-1)/PMD,"selected span stays in one PMD");
    printf("SETUP candidates=%u selected_first=%u discarded=%u\n",CANDIDATES,selected_first,CANDIDATES-PAGES);
    printf("MAPPING pmd_offset=%zu target_offset=%u target_bytes=%u touched_bytes=%zu\n",(size_t)((uintptr_t)area%PMD),PAGE,(PAGES-1)*PAGE,length);
    uint64_t cluster=0;unsigned type=0;
    for(unsigned j=0;j<PAGES;j++){
        slots[j]=slot(entry(owner,j));uint64_t off=slots[j]>>5;unsigned kind=slots[j]&31;
        if(!j){cluster=off/512;type=kind;}
        printf("SLOT page=%u type=%u offset=%"PRIu64" cluster=%"PRIu64"\n",j,kind,off,off/512);
        need(kind==type&&off/512==cluster,"same actual swap cluster");
        for(unsigned k=0;k<j;k++)need(slots[k]!=slots[j],"distinct swap slots");
        ks_slot(kind,off,j);
    }
    for(unsigned i=0;i<nr_children;i++){
        need(!pipe2(commands[i],O_CLOEXEC)&&!pipe2(replies[i],O_CLOEXEC),"owned pipes");
        children[i]=fork();need(children[i]>=0,"bounded fork");
        if(!children[i])child(i);
        close(commands[i][0]);commands[i][0]=-1;
        close(replies[i][1]);replies[i][1]=-1;
        need(receive(replies[i][0])=='R',"child waiting");ks_owner(children[i]);validate(children[i],0);
    }
    validate(owner,0);printf("STATE stage=0 refs=%u anchor_refs=%u pages=%u swapped=1\n",nr_children+1,nr_children+1,PAGES);
    for(unsigned stage=1;stage<=3;stage++){
        send_byte(commands[stage-1][1],'0'+stage);need(receive(replies[stage-1][0])=='D',"removal completed");
        validate(owner,0);
        for(unsigned i=0;i<nr_children;i++)validate(children[i],i<stage);
        printf("STATE stage=%u refs=%u anchor_refs=%u pages=%u swapped=1\n",stage,nr_children+1-stage,nr_children+1,PAGES-1);
    }
    for(unsigned i=0;i<nr_children;i++)send_byte(commands[i][1],'Q');
    for(unsigned i=0;i<nr_children;i++){
        int status;need(waitpid(children[i],&status,0)==children[i]&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"owned child exit");children[i]=0;
        close(commands[i][1]);close(replies[i][0]);
    }
    validate(owner,0);
    for(unsigned j=0;j<PAGES;j++)for(unsigned i=0;i<PAGE;i++)need(area[j*PAGE+i]==0x30+selected_first+j,"full refault content");
    need(!munmap(area,length),"parent unmap");alarm(0);
    printf("RESULT mode=%s children=%u pages=%u cpu=%d cleanup=1 timing=1\n",argv[1],nr_children,PAGES,sched_getcpu());
    return 0;
}
