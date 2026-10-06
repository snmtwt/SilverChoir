"""Copy a verified dependency manifest, with backups and local-edit protection."""
import argparse,json,hashlib,shutil,datetime,os
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);p.add_argument('--target',required=True);p.add_argument('--apply',action='store_true');p.add_argument('--initial-import',action='store_true');args=p.parse_args()
manifest=json.loads(Path(args.manifest).read_text(encoding='utf8'))
assert manifest['status']=='PASS',manifest.get('errors')
source=Path(manifest['source_project']).resolve();target=Path(args.target).resolve()
assert source!=target and (target/'SilverChoir.uproject').is_file(),'Unexpected target project'
folder=target/'Saved/CharacterAnimationSync';folder.mkdir(parents=True,exist_ok=True)
state_file=folder/'LastSync.json';previous=json.loads(state_file.read_text(encoding='utf8')) if state_file.exists() else {}
backup=folder/'Backups'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
def sha(f):
 with f.open('rb') as handle:return hashlib.file_digest(handle,'sha256').hexdigest()
changes=[];conflicts=[]
tag_relative='Config/Tags/CharacterAnimationSync.ini'
tag_bytes=manifest.get('animation_tags','').encode('utf8')
tag_hash=hashlib.sha256(tag_bytes).hexdigest()
tag_target=target/tag_relative
tag_before=sha(tag_target) if tag_target.exists() else None
if tag_bytes and tag_before and tag_before!=tag_hash and not args.initial_import and previous.get(tag_relative)!=tag_before:conflicts.append(tag_relative)
for record in manifest['files']:
 rel=Path(record['relative']);a=(source/rel).resolve();b=(target/rel).resolve()
 assert a.is_relative_to(source/'Content') and b.is_relative_to(target/'Content'),str(rel)
 assert a.is_file() and sha(a)==record['sha256'],'Source changed after export: '+str(rel)
 before=sha(b) if b.exists() else None
 if before==record['sha256']:continue
 if before and not args.initial_import and previous.get(record['relative'])!=before:conflicts.append(record['relative'])
 changes.append((record,a,b,before))
report={'status':'CONFLICT' if conflicts else ('PLAN' if not args.apply else 'RUNNING'),'files_to_copy':len(changes),'conflicts':conflicts,'backup':str(backup),'copied':[]}
(folder/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
assert not conflicts,'Target contains local edits; inspect Saved/CharacterAnimationSync/Report.json'
if args.apply:
 if tag_bytes and tag_before!=tag_hash:
  if tag_before:
   preserved=backup/tag_relative;preserved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(tag_target,preserved)
  tag_target.parent.mkdir(parents=True,exist_ok=True);tag_target.write_bytes(tag_bytes)
 for record,a,b,before in changes:
  if before:
   preserved=backup/record['relative'];preserved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(b,preserved)
  b.parent.mkdir(parents=True,exist_ok=True)
  temp=b.with_name(b.name+'.character-sync.tmp');shutil.copy2(a,temp)
  assert sha(temp)==record['sha256']
  os.replace(temp,b);report['copied'].append(record['relative'])
 report['status']='PASS'
 new_state={r['relative']:r['sha256'] for r in manifest['files']}
 if tag_bytes:new_state[tag_relative]=tag_hash
 state_file.write_text(json.dumps(new_state,ensure_ascii=False,indent=2),encoding='utf8')
 (folder/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in report.items() if k!='copied'},ensure_ascii=False))
