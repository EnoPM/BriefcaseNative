"""Build only the portable crypto module under Linux, using a workspace-local Zig compiler.
No system packages or user cache directories are modified. The full host is still Windows-only.
"""
import os,shlex,shutil,subprocess,sys
from pathlib import Path
project=Path(__file__).resolve().parents[2]
build=project/'build/linux-crypto'
build.mkdir(parents=True,exist_ok=True)
zig=Path(sys.argv[1]).resolve()
assert zig.is_file()
source=project/'third_party/mbedtls-3.6.7'
local=build/'mbedtls'
if not local.exists(): shutil.copytree(source,local)
runtime=project/'runtime/Briefcase.Admin'
env=os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR']=str(build/'zig-global-cache')
env['ZIG_LOCAL_CACHE_DIR']=str(build/'zig-local-cache')
env['CC']=shlex.quote(str(zig))+' cc -target x86_64-linux-musl'
env['AR']=shlex.quote(str(zig))+' ar'
env['CFLAGS']='-O2 -I'+shlex.quote(str(runtime))+r' -DMBEDTLS_USER_CONFIG_FILE=\"TlsConfig.h\"'
subprocess.run(['make','-C',str(local),'-j4','lib'],env=env,check=True)
exe=build/'Briefcase.AdminCryptoContracts'
command=[str(zig),'c++','-target','x86_64-linux-musl','-std=c++20','-O2','-pthread',
         '-DMBEDTLS_USER_CONFIG_FILE="TlsConfig.h"','-I'+str(runtime),'-I'+str(local/'include'),
         str(project/'tests/AdminCryptoContracts.cpp'),str(runtime/'Security.cpp'),
         str(local/'library/libmbedtls.a'),str(local/'library/libmbedx509.a'),str(local/'library/libmbedcrypto.a'),
         '-o',str(exe)]
subprocess.run(command,env=env,check=True)
subprocess.run([str(exe),'--export-fixture',str(project/'build/portable-cross/linux')],env=env,check=True)
subprocess.run([str(exe),'--verify-fixture',str(project/'build/portable-cross/windows')],env=env,check=True)
