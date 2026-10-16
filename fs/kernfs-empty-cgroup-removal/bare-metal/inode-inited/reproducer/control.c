/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include "partial_scope.h"
#include "cg_shared.h"
struct shared { unsigned ready, stop; pid_t tids[CG_MAX_TASKS]; };
struct result { uint64_t first, second, stat_ns, cycle_ns, id, from, to; int weight, rejected; struct cg_stats stats; struct cg_task tasks[CG_MAX_TASKS]; };
static struct shared *sh;
static volatile sig_atomic_t expired;
static void expire(int sig) { (void)sig; expired=1; }
static uint64_t now(void) { struct timespec t; if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))abort(); return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec; }
static void pause_ms(unsigned ms) { struct timespec t={.tv_sec=ms/1000,.tv_nsec=(ms%1000)*1000000L}; nanosleep(&t,NULL); }
static bool state(const char *wanted)
{ char s[32]={0}; FILE *f=fopen("/sys/kernel/sched_ext/state","r"); if(!f)return false; int n=fscanf(f,"%31s",s); fclose(f); return n==1&&!strcmp(s,wanted); }
static bool await_state(const char *wanted)
{ uint64_t end=now()+5000000000ULL; do {if(state(wanted))return true; pause_ms(1);}while(now()<end); return false; }
static int open_attr(const char *dir,const char *name,int flags)
{ char path[512]; if(snprintf(path,sizeof(path),"%s/%s",dir,name)>=(int)sizeof(path))return -1; return open(path,flags|O_CLOEXEC); }
static int put(int fd,const char *s)
{ size_t n=strlen(s); if(lseek(fd,0,SEEK_SET)<0)return -1; return write(fd,s,n)==(ssize_t)n?0:-1; }
static int value(int fd)
{ char b[64]={0}; if(lseek(fd,0,SEEK_SET)<0||read(fd,b,sizeof(b)-1)<=0)return -1; return atoi(b); }
static uint64_t cgid(const char *path)
{ struct stat st; return stat(path,&st)?0:(uint64_t)st.st_ino; }
static int pin(int cpu)
{ cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu,&s); return sched_setaffinity(0,sizeof(s),&s); }
static void *thread(void *arg)
{
	unsigned i=(unsigned)(uintptr_t)arg;
	sh->tids[i]=(pid_t)syscall(SYS_gettid);
	__atomic_add_fetch(&sh->ready,1,__ATOMIC_RELEASE);
	while(!__atomic_load_n(&sh->stop,__ATOMIC_ACQUIRE)) syscall(SYS_futex,&sh->stop,FUTEX_WAIT,0,NULL,NULL,0);
	return NULL;
}
static pid_t workers(int count)
{
	pid_t owner=getpid(),p=fork(); if(p)return p;
	if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=owner||pin(2)||sched_getscheduler(0)!=SCHED_OTHER)_exit(101);
	signal(SIGALRM,SIG_DFL); alarm(40);
	pthread_t ts[CG_MAX_TASKS];
	for(int i=1;i<count;i++)if(pthread_create(&ts[i],NULL,thread,(void *)(uintptr_t)i))_exit(102);
	thread(NULL);
	for(int i=1;i<count;i++)if(pthread_join(ts[i],NULL))_exit(103);
	_exit(0);
}
static bool task_group(pid_t pid,const char *relative)
{
	char path[80],line[512]; snprintf(path,sizeof(path),"/proc/%d/cgroup",pid);
	FILE *f=fopen(path,"r"); if(!f)return false; bool ok=false;
	while(fgets(line,sizeof(line),f)){line[strcspn(line,"\n")]=0; if(!strncmp(line,"0::",3))ok=!strcmp(line+3,relative);}
	fclose(f); if(!ok||sched_getscheduler(pid)!=SCHED_OTHER)return false;
	snprintf(path,sizeof(path),"/proc/%d/stat",pid); f=fopen(path,"r"); if(!f)return false;
	bool got=fgets(line,sizeof(line),f)!=NULL; fclose(f); char *end=got?strrchr(line,')'):NULL;
	return end&&end[1]==' '&&end[2]=='S';
}
static bool all_in(int n,const char *relative)
{ for(int i=0;i<n;i++)if(!task_group(sh->tids[i],relative))return false; return true; }
static int arm(int cfgfd,int statfd,int taskfd,struct cg_config *cfg,int n)
{
	if(cfgfd<0)return 0;
	unsigned zero=0; struct cg_stats s={}; struct cg_task t={};
	if(bpf_map_update_elem(statfd,&zero,&s,BPF_ANY))return -1;
	for(int i=0;i<n;i++)if(bpf_map_update_elem(taskfd,&sh->tids[i],&t,BPF_ANY))return -1;
	return bpf_map_update_elem(cfgfd,&zero,cfg,BPF_ANY);
}
static int snapshot(int statfd,int taskfd,struct result *r,int n)
{
	if(statfd<0)return 0;
	unsigned zero=0;
	if(bpf_map_lookup_elem(statfd,&zero,&r->stats)||r->stats.errors)return -1;
	for(int i=0;i<n;i++)if(bpf_map_lookup_elem(taskfd,&sh->tids[i],&r->tasks[i])||r->tasks[i].errors)return -1;
	return 0;
}
static int marker(int fd,const char *side,int op,int family)
{ char s[96]; int n=snprintf(s,sizeof(s),"CG %s actor=%d op=%d family=%d\n",side,getpid(),op,family); return fd<0||write(fd,s,n)==n?0:-1; }
int main(int argc,char **argv)
{
	if(argc!=9){fprintf(stderr,"cg OBJECT leaf|weight|move N SAME CALLBACKS DIAGNOSTIC ROUNDS WARMUP\n");return 2;}
	int family=!strcmp(argv[2],"leaf")?1:!strcmp(argv[2],"weight")?2:!strcmp(argv[2],"move")?3:0;
	int n=atoi(argv[3]),same=atoi(argv[4]),callbacks=atoi(argv[5]),diag=atoi(argv[6]),rounds=atoi(argv[7]),warmup=atoi(argv[8]);
	if(!family||(family==3?(n!=1&&n!=8):n!=0)||(same!=0&&same!=1)||(family==1&&same)||
	   (callbacks!=0&&callbacks!=1)||(diag!=0&&diag!=1)||rounds<1||rounds>128||warmup<0||warmup>16||
	   (getenv("CG_TRACE")&&(!diag||rounds!=4||warmup)))return 2;
	/* Diagnostic control identity: same leaf engine, optional no SCX load. */
	const char *off=getenv("CG_SCX_DISABLED");
	bool disabled=off&&!strcmp(off,"1"),graph_mode=getenv("CG_GRAPH")!=NULL;
	const char *pace=getenv("CG_CYCLE_PACE_MS");
	if(!disabled||!pace||(strcmp(pace,"0")&&strcmp(pace,"50")))return 2;
	unsigned pace_ms=(unsigned)atoi(pace); uint64_t measured_begin=0,workload_elapsed_ns=0;
	if((off&&!disabled)||family!=1||n||same||callbacks||diag||getenv("CG_TRACE")||
	   (graph_mode&&(rounds!=4||warmup)))return 2;
	int ret=1,markerfd=-1,afd=-1,bfd=-1,weightfd=-1,cfgfd=-1,statfd=-1,taskfd=-1;
	pid_t child=-1; bool child_reaped=false,root_created=false,a_created=false,b_created=false,leaf_created=false;
	bool negative=family==3&&n==8&&!same&&callbacks&&diag;
	uint64_t aid=0,bid=0;
	const char *stage="setup"; int completed=0;
	char root[128],a[256],b[256],leaf[256],arel[256],brel[256],pidtext[32];
	snprintf(root,sizeof(root),"/sys/fs/cgroup/kernel-study-cg-%d",getpid());
	snprintf(a,sizeof(a),"%s/a",root); snprintf(b,sizeof(b),"%s/b",root); snprintf(leaf,sizeof(leaf),"%s/leaf",root);
	snprintf(arel,sizeof(arel),"%s/a",root+14); snprintf(brel,sizeof(brel),"%s/b",root+14);
	struct bpf_object *obj=NULL; struct bpf_link *link=NULL; struct bpf_map *ops=NULL,*safe=NULL;
	struct cg_safety safety={}; unsigned zero=0; struct cg_config cfg={};
	struct result *rows=calloc((size_t)(rounds+warmup+1),sizeof(*rows)); if(!rows)return 1;
	struct sigaction sa={.sa_handler=expire}; sigemptyset(&sa.sa_mask); sigaction(SIGALRM,&sa,NULL); sigaction(SIGTERM,&sa,NULL); alarm(45);
	if(pin(0)||sched_getscheduler(0)!=SCHED_OTHER||!state("disabled"))goto out;
	if(mkdir(root,0755))goto out;
	root_created=true;
	fprintf(stderr,"CG_ROOT=%s\n",root);
	int cfd=open_attr(root,"cgroup.subtree_control",O_WRONLY); if(cfd<0)goto out;
	int cr=put(cfd,"+cpu"); close(cfd); if(cr)goto out;
	if(mkdir(a,0755))goto out;
	a_created=true;
	if(mkdir(b,0755))goto out;
	b_created=true;
	cfg.root=cgid(root); aid=cgid(a); bid=cgid(b); if(!cfg.root||!aid||!bid||aid==bid)goto out;
	afd=open_attr(a,"cgroup.procs",O_WRONLY); bfd=open_attr(b,"cgroup.procs",O_WRONLY); weightfd=open_attr(a,"cpu.weight",O_RDWR);
	if(afd<0||bfd<0||weightfd<0||value(weightfd)!=100)goto out;
	sh=mmap(NULL,sizeof(*sh),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0); if(sh==MAP_FAILED){sh=NULL;goto out;}
	if(n){
		child=workers(n); if(child<0)goto out;
		uint64_t until=now()+2000000000ULL; while(__atomic_load_n(&sh->ready,__ATOMIC_ACQUIRE)!=(unsigned)n&&now()<until&&!expired)pause_ms(1);
		if(sh->ready!=(unsigned)n)goto out;
		for(int i=0;i<n;i++)for(int j=0;j<i;j++)if(sh->tids[i]==sh->tids[j])goto out;
		snprintf(pidtext,sizeof(pidtext),"%d",child); if(put(afd,pidtext))goto out;
		until=now()+2000000000ULL; while(!all_in(n,arel)&&now()<until&&!expired)pause_ms(1);
		if(!all_in(n,arel))goto out;
	}
	stage="load";
	if(!disabled){
	obj=bpf_object__open_file(argv[1],NULL); if(!obj||libbpf_get_error(obj)){obj=NULL;goto out;}
	ops=bpf_object__find_map_by_name(obj,"cg_ops"); safe=bpf_object__find_map_by_name(obj,"safe");
	struct bpf_map *cm=bpf_object__find_map_by_name(obj,"cg_config_map"),*sm=bpf_object__find_map_by_name(obj,"stats"),*tm=bpf_object__find_map_by_name(obj,"tasks");
	if(!ops||!safe||!partial_only(obj,ops)||!!cm!=diag||!!sm!=diag||!!tm!=diag||
	   !!bpf_object__find_program_by_name(obj,"cg_weight")!=callbacks||bpf_object__load(obj))goto out;
	if(diag){cfgfd=bpf_map__fd(cm);statfd=bpf_map__fd(sm);taskfd=bpf_map__fd(tm);if(arm(cfgfd,statfd,taskfd,&cfg,n))goto out;}
	link=bpf_map__attach_struct_ops(ops); if(!link||libbpf_get_error(link)){link=NULL;goto out;}
	if(!await_state("enabled"))goto out;
	}
	if(graph_mode){markerfd=open("/sys/kernel/tracing/trace_marker",O_WRONLY|O_CLOEXEC);if(markerfd<0)goto out;cfg.trace=1;}
	stage="operations";
	uint64_t current=aid;
	for(int i=negative?-1:0;i<rounds+warmup;i++){
		if(i==warmup)measured_begin=now();
		if(pace_ms)pause_ms(pace_ms);
		struct result *r=&rows[completed]; int op=completed+1; bool rejected=i<0;
		cfg.armed=1; cfg.op=(unsigned)op; cfg.from=current; cfg.to=family==3?((same||current==bid)?aid:bid):aid;
		cfg.weight=(same||i%2)?100:200; cfg.deny_after=rejected?3:0;
		if(expired||!state(disabled?"disabled":"enabled")||arm(cfgfd,statfd,taskfd,&cfg,n)||marker(markerfd,"BEGIN",op,family))goto out;
		r->from=cfg.from;r->to=cfg.to;r->weight=(int)cfg.weight;r->rejected=rejected;
		if(family==1){
			uint64_t cycle_start=now(),start=cycle_start; int rc=mkdir(leaf,0755); r->first=now()-start; if(rc)goto out; leaf_created=true;
			start=now(); r->id=cgid(leaf); r->stat_ns=now()-start;if(!r->id)goto out;
			start=now(); rc=rmdir(leaf); uint64_t cycle_end=now(); r->second=cycle_end-start; r->cycle_ns=cycle_end-cycle_start;if(rc)goto out;leaf_created=false;
			if(marker(markerfd,"END",op,family))goto out;
			struct stat removed;errno=0;if(stat(leaf,&removed)!=-1||errno!=ENOENT)goto out;
			if(diag&&callbacks){uint64_t end=now()+2000000000ULL;do{if(snapshot(statfd,taskfd,r,n))goto out;if(r->stats.exit)break;pause_ms(1);}while(now()<end&&!expired);}
		}else{
			int fd=family==2?weightfd:(cfg.to==aid?afd:bfd);char text[32];
			if(family==2)snprintf(text,sizeof(text),"%u",cfg.weight);else snprintf(text,sizeof(text),"%d",child);
			if(lseek(fd,0,SEEK_SET)<0)goto out;
			size_t bytes=strlen(text);errno=0;
			uint64_t start=now();ssize_t rc=write(fd,text,bytes);int err=errno;r->first=now()-start;
			if(rejected){if(rc!=-1||err!=EPERM)goto out;}else if(rc!=(ssize_t)bytes)goto out;
			if(family==2){if(value(weightfd)!=(int)cfg.weight)goto out;}
			else {if(!rejected)current=cfg.to;if(!all_in(n,current==aid?arel:brel))goto out;}
		}
		if(snapshot(statfd,taskfd,r,n))goto out;
		if(diag){
			unsigned expect=callbacks&&!same?(unsigned)n:0;
			if(family==1&&(r->stats.init!=(unsigned)callbacks||r->stats.exit!=(unsigned)callbacks||
			   (callbacks&&(r->stats.created_id!=r->id||r->stats.exited_id!=r->id))))goto out;
			if(family==2&&(r->stats.weight!=(unsigned)(callbacks&&!same)||
			   (callbacks&&!same&&(r->stats.weight_id!=aid||r->stats.weight_value!=cfg.weight))))goto out;
			if(family==3){
				if(r->stats.prep!=(rejected?3:expect)||r->stats.move!=(rejected?0:expect)||r->stats.cancel!=(rejected?2:0)||r->stats.denied!=(unsigned)rejected)goto out;
				for(int j=0;j<n;j++){struct cg_task *t=&r->tasks[j];
					if(rejected){if(t->move||t->prep!=t->cancel+t->denied)goto out;}
					else if(t->prep!=(unsigned)(callbacks&&!same)||t->move!=t->prep||t->cancel||t->denied)goto out;
					if(t->prep&&(t->from!=cfg.from||t->to!=cfg.to))goto out;
				}
			}
		}
		completed++;
	}
	workload_elapsed_ns=now()-measured_begin;
	stage="restore";cfg.armed=0;if(arm(cfgfd,statfd,taskfd,&cfg,n))goto out;
	if(put(weightfd,"100")||value(weightfd)!=100)goto out;
	if(n&&(put(afd,pidtext)||!all_in(n,arel)))goto out;
	if(n){__atomic_store_n(&sh->stop,1,__ATOMIC_RELEASE);syscall(SYS_futex,&sh->stop,FUTEX_WAKE,CG_MAX_TASKS,NULL,NULL,0);
		int status;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))goto out;child_reaped=true;}
	if(!disabled){
	if(bpf_link__destroy(link)){link=NULL;goto out;}link=NULL;
	if(!await_state("disabled")||bpf_map_lookup_elem(bpf_map__fd(safe),&zero,&safety)||safety.magic!=CG_MAGIC||
	   safety.initialized!=1||safety.exited!=1||safety.exit_kind!=64||safety.foreign)goto out;
	}else if(!state("disabled"))goto out;
	ret=0;
out:
	if(ret){struct result *failed=&rows[completed<rounds+warmup+1?completed:completed-1];
		fprintf(stderr,"CG_FAIL_BEFORE_CLEANUP stage=%s completed=%d errno=%d id=%llu observed_ids=%llu/%llu init=%u exit=%u prep=%u move=%u cancel=%u denied=%u errors=%u\n",
		        stage,completed,errno,(unsigned long long)failed->id,failed->stats.created_id,failed->stats.exited_id,
		        failed->stats.init,failed->stats.exit,failed->stats.prep,failed->stats.move,failed->stats.cancel,failed->stats.denied,failed->stats.errors);}
	if(cfgfd>=0){cfg.armed=0;(void)bpf_map_update_elem(cfgfd,&zero,&cfg,BPF_ANY);}
	if(child>0&&!child_reaped){kill(child,SIGKILL);while(waitpid(child,NULL,0)<0&&errno==EINTR){}}
	if(link)bpf_link__destroy(link);
	if(obj)bpf_object__close(obj);
	if(!await_state("disabled"))ret=1;
	if(markerfd>=0)close(markerfd);
	if(afd>=0)close(afd);
	if(bfd>=0)close(bfd);
	if(weightfd>=0)close(weightfd);
	if(leaf_created&&rmdir(leaf))ret=1;
	if(a_created&&rmdir(a))ret=1;
	if(b_created&&rmdir(b))ret=1;
	if(root_created&&rmdir(root))ret=1;
	if(ret){fprintf(stderr,"CG_FAIL stage=%s completed=%d errno=%d expired=%d\n",stage,completed,errno,expired);free(rows);return ret;}
	printf("{\"schema\":\"cgroup-kernfs-inode-inited-cycle-v1\",\"scx_disabled\":%s,\"family\":%d,\"actor\":%d,\"n\":%d,\"same\":%d,\"callbacks\":%d,\"diagnostic\":%d,\"rounds\":%d,\"warmup\":%d,\"trace\":%s,\"root\":\"%s\",\"root_id\":%llu,\"a_id\":%llu,\"b_id\":%llu,\"restored\":true,\"exit_kind\":%d,\"tids\":[",
	       disabled?"true":"false",family,getpid(),n,same,callbacks,diag,rounds,warmup,graph_mode?"true":"false",root,
	       cfg.root,(unsigned long long)aid,(unsigned long long)bid,safety.exit_kind);
	for(int j=0;j<n;j++)printf("%s%d",j?",":"",sh->tids[j]);
	printf("],\"pace_ms\":%u,\"workload_elapsed_ns\":%llu,\"rows\":[",pace_ms,(unsigned long long)workload_elapsed_ns);
	for(int i=0;i<completed;i++){struct result *r=&rows[i];
		printf("%s{\"op\":%d,\"warmup\":%s,\"rejected\":%s,\"first_ns\":%llu,\"second_ns\":%llu,\"stat_ns\":%llu,\"cycle_ns\":%llu,\"id\":%llu,\"from\":%llu,\"to\":%llu,\"weight\":%d,\"init\":%u,\"exit\":%u,\"set_weight\":%u,\"prep\":%u,\"move\":%u,\"cancel\":%u,\"denied\":%u,\"errors\":%u,\"verified\":true,\"pairs\":[",
		       i?",":"",i+1,i-(int)negative<warmup?"true":"false",r->rejected?"true":"false",
		       (unsigned long long)r->first,(unsigned long long)r->second,(unsigned long long)r->stat_ns,(unsigned long long)r->cycle_ns,(unsigned long long)r->id,
		       (unsigned long long)r->from,(unsigned long long)r->to,r->weight,r->stats.init,r->stats.exit,r->stats.weight,
		       r->stats.prep,r->stats.move,r->stats.cancel,r->stats.denied,r->stats.errors);
		if(diag)for(int j=0;j<n;j++){struct cg_task *t=&r->tasks[j];printf("%s{\"pid\":%d,\"prep\":%u,\"move\":%u,\"cancel\":%u,\"denied\":%u,\"from\":%llu,\"to\":%llu}",j?",":"",sh->tids[j],t->prep,t->move,t->cancel,t->denied,t->from,t->to);}
		printf("]}");
	}
	printf("]}\n");if(sh)munmap(sh,sizeof(*sh));free(rows);return 0;
}
