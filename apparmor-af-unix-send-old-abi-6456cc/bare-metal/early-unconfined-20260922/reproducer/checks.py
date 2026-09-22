#!/usr/bin/env python3
"""Own-profile live socket controls and confined sendmmsg measurements."""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import time

PREFIX='ks_aa_extra2_20260922_'
CASES={'unconfined':('none','none'),'sender-confined':('allow_tx','none'),
       'receiver-confined':('none','allow_rx'),'both-confined':('allow_tx','allow_rx')}
PROFILES=['allow_tx','allow_rx','live_tx','live_rx','guard']
SCENARIOS=[('sender-born-confined','tx','allow'),('sender-born-unconfined','tx','unconfined-deny'),
           ('receiver-born-confined','rx','allow'),('receiver-born-unconfined','rx','unconfined-deny')]
PHASES=['allow','deny','allow','unconfined-deny','deny','unconfined-deny','allow']

def require(ok,msg):
    if not ok: raise RuntimeError(msg)

def save(path,obj):
    path.write_text(json.dumps(obj,indent=2)+'\n')

def policy(name,state='allow',deny='send'):
    require(name in PROFILES and state in ('allow','deny','unconfined-deny'),'policy scope')
    flags='unconfined' if state=='unconfined-deny' else 'attach_disconnected'
    rule=f'deny unix ({deny}),' if state!='allow' else ''
    return f'abi <abi/5.0>,\nprofile {PREFIX}{name} flags=({flags}) {{\n file,\n unix,\n signal,\n capability setuid,\n capability setgid,\n {rule}\n}}\n'

def argv(base,tx,rx,mode):
    label=lambda n:n if n=='none' else PREFIX+n
    return [str(base/'af_unix_live'),label(tx),label(rx),mode]

class Policies:
    def __init__(self,out):
        require(os.geteuid()==0,'root needed only for isolated policy administration')
        require(Path('/proc/self/attr/current').read_text().strip()=='unconfined','controller confined')
        self.out=out; self.profile_list=Path('/sys/kernel/security/apparmor/profiles')
        self.before=sorted(self.profile_list.read_text().splitlines()); self.loaded=[]; self.events=[]
        require(not any(PREFIX in p for p in self.before),'test prefix in use')
        (out/'profiles-before.txt').write_text('\n'.join(self.before)+'\n')
        features=Path('/sys/kernel/security/apparmor/features')
        require((features/'network_v9/af_unix').read_text().strip()=='yes','UNIX v9 ABI absent')
        (out/'abi-5.0.txt').write_bytes(Path('/etc/apparmor.d/abi/5.0').read_bytes())
        save(out/'features.json',{str(p.relative_to(features)):p.read_text().strip() for p in features.rglob('*') if p.is_file()})
    def load(self,name,state='allow',deny='send'):
        action='-r' if name in self.loaded else '-a'
        if name not in self.loaded: self.loaded.append(name)
        path=self.out/f'{len(self.events):03d}-{name}-{state}.profile'; path.write_text(policy(name,state,deny))
        start=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        p=subprocess.run(['/usr/sbin/apparmor_parser','-K','-j','1',action,str(path)],capture_output=True,text=True,timeout=15)
        end=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        event=dict(name=name,state=state,deny=deny,action=action,start_ns=start,end_ns=end,
                   returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,path=path.name)
        self.events.append(event); save(self.out/'parser.json',self.events)
        require(p.returncode==0,'policy load failed: '+p.stderr)
        mode='unconfined' if state=='unconfined-deny' else 'enforce'
        require(f'{PREFIX}{name} ({mode})' in self.profile_list.read_text().splitlines(),'loaded mode differs')
        return event
    def close(self):
        errors=[]
        for name in reversed(self.loaded):
            if not any(p.startswith(PREFIX+name+' (') for p in self.profile_list.read_text().splitlines()): continue
            path=self.out/('remove-'+name+'.profile'); path.write_text(policy(name))
            p=subprocess.run(['/usr/sbin/apparmor_parser','-K','-j','1','-R',str(path)],capture_output=True,text=True,timeout=15)
            if p.returncode: errors.append(p.stderr)
        after=sorted(self.profile_list.read_text().splitlines())
        (self.out/'profiles-after.txt').write_text('\n'.join(after)+'\n')
        require(not errors and after==self.before,'test policy restoration differs: '+str(errors))

class Actor:
    def __init__(self,base,out,tx,rx):
        self.path=out; self.path.mkdir(); self.err=(out/'stderr.log').open('w')
        self.p=subprocess.Popen(argv(base,tx,rx,'interactive'),stdin=subprocess.PIPE,stdout=subprocess.PIPE,
            stderr=self.err,text=True,bufsize=1,start_new_session=True)
        self.events=[]
        try:
            self.ready=self.read('ready'); require(self.ready['uid']==1000,'actor uid differs')
        except BaseException:
            self.close(force=True); raise
    def read(self,kind,timeout=15):
        require(bool(select.select([self.p.stdout],[],[],timeout)[0]),'actor output timeout')
        text=self.p.stdout.readline(); require(text,'actor stopped: '+(self.path/'stderr.log').read_text())
        obj=json.loads(text); self.events.append(obj); save(self.path/'protocol.json',self.events)
        require(obj['kind']==kind,'unexpected actor output: '+str(obj)); return obj
    def send(self,text): self.p.stdin.write(text+'\n'); self.p.stdin.flush()
    def command(self,text,kind=None): self.send(text); return self.read(kind or text)
    def context(self):
        return {side:Path(f'/proc/{self.ready[key]}/attr/current').read_text().strip()
                for side,key in [('tx','pid'),('rx','rx_pid')]}
    def close(self,force=False):
        if self.p.poll() is None:
            if not force:
                try:
                    self.send('quit'); self.read('complete'); self.p.wait(timeout=5)
                except BaseException:
                    os.killpg(self.p.pid,signal.SIGKILL); self.p.wait(timeout=5); self.err.close(); raise
            else:
                os.killpg(self.p.pid,signal.SIGKILL); self.p.wait(timeout=5)
        self.err.close()
        if not force: require(self.p.returncode==0,'actor failed')

def decision(row,side,state,strict_allow=True,peer_snapshot=False):
    allow=state!='deny'
    if peer_snapshot:
        # unix_may_send uses the receiver socket's stored label, not the
        # receiver task's newest label. PF_UNIX recvmsg itself returns 0 in
        # aa_sock_msg_perm. Record and compare this snapshot behavior; do not
        # claim that replacing the peer task's policy revoked that socket.
        delivered=row['send_errno']==0 and row['received']==1 and row['receive_errno']==0 and row['pending']==0
        permission_denied=row['received']==0 and (row['send_errno'] in (1,13) or row['receive_errno'] in (1,13))
        require(side=='rx' and (delivered or permission_denied),'invalid peer snapshot observation')
    elif allow:
        delivered=row['send_errno']==0 and row['received']==1 and row['receive_errno']==0 and row['pending']==0
        permission_denied=row['received']==0 and (row['send_errno'] in (1,13) or row['receive_errno'] in (1,13))
        require(delivered or (not strict_allow and permission_denied),'expected delivery or explicit baseline-relative permission observation')
    elif side=='tx':
        require(row['send_errno'] in (1,13) and row['received']==0 and row['pending']==0,'sender deny failed')
    else:
        # An existing peer label may first permit queuing while the receiver's
        # updated profile blocks delivery. Do not mislabel that as a bypass.
        require(row['received']==0 and (row['send_errno'] in (1,13) or row['receive_errno'] in (1,13)),
                'receiver deny did not prevent delivery')
    return {k:row[k] for k in ['send_errno','received','receive_errno','pending']}

def correctness(base,out,policies):
    results=[]
    for name,side,initial in SCENARIOS:
        label='live_'+side; policies.load(label,initial,'send' if side=='tx' else 'receive')
        actor=Actor(base,out/name,label if side=='tx' else 'none',label if side=='rx' else 'none')
        record=dict(name=name,ready=actor.ready,checks=[],contract='current-sender-policy' if side=='tx' else 'receiver-socket-snapshot-equivalence'); results.append(record)
        try:
            for phase,state in enumerate(PHASES):
                policies.load(label,state,'send' if side=='tx' else 'receive')
                flushed=actor.command('flush')
                record.setdefault('flushes',[]).append(dict(phase=phase,state=state,**flushed))
                require(side!='tx' or state!='deny' or flushed['received']==0,'queued packet delivered in sender enforced-deny phase')
                if flushed['pending']:
                    record['checks'].append(dict(phase=phase,state=state,skipped='prior queued message still denied; compare baseline'))
                    continue
                for repetition in range(3):
                    row=actor.command('check')
                    result=decision(row,side,state,peer_snapshot=side=='rx' and len(record['checks'])>0)
                    require(all(row[k]==actor.ready[k] for k in ['tx_inode','rx_inode']),'socket replaced')
                    record['checks'].append(dict(phase=phase,state=state,repetition=repetition,decision=result,contexts=actor.context()))
                    # Only one blocked queued packet is retained; subsequent
                    # checks resume after the next allow phase has drained it.
                    if row['pending']: break
            record['final_flush']=actor.command('flush'); actor.close(); record['status']='observed-pending-baseline-comparison'
        except BaseException:
            actor.close(force=True); save(out/'correctness.json',results); raise
        save(out/'correctness.json',results)
    # Sender-mode replacement overlaps sends. A second control changes the
    # receiving label while a permanently denying sending label must prevail.
    for name,guard in [('concurrent-sender',False),('concurrent-peer-with-sender-deny',True)]:
        policies.load('live_tx'); policies.load('live_rx'); policies.load('guard')
        actor=Actor(base,out/name,'guard' if guard else 'live_tx','live_rx' if guard else 'none')
        record=dict(name=name,ready=actor.ready); results.append(record)
        try:
            if guard: policies.load('guard','deny','send')
            actor.send('race'); begin=actor.read('race-start'); changes=[]
            time.sleep(.05)
            for state in ['unconfined-deny','deny','allow','deny']*4:
                require(actor.p.poll() is None,'actor exited during replacement')
                # Peer variation never denies receive; only mode changes here.
                changes.append(policies.load('live_rx' if guard else 'live_tx',state,'send'))
                time.sleep(.02)
            end=actor.read('race-end')
            require(begin['time_ns']==end['start_ns'] and end['end_ns']-end['start_ns']>=1900000000,'race duration short')
            require(all(begin['time_ns']<x['start_ns']<x['end_ns']<end['end_ns'] for x in changes),'policy replacement did not overlap sends')
            require(end['denied']>0 and (end['allowed']==0 if guard else end['allowed']>0),'race did not exercise expected allowed/denied states')
            record.update(begin=begin,end=end,changes=changes,after_deny=decision(actor.command('check'),'tx','deny'))
            policies.load('guard' if guard else 'live_tx','allow')
            # In the peer control, live_rx's send-only deny would stop its
            # receive-side fd revalidation; restore it before the allow check.
            if guard: policies.load('live_rx','allow')
            record['after_allow']=decision(actor.command('check'),'tx','allow',strict_allow=False)
            actor.close(); record['status']='pass'
        except BaseException:
            actor.close(force=True); save(out/'correctness.json',results); raise
        save(out/'correctness.json',results)
    return results

def parse_measurements(text,messages,warmups,rounds):
    records=[json.loads(line) for line in text.splitlines() if line.startswith('{')]
    ready=[x for x in records if x['kind']=='ready']; data=[x for x in records if x['kind']=='measurement']
    require(len(ready)==1 and ready[0]['uid']==1000 and len(data)==warmups+rounds,'incomplete measurements')
    require(records[-1]==dict(kind='complete',status='pass'),'completion absent')
    for i,r in enumerate(data):
        require(r['round']==i and r['phase']==('warmup' if i<warmups else 'measured') and
            r['messages']==messages and r['batch']==32 and r['payload']==128 and r['elapsed_ns']>0 and r['semantic_pass'],
            'measurement mismatch')
    return ready[0],data

def native_run(base,out,case,mode):
    p=subprocess.run(argv(base,*CASES[case],mode),capture_output=True,text=True,timeout=120)
    (out.with_suffix('.stdout.log')).write_text(p.stdout); (out.with_suffix('.stderr.log')).write_text(p.stderr)
    require(p.returncode==0,'native execution failed: '+p.stderr)
    ready,rows=parse_measurements(p.stdout,1024 if mode=='probe' else 65536,0 if mode=='probe' else 3,1 if mode=='probe' else 15)
    return dict(case=case,mode=mode,ready=ready,rows=rows)

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,required=True); p.add_argument('--static-only',action='store_true'); a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False); base=Path(__file__).resolve().parent
    policies=Policies(a.output); state=dict(status='incomplete')
    try:
        for name in PROFILES: policies.load(name)
        if not a.static_only: state['correctness']=correctness(base,a.output,policies)
        state['smokes']=[native_run(base,a.output/('smoke-'+case),case,'probe') for case in CASES]
        state['status']='pass'
    except BaseException as error:
        state.update(status='fail',error=repr(error)); raise
    finally:
        try: policies.close(); state['profiles_restored']=True
        except BaseException as error: state.update(status='fail',profiles_restored=False,cleanup_error=repr(error))
        save(a.output/'result.json',state)
