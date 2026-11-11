/* G-W02 bounded no-pressure adapter: same32MiB, finite/max,1/8workers. */
#define main unused_peak_state_main
#include "peak_state.c"
#undef main
#include <time.h>

static uint64_t ns(clockid_t c){struct timespec t;ck(!clock_gettime(c,&t),"clock");return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
/* debugfs simple attributes can be non-seekable; a fresh open starts at zero. */
static uint64_t fresh_counter(const char *p)
{
    int f=open(p,O_RDONLY|O_CLOEXEC);ck(f>=0,"open-counter");char buf[128],*end;
    ssize_t n=read(f,buf,sizeof(buf)-1);ck(n>0,"read-counter");buf[n]=0;errno=0;
    uint64_t value=strtoull(buf,&end,10);ck(!errno&&end!=buf&&*end=='\n'&&!end[1],"counter-number");
    ck(!close(f),"close-counter");return value;
}
static void path(char *p,const char *base,const char *name){ck(snprintf(p,PATH_MAX,"%s/%s",base,name)<PATH_MAX,"path");}
static uint64_t field(const char *base,const char *name)
{
    char p[PATH_MAX],key[128];path(p,base,"memory.stat");FILE *f=fopen(p,"r");ck(f,"stat open");uint64_t v,found=UINT64_MAX;
    while(fscanf(f,"%127s %"SCNu64,key,&v)==2)if(!strcmp(key,name))found=v;
    ck(!fclose(f)&&found!=UINT64_MAX,"stat field");return found;
}
MARK void zswap_scope(unsigned phase,unsigned worker){__asm__ __volatile__(""::"r"(phase),"r"(worker):"memory");}
static uint64_t pattern(unsigned worker,size_t page,size_t word){return ((uint64_t)(worker+1)<<56)^((uint64_t)page<<24)^((word%32+1)*UINT64_C(0x9e3779b97f4a7c15));}
struct result {unsigned worker,cpu;uint64_t bytes,pageout_cpu_ns,pageout_wall_ns,restore_cpu_ns,swapped;};
static void child(const char *leaf,unsigned id,unsigned workers,int ready,int go,int reports,int restore)
{
    ck(!prctl(PR_SET_PDEATHSIG,SIGKILL),"pdeath");pin(id*2);char p[PATH_MAX];path(p,leaf,"cgroup.procs");put(p,"0");
    size_t bytes=(32UL<<20)/workers;uint64_t *m=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    ck(m!=MAP_FAILED&&!madvise(m,bytes,MADV_NOHUGEPAGE),"base pages");
    for(size_t pg=0;pg<bytes/4096;pg++)for(size_t w=0;w<512;w++)m[pg*512+w]=pattern(id,pg,w);
    unsigned char *vec=malloc(bytes/4096);ck(vec&&!mincore(m,bytes,vec),"resident");for(size_t i=0;i<bytes/4096;i++)ck(vec[i]&1,"initial residency");free(vec);
    int pm=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC);ck(pm>=0,"pagemap");char token;
    ck(write(ready,"R",1)==1&&read(go,&token,1)==1,"barrier");zswap_scope(1,id);
    struct result r={.worker=id,.cpu=id*2,.bytes=bytes};uint64_t c=ns(CLOCK_THREAD_CPUTIME_ID),w=ns(CLOCK_MONOTONIC_RAW);
    ck(!madvise(m,bytes,MADV_PAGEOUT),"pageout");r.pageout_cpu_ns=ns(CLOCK_THREAD_CPUTIME_ID)-c;r.pageout_wall_ns=ns(CLOCK_MONOTONIC_RAW)-w;zswap_scope(0,id);
    for(size_t pg=0;pg<bytes/4096;pg++){uint64_t e;ck(pread(pm,&e,8,(off_t)(((uintptr_t)m/4096+pg)*8))==8,"pagemap read");ck(!(e&(UINT64_C(1)<<63))&&(e&(UINT64_C(1)<<62)),"swapped PTE");r.swapped++;}
    ck(write(reports,&r,sizeof(r))==sizeof(r)&&read(restore,&token,1)==1,"hold swapped state");zswap_scope(2,id);c=ns(CLOCK_THREAD_CPUTIME_ID);
    for(size_t pg=0;pg<bytes/4096;pg++)for(size_t v=0;v<512;v++)ck(m[pg*512+v]==pattern(id,pg,v),"full contents");
    r.restore_cpu_ns=ns(CLOCK_THREAD_CPUTIME_ID)-c;zswap_scope(0,id);ck(sched_getcpu()==(int)r.cpu,"CPU");
    ck(!munmap(m,bytes)&&!close(pm),"unmap/close");ck(write(reports,&r,sizeof(r))==sizeof(r),"final report");_exit(0);
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--selftest")){puts("selftest=pass cases=3 no_runtime_changes=1");return 0;}
    ck(argc==3&&geteuid()==0,"args");bool finite=strcmp(argv[2],"unlimited8")!=0;unsigned workers=!strcmp(argv[2],"finite1")?1:8;
    ck(!strcmp(argv[2],"finite1")||!strcmp(argv[2],"finite8")||!strcmp(argv[2],"unlimited8"),"case");
    char root[PATH_MAX],leaf[PATH_MAX],p[PATH_MAX];ck(realpath(argv[1],root),"root");const char *prefix="/sys/fs/cgroup/ks-memcontrol-rest-";
    ck(!strncmp(root,prefix,strlen(prefix))&&!strchr(root+strlen(prefix),'/'),"owned root");alarm(25);pin(18);
    path(leaf,root,"g0");ck(!mkdir(leaf,0755),"mkdir");path(p,leaf,"memory.zswap.max");put(p,finite?"67108864":"max");
    int ready[2],go[2],reports[2],restore[2];ck(!pipe2(ready,O_CLOEXEC)&&!pipe2(go,O_CLOEXEC)&&!pipe2(reports,O_CLOEXEC)&&!pipe2(restore,O_CLOEXEC),"pipes");pid_t pids[8];
    for(unsigned i=0;i<workers;i++){pids[i]=fork();ck(pids[i]>=0,"fork");if(!pids[i]){close(ready[0]);close(go[1]);close(reports[0]);close(restore[1]);child(leaf,i,workers,ready[1],go[0],reports[1],restore[0]);}}
    close(ready[1]);close(go[0]);close(reports[1]);close(restore[0]);for(unsigned i=0;i<workers;i++){char c;ck(read(ready[0],&c,1)==1&&c=='R',"ready all");}
    uint64_t anon=field(leaf,"anon"),thp=field(leaf,"anon_thp");
    printf("PREP anon=%"PRIu64" anon_thp=%"PRIu64"\n",anon,thp);
    ck(anon>=32UL*1024*1024&&anon<36UL*1024*1024&&thp==0,"base anonymous bytes plus bounded worker metadata");
    uint64_t wall=ns(CLOCK_MONOTONIC_RAW);for(unsigned i=0;i<workers;i++)ck(write(go[1],"G",1)==1,"go");
    struct result first[8];unsigned seen=0;for(unsigned i=0;i<workers;i++){struct result r;ck(read(reports[0],&r,sizeof(r))==sizeof(r),"out report");ck(r.worker<workers&&!(seen&(1U<<r.worker)),"unique");seen|=1U<<r.worker;first[r.worker]=r;}
    uint64_t pageout_window=ns(CLOCK_MONOTONIC_RAW)-wall;path(p,leaf,"memory.zswap.current");uint64_t compressed=get(p),zswapped=field(leaf,"zswapped");
    uint64_t stored=fresh_counter("/sys/kernel/debug/zswap/stored_pages");
    printf("STATE compressed_bytes=%"PRIu64" zswapped_bytes=%"PRIu64" stored_pages=%"PRIu64" finite=%u same_bytes=33554432 thp=0\n",compressed,zswapped,stored,finite);
    ck(compressed>0&&compressed<(32UL<<20)&&zswapped>=(28UL<<20)&&zswapped<=(32UL<<20)&&stored==8192,"stored pages and bounded batched statistics");
    for(unsigned i=0;i<workers;i++)ck(write(restore[1],"V",1)==1,"verify go");
    uint64_t cpu=0,restore_cpu=0,swapped=0;seen=0;
    for(unsigned i=0;i<workers;i++){struct result r;ck(read(reports[0],&r,sizeof(r))==sizeof(r),"final report");ck(r.worker<workers&&!(seen&(1U<<r.worker)),"unique final");seen|=1U<<r.worker;
        ck(r.cpu==r.worker*2&&r.bytes==(32UL<<20)/workers&&r.swapped==r.bytes/4096&&r.pageout_cpu_ns==first[r.worker].pageout_cpu_ns,"same result");
        cpu+=r.pageout_cpu_ns;restore_cpu+=r.restore_cpu_ns;swapped+=r.swapped;
        printf("WORKER id=%u cpu=%u bytes=%"PRIu64" pageout_cpu_ns=%"PRIu64" pageout_wall_ns=%"PRIu64" restore_cpu_ns=%"PRIu64" swapped=%"PRIu64"\n",r.worker,r.cpu,r.bytes,r.pageout_cpu_ns,r.pageout_wall_ns,r.restore_cpu_ns,r.swapped);}
    for(unsigned i=0;i<workers;i++){int st;ck(waitpid(pids[i],&st,0)==pids[i]&&WIFEXITED(st)&&WEXITSTATUS(st)==0,"child");}
    close(ready[0]);close(go[1]);close(reports[0]);close(restore[1]);path(p,leaf,"memory.swap.current");uint64_t left_swap=get(p);path(p,leaf,"memory.zswap.current");uint64_t left_zswap=get(p);
    uint64_t stored_left=fresh_counter("/sys/kernel/debug/zswap/stored_pages");
    printf("CLEANUP swap_left=%"PRIu64" zswap_left=%"PRIu64" stored_left=%"PRIu64"\n",left_swap,left_zswap,stored_left);
    /* 20 online CPUs x 64 returned slots x 4KiB: old slot-cache charges can
     * survive unmapping and zswap invalidation. Do not force a global drain. */
    ck(left_swap<=20UL*64*4096&&!(left_swap%4096)&&stored_left==0&&!rmdir(leaf),"bounded logical cleanup");
    printf("RESULT state=pass case=%s workers=%u bytes=33554432 swapped_pages=%"PRIu64" pageout_cpu_ns=%"PRIu64" pageout_window_wall_ns=%"PRIu64" restore_verify_cpu_ns=%"PRIu64" swap_left=%"PRIu64" zswap_left=%"PRIu64" stored_left=%"PRIu64" data=pass cold_restore_proven=0 backend_completion_proven=0\n",argv[2],workers,swapped,cpu,pageout_window,restore_cpu,left_swap,left_zswap,stored_left);
    alarm(0);return 0;
}
