"""Read-only PE/signature audit. No process access or executable modifications."""
import hashlib, json, pathlib, struct, sys
p=pathlib.Path(sys.argv[1]); data=p.read_bytes()
nt=struct.unpack_from("<I",data,0x3c)[0]
sections=struct.unpack_from("<H",data,nt+6)[0]
opt=struct.unpack_from("<H",data,nt+20)[0]
out={"sha256":hashlib.sha256(data).hexdigest(),"timestamp":hex(struct.unpack_from("<I",data,nt+8)[0]),"entryRva":hex(struct.unpack_from("<I",data,nt+40)[0]),"sites":{}}
patterns={
"manager":"83 E9 02 74 15 83 F9 01 74 08 41 B9 0C 00 00 00 EB 0E 41 B9 0A 00 00 00 EB 06 41 B9 08 00 00 00 C6 47 51 00",
"session":"41 0F B6 48 60 83 E9 02 74 11 83 F9 01 74 05 8D 42 0C EB 0C B8 0A 00 00 00 EB 05 B8 08 00 00 00 41 8B 48 48 83 F9 01"}
for n in range(sections):
 s=nt+24+opt+n*40
 name=data[s:s+8].rstrip(b"\0").decode()
 vs,rva,rs,raw=struct.unpack_from("<4I",data,s+8)
 if name!=".text": continue
 for k,v in patterns.items():
  sig=bytes.fromhex(v); matches=[]; pos=raw
  while (pos:=data.find(sig,pos,raw+rs))>=0:
   matches.append(hex(rva+pos-raw));pos+=1
  if len(matches)!=1: raise RuntimeError(f"{k}: expected one match, got {matches}")
  out["sites"][k]={"rva":matches[0],"bytes":v}
print(json.dumps(out,indent=2))
