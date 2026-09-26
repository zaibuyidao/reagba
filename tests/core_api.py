"""Exercise the actual core DLL's native/ReaScript ABI without a UI host."""
import ctypes as C,json,os,sys,time
from pathlib import Path
binary=Path(sys.argv[1]).resolve();root=binary.parent/('core-api-test-'+str(os.getpid()));root.mkdir(exist_ok=False)
os.environ['SDL_AUDIODRIVER']='dummy'
resource=C.create_string_buffer(str(root).encode('utf-8'))
registers={};requested=[]
REGISTER=C.CFUNCTYPE(C.c_int,C.c_char_p,C.c_void_p)
GETFUNC=C.CFUNCTYPE(C.c_void_p,C.c_char_p)
@C.CFUNCTYPE(C.c_void_p)
def resource_path():return C.addressof(resource)
@REGISTER
def register(name,address):
 name=name.decode()
 if name.startswith('-'):registers.pop(name[1:],None)
 else:registers[name]=address
 return 1
@GETFUNC
def getfunc(name):
 requested.append(name.decode())
 return C.cast(resource_path,C.c_void_p).value if name==b'GetResourcePath' else None
class Info(C.Structure):_fields_=[('version',C.c_int),('window',C.c_void_p),('register',REGISTER),('getfunc',GETFUNC)]
lib=C.CDLL(str(binary));entry=lib.ReaperPluginEntry;entry.argtypes=[C.c_void_p,C.POINTER(Info)];entry.restype=C.c_int
info=Info(0x20e,None,register,getfunc)
assert entry(None,C.byref(info))==1
assert requested==['GetResourcePath'],requested
names={'ReaGBA_Create','ReaGBA_Destroy','ReaGBA_Request','ReaGBA_Poll','ReaGBA_ReadFrame','ReaGBA_SetInput','ReaGBA_GetLastError'}
assert set(registers)=={'timer'}|{prefix+n for prefix in ('API_','APIdef_','APIvararg_') for n in names}
def api(name,returns,*args):return C.CFUNCTYPE(returns,*args)(registers['API_'+name])
create=api('ReaGBA_Create',C.c_int,C.c_char_p);destroy=api('ReaGBA_Destroy',C.c_bool,C.c_int)
request=api('ReaGBA_Request',C.c_bool,C.c_int,C.c_char_p);poll=api('ReaGBA_Poll',C.c_char_p,C.c_int)
error=api('ReaGBA_GetLastError',C.c_char_p);frame=api('ReaGBA_ReadFrame',C.c_char_p,C.c_int,C.c_bool)
set_input=api('ReaGBA_SetInput',C.c_bool,C.c_int,C.c_int,C.c_bool,C.c_bool)
def reply(id):
 end=time.monotonic()+5
 while time.monotonic()<end:
  text=poll(id)
  if text:return json.loads(text)
  time.sleep(.002)
 raise AssertionError('Missing reply')
try:
 assert create(b'relative/path')==0 and error()
 id=create(b'');assert id>0
 assert create(b'')==0 and b'already' in error()
 assert not destroy(id+1)
 assert not request(id,b'{') and error()
 assert not request(id,b' '*65537)
 assert not set_input(id,1024,False,True)
 assert set_input(id,65,False,True)
 assert frame(id,False)==b''
 assert request(id,b'{"action":"game_viewport","id":7}')
 result=reply(id);assert result['id']==7 and not result['ok'],'UI commands must not execute in core'
 for i in range(128):assert request(id,json.dumps({'action':'get_emulator_state','id':i}).encode())
 assert not request(id,b'{"action":"pause"}') and b'queue' in error()
 results=[reply(id) for _ in range(128)]
 assert [v['id'] for v in results]==list(range(128))
 assert all(v['ok'] for v in results)
 assert poll(id)==b''
 assert destroy(id)
 new=create(b'');assert new>id
 assert not destroy(id) and request(new,b'{"action":"get_settings"}')
 preferences=reply(new)['result'];assert not any(k in preferences for k in ('shader','language','library_view'))
 assert destroy(new)
 # Verify the vararg path as well as the typed C API.
 vararg=C.CFUNCTYPE(C.c_void_p,C.POINTER(C.c_void_p),C.c_int)(registers['APIvararg_ReaGBA_Create'])
 empty=C.create_string_buffer(b'');args=(C.c_void_p*1)(C.cast(empty,C.c_void_p));id=vararg(args,1);assert id and destroy(id)
finally:entry(None,None)
assert not registers,'Registrations leaked on unload'
print('PASS: DLL ABI, core-only API registration, validation, queue pressure, reply IDs, stale-session protection and unload')
