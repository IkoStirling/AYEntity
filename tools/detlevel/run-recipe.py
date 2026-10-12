"""Run a bounded offline authoring recipe through one explicitly pinned aydetlevel CLI."""
from __future__ import annotations
import argparse,datetime,hashlib,json,os,re,secrets,subprocess,sys,threading,time
from pathlib import Path
import xml.etree.ElementTree as ET

MAX_MANIFEST=1024*1024
MAX_STEPS=64
MAX_LOG=2*1024*1024
MAX_TOTAL=64*1024*1024
COMMANDS={
 'create':({'starter'},True),
 'place':({'input','actor','x','y'},True),
 'resize-half':({'input','actor','halfX','halfY'},True),
 'edit-offset':({'input','actor','x','y'},True),
 'edit-playback':({'input','path','playback'},True),
 'remove-solid':({'input','actor'},True),
 'add-solid':({'input','actor','shape','x','y','halfX','halfY','layer','mask'},True),
 'add-oneway':({'input','actor','x','y','halfX','halfY','layer','mask'},True),
 'edit-filter':({'input','actor','layer','mask'},True),
 'add-path':({'input','actor','path','dx','dy','travelTicks','playback'},True),
 'bind-path':({'input','actor','path'},True),
 'edit-waypoint':({'input','path','index','dx','dy','travelTicks','dwellTicks'},True),
 'stamp-solids':({'donor','target','shiftX','shiftY','mapping'},True),
 'canonicalize':({'input'},True),'preview':({'input'},True),
 'validate':({'input'},False),'inspect':({'input'},False)}
REF_KEYS={'input','donor','target'}
DECIMAL_KEYS={'x','y','halfX','halfY','dx','dy','shiftX','shiftY'}
def check(ok,why):
    if not ok:raise ValueError(why)
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def ordinary(path,is_dir=False):
    check(path.is_dir() if is_dir else path.is_file(),'ordinary existing '+('directory' if is_dir else 'file')+': '+str(path))
    for p in [path,*path.parents]:
        if p.exists():
            st=p.lstat();check(not p.is_symlink() and not(getattr(st,'st_file_attributes',0)&0x400),'link/junction forbidden: '+str(p))
            if p==path and not is_dir:check(getattr(st,'st_nlink',1)==1,'hard-linked file forbidden: '+str(p))
def fingerprint(path,maximum=MAX_TOTAL):
    ordinary(path);check(0<=path.stat().st_size<=maximum,'file byte bound before read: '+str(path));raw=path.read_bytes()
    check(len(raw)<=maximum,'file grew past byte bound during read: '+str(path))
    return {'path':str(path.absolute()),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def write_json(path,value):
    raw=(json.dumps(value,ensure_ascii=True,indent=2)+'\n').encode('utf-8');check(len(raw)<=8*1024*1024,'receipt8MiB bound')
    with path.open('xb') as stream:stream.write(raw)
def partial_inventory(run):
    result={}
    for path in run.iterdir():
        if not path.is_file():continue
        try:result[path.name]=fingerprint(path)
        except Exception as e:result[path.name]={'path':str(path.absolute()),'bytes':path.stat().st_size,'sha256':None,'inspectionError':str(e)}
    return result
def exact(obj,keys,where):check(type(obj) is dict and set(obj)==set(keys),'exact fields required: '+where)
def word(value,maximum,nonzero=False):
    check(type(value) is str and re.fullmatch(r'0|[1-9][0-9]{0,19}',value) is not None,'canonical decimal integer string')
    number=int(value);check((1 if nonzero else 0)<=number<=maximum,'integer range');return value
def integer(value,low,high):check(type(value) is int and low<=value<=high,'exact bounded integer');return str(value)
def identifier(value):check(type(value) is str and re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,47}',value) is not None,'bounded ID');return value
def leaf(value,suffix):
    check(type(value) is str and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}',value) is not None and value.endswith(suffix),'fresh leaf output '+suffix)
    check(value not in ('.','..') and value.split('.',1)[0].upper() not in {'CON','PRN','AUX','NUL',*(f'COM{n}' for n in range(1,10)),*(f'LPT{n}' for n in range(1,10))},'reserved output name');return value
def relative_source(value):
    check(type(value) is str and 1<=len(value)<=240 and '\\' not in value and ':' not in value and not value.startswith('/'),'relative source path only')
    parts=value.split('/');check(all(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}',p) is not None and p not in ('.','..') for p in parts),'source path segments')
    check(value.endswith('.dlt'),'source must be .dlt');return value
def decimal(value):
    check(type(value) is str and 1<=len(value)<=128 and re.fullmatch(r'[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?',value) is not None,
        'bounded software decimal token');return value
def no_duplicate_keys(pairs):
    result={}
    for key,value in pairs:check(key not in result,'duplicate JSON key');result[key]=value
    return result
def parse_manifest(path):
    ordinary(path);check(1<=path.stat().st_size<=MAX_MANIFEST,'manifest1MiB bound before read');raw=path.read_bytes();check(1<=len(raw)<=MAX_MANIFEST,'manifest1MiB bound')
    result=json.loads(raw.decode('utf-8'),object_pairs_hook=no_duplicate_keys,
        parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite JSON constant')))
    exact(result,{'schema','sources','steps'},'recipe')
    check(type(result['schema']) is int and result['schema']==1,'recipe schema1')
    check(type(result['sources']) is list and len(result['sources'])<=16,'at most16 bound sources')
    check(type(result['steps']) is list and 1<=len(result['steps'])<=MAX_STEPS,'1..64 steps')
    sources={};bindings={};source_paths=set()
    for row in result['sources']:
        exact(row,{'id','path','bytes','sha256'},'source binding');sid=identifier(row['id']);check(sid not in sources,'duplicate source ID')
        rel=relative_source(row['path']);check(rel.casefold() not in source_paths,'duplicate source path');source_paths.add(rel.casefold())
        check(type(row['bytes']) is int and 1<=row['bytes']<=65536,'source64KiB bound')
        check(type(row['sha256']) is str and re.fullmatch('[0-9a-f]{64}',row['sha256']) is not None,'full lower-case SHA256')
        source=(path.parent/rel).absolute();ordinary(source);check(source.resolve().is_relative_to(path.parent.resolve()),'source escapes manifest directory')
        meta=fingerprint(source,65536);check(meta['bytes']==row['bytes'] and meta['sha256']==row['sha256'],'changed source binding '+sid)
        sources[sid]=source;bindings[sid]=meta
    outputs={};ids=set();names=set()
    for row in result['steps']:
        check(type(row) is dict and type(row.get('command')) is str and row['command'] in COMMANDS,'unknown authoring command')
        command=row['command'];params,produces=COMMANDS[command]
        exact(row,{'id','command','params','output'} if produces else {'id','command','params'},'step')
        sid=identifier(row['id']);check(sid not in ids,'duplicate step ID');exact(row['params'],params,'command params')
        p=row['params']
        for key,value in p.items():
            if key in REF_KEYS:
                check(type(value) is dict and len(value)==1 and next(iter(value)) in ('source','step'),'typed input reference')
                kind=next(iter(value));rid=identifier(value[kind])
                check(rid in (sources if kind=='source' else outputs),'missing/forward/non-level input reference')
                if kind=='step':check(outputs[rid].endswith('.dlt'),'preview is not a Level input')
            elif key in DECIMAL_KEYS:decimal(value)
            elif key=='actor':word(value,2**64-1,True)
            elif key=='path':word(value,2**32-1,True)
            elif key in ('layer','mask'):word(value,2**32-1)
            elif key=='travelTicks':integer(value,1,65535)
            elif key=='dwellTicks':integer(value,0,65535)
            elif key=='index':integer(value,0,23)
            elif key=='shape':check(value in ('aabb','slope-left','slope-right'),'unknown filled shape')
            elif key=='starter':check(value in ('character','dash','terrain'),'unknown starter')
            elif key=='playback':check(value in ('once','loop','pingpong'),'unknown playback')
            elif key=='mapping':
                check(type(value) is list and 1<=len(value)<=16,'1..16 explicit mappings');old=set();new=set();tokens=[]
                for mapping in value:
                    exact(mapping,{'old','new'},'mapping');a=word(mapping['old'],2**64-1,True);b=word(mapping['new'],2**64-1,True)
                    check(a not in old and b not in new,'duplicate mapping identity');old.add(a);new.add(b);tokens.append(a+':'+b)
                check(len(','.join(tokens))<=1024,'mapping1024token bound')
        if produces:
            name=leaf(row['output'],'.svg' if command=='preview' else '.dlt');check(name.casefold() not in names,'output overwrite/casefold collision')
            names.add(name.casefold());outputs[sid]=name
        ids.add(sid)
    return result,sources,bindings
def argv_for(step,run,sources,produced):
    p=step['params'];c=step['command']
    def ref(key):
        val=p[key];kind=next(iter(val));return str((sources if kind=='source' else produced)[val[kind]])
    if c=='create':args=[c,p['starter']]
    elif c=='add-solid':args=[c,ref('input'),p['actor'],p['shape'],p['x'],p['y'],p['halfX'],p['halfY'],p['layer'],p['mask']]
    elif c=='add-oneway':args=[c,ref('input'),p['actor'],p['x'],p['y'],p['halfX'],p['halfY'],p['layer'],p['mask']]
    elif c=='edit-filter':args=[c,ref('input'),p['actor'],p['layer'],p['mask']]
    elif c in ('place','edit-offset'):args=[c,ref('input'),p['actor'],p['x'],p['y']]
    elif c=='edit-playback':args=[c,ref('input'),p['path'],p['playback']]
    elif c=='remove-solid':args=[c,ref('input'),p['actor']]
    elif c=='resize-half':args=[c,ref('input'),p['actor'],p['halfX'],p['halfY']]
    elif c=='add-path':args=[c,ref('input'),p['actor'],p['path'],p['dx'],p['dy'],str(p['travelTicks']),p['playback']]
    elif c=='bind-path':args=[c,ref('input'),p['actor'],p['path']]
    elif c=='edit-waypoint':args=[c,ref('input'),p['path'],str(p['index']),p['dx'],p['dy'],str(p['travelTicks']),str(p['dwellTicks'])]
    elif c=='stamp-solids':args=[c,ref('donor'),ref('target'),p['shiftX'],p['shiftY'],','.join(v['old']+':'+v['new'] for v in p['mapping'])]
    else:args=[c,ref('input')]
    if 'output' in step:args.append(str(run/step['output']))
    return args
def execute(tool,argv,run,index,sid,remaining):
    check(remaining>=2,'aggregate log budget exhausted');limit=min(MAX_LOG,remaining//2)
    paths=[run/f'{index:02d}-{sid}.stdout.bin',run/f'{index:02d}-{sid}.stderr.bin'];handles=[p.open('xb') for p in paths]
    launch=utc();start=time.monotonic();p=None;threads=[];errors=[];timed=False
    try:
        p=subprocess.Popen([str(tool),*argv],stdout=subprocess.PIPE,stderr=subprocess.PIPE,stdin=subprocess.DEVNULL,
            shell=False,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        def drain(pipe,stream):
            total=0
            try:
                while True:
                    chunk=pipe.read(65536)
                    if not chunk:break
                    room=limit-total;stream.write(chunk[:max(0,room)]);total+=len(chunk)
                    if total>limit:errors.append('bounded output log exceeded');p.kill();break
            except Exception as e:errors.append(str(e))
            finally:pipe.close();stream.flush()
        threads=[threading.Thread(target=drain,args=(pipe,stream),daemon=True) for pipe,stream in zip((p.stdout,p.stderr),handles)]
        for thread in threads:thread.start()
        try:code=p.wait(timeout=30)
        except subprocess.TimeoutExpired:timed=True;p.kill();code=p.wait()
        for thread in threads:thread.join(timeout=5);check(not thread.is_alive(),'authoring output pipe did not close')
    finally:
        for stream in handles:stream.close()
    return {'index':index,'id':sid,'argv':[str(tool),*argv],'launchObservedUtc':launch,'exitedObservedUtc':utc(),
        'steadyElapsedMs':(time.monotonic()-start)*1000,'exitCode':code,'timedOut':timed,'logErrors':errors,
        'stdout':fingerprint(paths[0]),'stderr':fingerprint(paths[1]),'osIdentityAuthenticated':False}
def preview_info(path):
    check(0<path.stat().st_size<=1024*1024,'preview1MiB bound before read');raw=path.read_bytes();check(0<len(raw)<=1024*1024,'preview1MiB bound')
    check(b'<!DOCTYPE' not in raw.upper() and b'<!ENTITY' not in raw.upper(),'SVG declarations forbidden')
    node=ET.fromstring(raw);check(node.tag=='{http://www.w3.org/2000/svg}svg','SVG root')
    for element in node.iter():
        check(element.tag.rsplit('}',1)[-1] not in ('script','foreignObject','image'),'no active/external SVG content')
        check(all(not k.lower().startswith('on') and k.rsplit('}',1)[-1]!='href' for k in element.attrib),'no active/external SVG attributes')
    ns={'s':'http://www.w3.org/2000/svg'}
    polygons=node.findall('.//s:polygon',ns);paths=node.findall('.//s:polyline',ns)
    check(len(polygons)<=64 and len(paths)<=64,'bounded Level preview elements')
    return {'polygons':[dict(p.attrib) for p in polygons],'paths':[dict(p.attrib) for p in paths],
        'simulationExecuted':False,'browserPixelsAuthoritative':False}
def main():
    check(__debug__,'-O forbidden');ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tool',required=True,type=Path);ap.add_argument('--tool-sha256',required=True)
    ap.add_argument('--manifest',required=True,type=Path);ap.add_argument('--output-parent',required=True,type=Path)
    a=ap.parse_args();ordinary(a.tool);check(a.tool.name.lower() in ('aydetlevel','aydetlevel.exe'),'only explicitly selected aydetlevel executable')
    check(re.fullmatch('[0-9a-f]{64}',a.tool_sha256) is not None,'full selected tool SHA required')
    tool=a.tool.absolute();tool_before=fingerprint(tool,256*1024*1024);check(tool_before['sha256']==a.tool_sha256,'selected CLI changed')
    ordinary(a.output_parent,True);parent=a.output_parent.absolute();manifest=a.manifest.absolute()
    recipe,sources,bindings=parse_manifest(manifest)
    # Manifest graph/commands/source hashes are fully preflighted before creating any fresh child.
    run=parent/('recipe-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+secrets.token_hex(6));run.mkdir(exist_ok=False)
    report={'schema':1,'status':'failed','startedUtc':utc(),'recipe':fingerprint(manifest),'toolBefore':tool_before,'sourcesBefore':bindings,
        'directory':str(run),'steps':[],'simulationExecuted':False,'nativeAcceptance':False,'authentication':False,
        'scope':'bounded offline initial content creation through selected CLI; no shell, arbitrary executable, simulation or multi-file atomicity'}
    produced={};produced_bindings={};bytes_total=0
    try:
        for index,row in enumerate(recipe['steps']):
            check(fingerprint(tool,256*1024*1024)==tool_before,'CLI changed before step')
            check(fingerprint(manifest)==report['recipe'],'recipe changed before step')
            for sid,path in sources.items():check(fingerprint(path,65536)==bindings[sid],'source changed before step')
            for sid,path in produced.items():check(fingerprint(path)==produced_bindings[sid],'prior output changed before step')
            if 'output' in row:check(not(run/row['output']).exists(),'fresh output already exists')
            args=argv_for(row,run,sources,produced);entry=execute(tool,args,run,index,row['id'],MAX_TOTAL-bytes_total);report['steps'].append(entry)
            bytes_total+=entry['stdout']['bytes']+entry['stderr']['bytes']
            check(entry['exitCode']==0 and not entry['timedOut'] and not entry['logErrors'],'actual command failed: '+row['id'])
            check(entry['stderr']['bytes']==0,'successful CLI emitted unexpected stderr')
            check(entry['stdout']['bytes']<=256*1024,'CLI JSON256KiB bound before parse')
            data=json.loads(Path(entry['stdout']['path']).read_text(encoding='utf-8'),object_pairs_hook=no_duplicate_keys,
                parse_constant=lambda _:(_ for _ in ()).throw(ValueError('nonfinite CLI JSON constant')))
            check(type(data) is dict and type(data.get('schema')) is int and data['schema']==1 and data.get('status')=='passed','CLI complete success JSON')
            content=data.get('contentHash');word(content,2**64-1)
            check(type(data.get('formatVersion')) is int and 1<=data['formatVersion']<=11,'actual CLI Level1..11 format required')
            entry['contentIdentity']={'contentHash':content,'formatVersion':data['formatVersion']}
            entry['cliFields']={key:data[key] for key in ('formatVersion','actorCount','pathCount','source','output','simulationExecuted','scope') if key in data}
            # Complete CLI JSON remains in its fingerprinted stdout file; avoid copying all actor/policy fields into every recipe receipt.
            if 'output' in row:
                path=run/row['output'];meta=fingerprint(path,1024*1024 if row['command']=='preview' else 65536);check(meta['bytes']>0,'produced file budget')
                produced[row['id']]=path;produced_bindings[row['id']]=meta;entry['output']=meta;bytes_total+=meta['bytes']
                if row['command']=='preview':entry['preview']=preview_info(path)
            check(bytes_total<=MAX_TOTAL,'aggregate recipe output/log64MiB budget')
        report['toolAfter']=fingerprint(tool,256*1024*1024);check(report['toolAfter']==tool_before,'tool changed after recipe')
        report['sourcesAfter']={sid:fingerprint(path,65536) for sid,path in sources.items()};check(report['sourcesAfter']==bindings,'source changed after recipe')
        check(fingerprint(manifest)==report['recipe'],'recipe changed after recipe')
        report['status']='passed';report['finishedUtc']=utc();report['outputAndLogBytes']=bytes_total
        write_json(run/'recipe-receipt.json',report)
        print(json.dumps({'status':'passed','receipt':str(run/'recipe-receipt.json'),'directory':str(run)}));return 0
    except BaseException as e:
        report['error']=str(e);report['finishedUtc']=utc();report['createdOutputs']=partial_inventory(run)
        write_json(run/'recipe-failure.json',report)
        raise
if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as e:print(json.dumps({'schema':1,'status':'failed','error':str(e)}),file=sys.stderr);raise SystemExit(1)
