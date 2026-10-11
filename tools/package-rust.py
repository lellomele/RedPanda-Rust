# Copyright (C) 2026 Raffaele Mele
# SPDX-License-Identifier: GPL-3.0-or-later
# Runtime traversal and component notice mapping reused from RedPanda-CPP
# platform/windows/fork-packaging/complete-runtime.py, with original notices retained.
from pathlib import Path
import subprocess, os, re, shutil, json, argparse, hashlib, zipfile
parser=argparse.ArgumentParser(description="Package Red Panda Rust with complete private Qt runtime and notices")
parser.add_argument('--installed',required=True,type=Path)
parser.add_argument('--build',required=True,type=Path)
parser.add_argument('--qt-prefix',required=True,type=Path)
parser.add_argument('--compiler-prefix',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args()
source=Path(__file__).resolve().parent.parent
sdk=args.qt_prefix;compiler=args.compiler_prefix
env=os.environ.copy();env['PATH']=str(compiler/'bin')+';'+env.get('PATH','')
def imports(path):
 p=subprocess.run([str(compiler/'bin/objdump.exe'),'-p',str(path)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW,timeout=20)
 if p.returncode:raise RuntimeError('Cannot inspect '+str(path))
 return re.findall(r'DLL Name:\s*(\S+)',p.stdout.decode('utf-8',errors='replace'))

def digest(path):
 value=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
 return value.hexdigest()

def copy_notices(payload, build):
 app=payload;license_root=payload/'licenses';license_root.mkdir(exist_ok=True)
 components={};versions={};origins={}
 rules=[(r'qt6svg\.dll|qsvg(?:icon)?\.dll','qt6-svg'),(r'qt6.*\.dll','qt6-base'),
        (r'lib(?:gcc_s_seh-1|stdc\+\+-6)\.dll','gcc-libs'),(r'libwinpthread-1\.dll','libwinpthread'),
        (r'libicu(?:dt|in|uc)\d+\.dll','icu'),(r'libb2-1\.dll','libb2'),
        (r'libbrotli(?:common|dec|enc)\.dll','brotli'),(r'libbz2-1\.dll','bzip2'),
        (r'libdouble-conversion\.dll','double-conversion'),(r'libffi-\d+\.dll','libffi'),
        (r'libfreetype-\d+\.dll','freetype'),(r'lib(?:gio|glib|gmodule|gobject)-2\.0-0\.dll','glib2'),
        (r'libgraphite2\.dll','graphite2'),(r'libharfbuzz-\d+\.dll','harfbuzz'),
        (r'libiconv-\d+\.dll','libiconv'),(r'libintl-\d+\.dll','gettext-runtime'),
        (r'libjpeg-\d+\.dll','libjpeg-turbo'),(r'libmd4c\.dll','md4c'),
        (r'libpcre2-\d+-0\.dll','pcre2'),(r'libpng\d+-\d+\.dll','libpng'),
        (r'libzstd\.dll','zstd'),(r'zlib1\.dll','zlib'),(r'lib(?:ssl|crypto)-\d+-x64\.dll','openssl')]
 for module in sorted(payload.rglob('*')):
  if not module.is_file():continue
  name=module.name.casefold();component=None
  if module.suffix.casefold()=='.dll':
   component=next((item for pattern,item in rules if re.fullmatch(pattern,name)),None)
   if component is None and module.parent!=app:
    plugin=sdk/'share/qt6/plugins'/module.relative_to(app)
    if plugin.is_file() and digest(plugin)==digest(module):component='qt6-base'
   if component is None:raise RuntimeError('License mapping required for distributed DLL '+str(module))
  elif name=='astyle.exe':component='astyle'
  elif name in ('redpandarust.exe','redpandaide.exe','redpandaide-core.exe','consolepauser.exe','redpanda-win-git-askpass.exe'):component='redpanda'
  elif module.suffix.casefold()=='.exe':raise RuntimeError('License mapping required for distributed EXE '+str(module))
  elif name.startswith('qt') and name.endswith('.qm'):component='qt6-translations'
  if component:components.setdefault(component,[]).append(module.relative_to(payload).as_posix())
 for component,modules in components.items():
  destination=license_root/component
  if component=='redpanda':
   destination.mkdir(exist_ok=True);shutil.copy2(app/'LICENSE',destination/'LICENSE')
   origins[component]='https://github.com/lellomele/RedPanda-CPP'
  elif component=='astyle':
   result=subprocess.run([str(app/'astyle.exe'),'--version'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                         creationflags=subprocess.CREATE_NO_WINDOW,timeout=10,check=True)
   version=re.search(rb'Artistic Style Version (\d+\.\d+\.\d+)',result.stdout+result.stderr)
   if not version:raise RuntimeError('Cannot identify distributed Artistic Style version')
   versions[component]=version.group(1).decode('ascii')
   source=Path(__file__).parent/'notices'/('astyle-'+versions[component])
   if not (source/'LICENSE.md').is_file():raise RuntimeError('Matching Artistic Style notice missing: '+str(source))
   shutil.copytree(source,destination,dirs_exist_ok=True)
   origins[component]='https://gitlab.com/saalen/astyle/-/tree/'+versions[component]
  elif component=='icu':
   majors={re.fullmatch(r'libicu(?:dt|in|uc)(\d+)\.dll',Path(module).name).group(1) for module in modules}
   if len(majors)!=1:raise RuntimeError('Mixed ICU majors in payload')
   major=next(iter(majors));candidates=[]
   for prefix in (sdk,compiler):
    if all((prefix/'bin'/Path(module).name).is_file() and digest(prefix/'bin'/Path(module).name)==digest(payload/module) for module in modules):
     candidates += list((prefix/'share'/'icu').glob(major+'.*/LICENSE'))
   if len(candidates)!=1:raise RuntimeError('A unique matching ICU notice is required: '+repr(candidates))
   source=candidates[0];versions[component]=source.parent.name
   destination.mkdir(exist_ok=True);shutil.copy2(source,destination/'LICENSE')
   origins[component]='https://github.com/unicode-org/icu/releases'
  else:
   source=next((prefix/'share'/'licenses'/component for prefix in (sdk,compiler)
                if (prefix/'share'/'licenses'/component).is_dir()),None)
   if source is None:raise RuntimeError('Required distributed license missing: '+component)
   shutil.copytree(source,destination,dirs_exist_ok=True)
   origins[component]='Qt SDK or MSYS2 package notice: '+component
 cache=(build/'CMakeCache.txt').read_text(encoding='utf-8')
 if 'LUA_ADDON:BOOL=ON' in cache:
  header=Path(__file__).resolve().parents[3]/'libs/lua/lua/lua.h'
  text=header.read_text(encoding='utf-8');start=text.rfind('/*',0,text.index('* Copyright (C) 1994-2023 Lua.org'))
  end=text.index('*/',start)+2;notice=text[start:end]
  if 'Permission is hereby granted' not in notice:raise RuntimeError('Lua MIT notice not found in corresponding sources')
  destination=license_root/'lua';destination.mkdir(exist_ok=True)
  (destination/'LICENSE.txt').write_text(notice+'\n',encoding='utf-8')
  versions['lua']='.'.join(re.search(r'#define LUA_VERSION_'+part+r'\s+"([^"]+)"',text).group(1) for part in ('MAJOR','MINOR','RELEASE'))
  components['lua']=['app/RedPandaIDE-core.exe (embedded Lua)'];origins['lua']='libs/lua/lua/lua.h; https://www.lua.org/license.html'
 index=[]
 for component,modules in sorted(components.items()):
  notices=sorted(p for p in (license_root/component).rglob('*') if p.is_file())
  if not notices or any(not p.read_bytes().strip() for p in notices):raise RuntimeError('Empty required license notice: '+component)
  index.append(dict(component=component,version=versions.get(component),modules=modules,origin=origins[component],
                    notices=[dict(path=p.relative_to(payload).as_posix(),sha256=digest(p)) for p in notices]))
 (license_root/'COMPONENTS.json').write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 (license_root/'README.txt').write_text('English\nThird-party licenses and original notices are retained in the component folders.\nCOMPONENTS.json maps distributed modules to their notices and SHA-256 checksums.\nArtistic Style and embedded Lua use MIT; ICU includes its complete upstream notice.\nThe IDE, launcher and Console Pauser sources are in the Sources archive of this release.\nQt is dynamically linked; its DLLs may be replaced with compatible builds under their licenses.\n\nItaliano\nLe licenze e i notice originali sono conservati nelle cartelle dei componenti.\nCOMPONENTS.json associa moduli distribuiti, notice e impronte SHA-256.\nArtistic Style e Lua incorporato usano MIT; ICU include il notice upstream completo.\nI sorgenti di IDE, launcher e Console Pauser sono nello ZIP Sources della release.\nQt è collegato dinamicamente: le DLL sono sostituibili con build compatibili secondo le relative licenze.\n',encoding='utf-8')
 return index

if args.output.exists() or args.output.with_suffix('.zip').exists():
    raise RuntimeError('Choose a new package output; existing packages are preserved')
stage=args.output.parent/(args.output.name+'-staging')
if stage.exists():raise RuntimeError('Staging already exists: '+str(stage))
stage.mkdir(parents=True)
try:
    for name in ('RedPandaRust.exe','README.md','LICENSE','FORK_NOTICE.txt','qt.conf'):
        shutil.copy2(args.installed/name,stage/name)
    (stage/'config').mkdir()
    shutil.copy2(source/'RedPandaIDE/resources/appicon/red-panda-rust.ico',stage/'red-panda-rust.ico')
    shutil.copy2(source/'RedPandaIDE/resources/appicon/red-panda-rust.png',stage/'red-panda-rust.png')
    subprocess.run([str(sdk/'bin/windeployqt.exe'),'--dir',str(stage),'--compiler-runtime','--no-translations',str(stage/'RedPandaRust.exe')],env=env,check=True)
    queue=list(stage.rglob('*.dll'))+[stage/'RedPandaRust.exe'];seen=set();extra=[];system=Path(os.environ['SystemRoot'])/'System32'
    while queue:
        module=queue.pop()
        if str(module).casefold() in seen:continue
        seen.add(str(module).casefold())
        for dll in imports(module):
            if dll.lower().startswith(('api-ms-win-','ext-ms-win-')) or (system/dll).is_file():continue
            target=stage/dll
            if not target.exists():
                candidate=next((p/dll for p in (sdk/'bin',compiler/'bin') if (p/dll).is_file()),None)
                if candidate is None:raise RuntimeError('Missing runtime dependency: '+dll+' imported by '+str(module))
                shutil.copy2(candidate,target);extra.append(dll)
            queue.append(target)
    notices=copy_notices(stage,args.build)
    (stage/'licenses/README.txt').write_text('Third-party notices are retained by component. COMPONENTS.json maps the distributed binaries to their licenses and checksums. Qt is dynamically linked; compatible DLL replacements are permitted under its licenses. Complete matching IDE sources are in RedPandaRust-Sources.zip.\n',encoding='utf-8')
    report={'modules':len(seen),'unresolved':[],'extraDependencies':extra,'licenseComponents':len(notices),'sourceBase':'18e9afbc8a60a4e5568362fc5e8011a8050cc84c'}
    (stage/'RUNTIME.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (stage/'COPYRIGHT.txt').write_text('Red Panda Rust\nRust modifications copyright (C) 2026 Raffaele Mele.\nOriginal code copyright (C) 2020-2026 Roy Qu and contributors.\nGPL-3.0-or-later; original notices and dependency licenses retained.\n',encoding='utf-8')
    stage.rename(args.output)
    with zipfile.ZipFile(args.output.with_suffix('.zip'),'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for file in sorted(args.output.rglob('*')):
            if file.is_file():archive.write(file,args.output.name+'/'+file.relative_to(args.output).as_posix())
    print(json.dumps(report,indent=2))
except Exception:
    print('Incomplete staging retained for diagnosis:',stage)
    raise
