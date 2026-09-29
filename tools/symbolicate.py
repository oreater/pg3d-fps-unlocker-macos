#!/usr/bin/env python3
"""Map GameAssembly sample offsets using a matching Il2CppDumper dump.cs.
Inclusive stack appearances are NOT additive CPU percentages.
"""
import argparse,bisect,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('dump',type=Path);p.add_argument('sample',type=Path);args=p.parse_args()
entries=[]; namespace='';cls='';rva=None
for line in args.dump.open():
 if line.startswith('// Namespace:'): namespace=line.strip().split(':',1)[1].strip()
 m=re.search(r'\b(?:class|struct) (\S+)',line)
 if m and 'TypeDefIndex:' in line:cls=(namespace+'.' if namespace else '')+m[1]
 m=re.match(r'\s*// RVA: (0x[0-9A-Fa-f]+)',line)
 if m:rva=int(m[1],16)
 elif rva is not None and line.strip() and not line.strip().startswith('//'):
  if rva:entries.append((rva,cls+' :: '+line.strip().replace(' { }','')))
  rva=None
entries.sort();starts=[x[0] for x in entries]
text=args.sample.read_text().split('Total number in stack')[0]
chunks=re.split(r'(?=^    \d+ Thread_)',text,flags=re.M)
for chunk in chunks:
 if not chunk.startswith('    '):continue
 title=chunk.splitlines()[0]
 if 'main-thread' not in title and 'UnityGfx' not in title and 'Main Thread' not in title:continue
 counts={}
 for line in chunk.splitlines():
  m=re.search(r'(\d+) \?\?\?  \(in GameAssembly.dylib\)  load address \w+ \+ (0x[0-9a-f]+)',line)
  if m:
   addr=int(m[2],16); i=bisect.bisect_right(starts,addr)-1
   if i>=0 and addr-starts[i]<8192:counts[entries[i][1]]=counts.get(entries[i][1],0)+int(m[1])
 print(title)
 for name,n in sorted(counts.items(),key=lambda x:-x[1])[:25]:print(n,name)
