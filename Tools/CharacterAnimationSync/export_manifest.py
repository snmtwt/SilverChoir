"""Run in the animation-authoring project's UE Python commandlet; read-only export."""
import unreal,json,hashlib,os,re
from pathlib import Path

root=Path(unreal.Paths.project_dir()).resolve()
destination=Path(os.environ.get('CHARACTER_SYNC_MANIFEST',str(root/'Saved/CharacterAnimationSync/manifest.json')))
r=unreal.AssetRegistryHelpers.get_asset_registry();r.search_all_assets(True)
options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
prefix='/Game/System/Object/Unit/Character/Animation'
queue=[str(a.package_name) for a in r.get_assets_by_path(prefix,True)]
assert queue,'No shared animation assets found: '+prefix
seen=set();files=[];external=set();errors=[]
while queue:
 p=queue.pop()
 if p in seen:continue
 seen.add(p)
 if p.startswith('/Script/GameAnimationSample') or p.startswith('/Script/SilverChoir'):
  errors.append('Game-specific native dependency: '+p);continue
 if not p.startswith('/Game/'):
  external.add(p);continue
 data=r.get_assets_by_package_name(p)
 if not data:errors.append('Missing package: '+p);continue
 classes=sorted(set(str(a.asset_class_path.asset_name) for a in data))
 if 'World' in classes:errors.append('Maps cannot be synchronized: '+p);continue
 for suffix in ['.uasset','.uexp','.ubulk','.uptnl']:
  relative='Content/'+p[6:]+suffix;f=root/relative
  if f.is_file():files.append({'relative':relative,'package':p,'classes':classes,'size':f.stat().st_size,'sha256':hashlib.file_digest(f.open('rb'),'sha256').hexdigest()})
 queue.extend(str(x) for x in r.get_dependencies(p,options))
manifest={'status':'FAIL' if errors else 'PASS','source_project':str(root),'animation_root':prefix,'files':sorted(files,key=lambda f:f['relative']),'external_dependencies':sorted(external),'errors':errors}
tag_lines=set()
for cfg in [root/'Config/DefaultGameplayTags.ini',*sorted((root/'Config/Tags').glob('*.ini'))]:
 for line in cfg.read_text(encoding='utf-8-sig').splitlines():
  if ('GameplayTagList=' in line or 'GameplayTagRedirects=' in line) and re.search(r'"(?:SM\.|HMS\.|Foley(?:\.|\")|MotionMatching(?:\.|\"))',line):tag_lines.add(line.lstrip('+'))
manifest['animation_tags']='[/Script/GameplayTags.GameplayTagsList]\n'+'\n'.join(sorted(tag_lines))+'\n'
destination.parent.mkdir(parents=True,exist_ok=True);destination.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
assert not errors,str(errors)
unreal.log('CHARACTER_SYNC_EXPORT_PASS packages='+str(len(seen))+' files='+str(len(files)))
