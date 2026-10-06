"""Task-owned editor job runner; current interactive editor is not controlled."""
import json, os, time, traceback
import unreal
ROOT='S:/UE_WorkSpace/SilverChoir'
DIR=ROOT+'/Saved/SandboxStaticMeshRepair'
os.makedirs(DIR,exist_ok=True)
JOB=DIR+'/job.json';RESULT=DIR+'/job_result.json'
last_id=None;busy=False
def tick(delta):
    global last_id,busy
    if busy or not os.path.isfile(JOB):return
    try:
        with open(JOB,encoding='utf-8-sig') as f:job=json.load(f)
    except (OSError,ValueError):return
    if job['id']==last_id:return
    last_id=job['id'];busy=True
    result={'id':last_id,'ok':False,'pid':os.getpid(),'started':time.time()}
    try:
        path=os.path.realpath(job['script'])
        assert path.startswith(os.path.realpath(ROOT+'/Scripts')+os.sep)
        with open(path,encoding='utf-8-sig') as f:code=f.read()
        exec(compile(code,path,'exec'),{'__name__':'__main__','__file__':path})
        result['ok']=True
    except Exception:result['error']=traceback.format_exc();unreal.log_error(result['error'])
    finally:
        result['finished']=time.time()
        with open(RESULT,'w') as f:json.dump(result,f,indent=2)
        busy=False
handle=unreal.register_slate_post_tick_callback(tick)
with open(DIR+'/ready.json','w') as f:json.dump({'pid':os.getpid(),'ready':True},f)
