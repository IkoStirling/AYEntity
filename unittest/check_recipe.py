"""One finite Stage71 CLI integration check, to be executed by the root owner after installation."""
import argparse,copy,datetime,hashlib,json,secrets,subprocess,sys
from pathlib import Path
def check(ok,why):
    if not ok:raise ValueError(why)
def info(path):
    raw=path.read_bytes();return {'path':str(path.absolute()),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def put(path,value):
    with path.open('x',encoding='utf-8',newline='\n') as f:json.dump(value,f,indent=2);f.write('\n')
def main():
    check(__debug__,'-O forbidden');ap=argparse.ArgumentParser(description=__doc__)
    for name in ('runner','tool','examples','output-parent'):ap.add_argument('--'+name,required=True,type=Path)
    ap.add_argument('--tool-sha256',required=True);a=ap.parse_args()
    check(a.output_parent.is_dir(),'existing output parent required')
    out=a.output_parent/('recipe-check-'+secrets.token_hex(6));out.mkdir(exist_ok=False)
    source_files=sorted(a.examples.iterdir());before={p.name:info(p) for p in source_files if p.is_file()}
    report={'schema':1,'status':'failed','scope':'two finite offline recipes, whole canonical/SVG bytes and bounded refusal/partial-output checks; no Sim, backend matrix or authentication',
        'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'runs':[],'negativeChecks':[],'sourcesBefore':before}
    def run(manifest,parent,name,expected):
        args=[sys.executable,str(a.runner),'--tool',str(a.tool),'--tool-sha256',a.tool_sha256,'--manifest',str(manifest),'--output-parent',str(parent)]
        value=subprocess.run(args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,shell=False,timeout=600)
        (out/(name+'.stdout.bin')).write_bytes(value.stdout);(out/(name+'.stderr.bin')).write_bytes(value.stderr)
        check(len(value.stdout)+len(value.stderr)<=2*1024*1024,'wrapper output bound')
        check(value.returncode==expected,'unexpected wrapper exit: '+name)
        return value
    try:
        receipts={}
        for name in ('terrain-platforms','oneway-filter'):
            parent=out/name;parent.mkdir();v=run(a.examples/(name+'.json'),parent,name,0)
            check(not v.stderr,'successful recipe stderr')
            result=json.loads(v.stdout);receipt=Path(result['receipt']);check(receipt.resolve().is_relative_to(parent.resolve()),'actual recipe receipt boundary')
            r=json.loads(receipt.read_text());check(r['status']=='passed','actual recipe not passed');receipts[name]=r
            rows={s['id']:s for s in r['steps']};check(all(s['exitCode']==0 and not s['timedOut'] and not s['logErrors'] for s in rows.values()),'actual CLI failures')
            left='shared' if name=='terrain-platforms' else 'filtered'
            p=Path(rows[left]['output']['path']);q=Path(rows['canonical']['output']['path']);check(p.read_bytes()==q.read_bytes(),'complete canonical bytes')
            p=Path(rows['beforePreview']['output']['path']);q=Path(rows['preview']['output']['path']);check(p.read_bytes()==q.read_bytes(),'complete initial preview bytes')
            polygons={p['data-actor']:p for p in rows['preview']['preview']['polygons']}
            if name=='terrain-platforms':
                check(polygons['200']['points']=='3,0 5,0 5,1' and polygons['200']['data-kind']=='triangle','actual filled wedge vertices')
                check(polygons['300']['data-mode']=='3' and polygons['301']['data-mode']=='3','actual two prescribed bodies')
                paths=rows['preview']['preview']['paths'];check(len(paths)==2,'two actual actor-relative path previews')
                check({p['points'] for p in paths}=={'8,2 8,3','10,2 10,3'},'actual shared path with separate origins')
                check(rows['terrain']['contentIdentity']['formatVersion']==1 and rows['canonical']['contentIdentity']['formatVersion']==1,'no format promotion')
            else:
                check(polygons['200']['points']=='11,1.875 13,1.875 13,2.125 11,2.125','actual one-way box vertices')
                check(polygons['200']['data-role']=='one-way' and polygons['200']['data-mask']=='0','mask zero preserves authored proxy geometry')
                check(rows['validated']['contentIdentity']['formatVersion']==10 and rows['canonical']['contentIdentity']['formatVersion']==10,'one-way source version remains explicit')
                check(rows['oneway']['contentIdentity']['contentHash']!=rows['filtered']['contentIdentity']['contentHash'],'filter enters actual content identity')
            report['runs'].append({'name':name,'receipt':info(receipt),'actualCLICommands':len(rows),'canonicalWholeBytesEqual':True,'previewWholeBytesEqual':True})
        base=json.loads((a.examples/'terrain-platforms.json').read_text())
        negatives=[]
        v=copy.deepcopy(base);v['steps'][0]['command']='shell';negatives.append(('unknown-command',v,'unknown authoring command'))
        v=copy.deepcopy(base);v['steps'][0]['output']='C:/escape.dlt';negatives.append(('absolute-output',v,'fresh leaf output'))
        v=copy.deepcopy(base);v['steps'][1]['output']='TERRAIN.dlt';negatives.append(('casefold-overwrite',v,'output overwrite/casefold collision'))
        for name,value,message in negatives:
            case=out/name;case.mkdir();parent=case/'outputs';parent.mkdir();manifest=case/'recipe.json';put(manifest,value)
            result=run(manifest,parent,name,1);check(not list(parent.iterdir()),'preflight refusal created an output directory')
            check(message in json.loads(result.stderr)['error'],'wrong preflight refusal: '+name)
            report['negativeChecks'].append({'name':name,'exitCode':1,'noFreshChild':True})
        case=out/'changed-source';case.mkdir();parent=case/'outputs';parent.mkdir()
        v=json.loads((a.examples/'oneway-filter.json').read_text());name=v['sources'][0]['path']
        (case/name).write_bytes((a.examples/name).read_bytes()+b'\n');put(case/'recipe.json',v)
        result=run(case/'recipe.json',parent,'changed-source',1)
        check(not list(parent.iterdir()) and 'changed source binding' in json.loads(result.stderr)['error'],'changed source not refused before any command')
        report['negativeChecks'].append({'name':'changed-source','exitCode':1,'noFreshChild':True})
        case=out/'oversized';case.mkdir();parent=case/'outputs';parent.mkdir();(case/'recipe.json').write_bytes(b' '*(1024*1024+1))
        result=run(case/'recipe.json',parent,'oversized',1)
        check(not list(parent.iterdir()) and 'manifest1MiB bound before read' in json.loads(result.stderr)['error'],'oversized manifest not refused before parsing/commands')
        report['negativeChecks'].append({'name':'oversized','exitCode':1,'noFreshChild':True})
        # Actual CLI refusal after one successful fresh create must preserve it and not execute the suffix.
        case=out/'partial';case.mkdir();parent=case/'outputs';parent.mkdir();v=copy.deepcopy(base)
        v['steps']=v['steps'][:3];v['steps'][1]['params']['halfX']='1e9999';put(case/'recipe.json',v)
        run(case/'recipe.json',parent,'partial',1);children=list(parent.iterdir());check(len(children)==1,'one failed fresh recipe child')
        failure=children[0]/'recipe-failure.json';r=json.loads(failure.read_text())
        check(len(r['steps'])==2 and r['steps'][0]['exitCode']==0 and r['steps'][1]['exitCode']!=0,'actual failed command/stop scope')
        check((children[0]/'terrain.dlt').is_file() and not(children[0]/'platform-a.dlt').exists(),'partial output preservation and stopped suffix')
        report['partialFailure']={'receipt':info(failure),'expectedActualCLIFailure':True,'completedPrefixRetained':True,'suffixNotExecuted':True}
        after={p.name:info(p) for p in source_files if p.is_file()};check(after==before,'bound example sources changed')
        report['sourcesAfter']=after;report['status']='passed';report['finishedUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
        put(out/'recipe-check-result.json',report);print(json.dumps({'status':'passed','receipt':str(out/'recipe-check-result.json')}));return 0
    except Exception as e:
        report['error']=str(e);put(out/'recipe-check-failure.json',report);raise
if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as e:print(str(e),file=sys.stderr);raise SystemExit(1)
