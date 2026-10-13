"""Inventory and risk-site index. Findings are review leads, not defect proofs."""
# Copyright (C) 2026 Raffaele Mele
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse, json, hashlib, re

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=Path(__file__).resolve().parent.parent)
p.add_argument('--compile-commands',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
source=a.source.resolve()
commands=json.loads(a.compile_commands.read_text(encoding='utf-8'))
compiled={Path(c['file']).resolve() for c in commands}
patterns={
 'nested_events':r'\bprocessEvents\s*\(|(?:\.|->)exec\s*\(',
 'signals':r'\bemit\s+\w+',
 'callbacks':r'\bconnect\s*\(|QTimer::singleShot|QtConcurrent::|QFutureWatcher',
 'destruction':r'\bdelete\s+|deleteLater\s*\(',
 'model_pointers':r'internalPointer\s*\(|QTreeWidgetItem\s*\*|QModelIndex\b',
 'blocking':r'waitFor\w*\s*\(|QMutexLocker|\.lock\s*\(',
 'external_input':r'fromJson\s*\(|readAll\w*\s*\(|\.toInt\s*\(|\.toLongLong\s*\(',
 'ownership_cleanup':r'finally\s*\(|QScopedValueRollback|QSignalBlocker',
}
records=[]
for folder in ('RedPandaIDE/src','libs','tools'):
 for f in sorted((source/folder).rglob('*')):
  if not f.is_file() or f.suffix.lower() not in ('.cpp','.c','.h','.hpp','.cc'):continue
  raw=f.read_bytes();text=raw.decode('utf-8',errors='replace');sites=[]
  for kind,pattern in patterns.items():
   for match in re.finditer(pattern,text):
    line=text.count('\n',0,match.start())+1
    sites.append(dict(kind=kind,line=line,token=match.group(0)))
  rel=f.relative_to(source).as_posix()
  third_party=rel.startswith(('libs/lua/','libs/simpleini/','libs/3rdparty/','libs/tomlplusplus/')) or rel.endswith('/SimpleIni.h')
  records.append(dict(path=rel,sha256=hashlib.sha256(raw).hexdigest(),lines=len(text.splitlines()),compiled_translation_unit=f.resolve() in compiled,third_party=third_party,scan_complete=True,semantic_review='pending',risk_sites=sorted(sites,key=lambda s:s['line'])))
result=dict(scope='All repository application, library and tool C/C++ files; framework binaries are reviewed at their integration boundary.',method='Textual risk-site inventory plus compile-command membership; semantic review and analyzer findings tracked separately.',files=records,fileCount=len(records),lines=sum(f['lines'] for f in records),riskSiteCount=sum(len(f['risk_sites']) for f in records))
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
