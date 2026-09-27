"""Exercise the real ReaGBA service/producer ABI and both extension unload orders."""
import argparse,ctypes as C,json,os,queue,random,struct,threading,time,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--core',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False);os.environ['SDL_AUDIODRIVER']='dummy'
resource=C.create_string_buffer(str(a.output.resolve()).encode());functions={};registered={};refs=[];replies=queue.Queue();events=[];failures=[];frames=0;stream_open=False;service=None;shutdown=None
U=C.c_uint64;P=C.c_void_p;S=C.c_char_p;I=C.c_int
REQUEST=C.CFUNCTYPE(I,P,U,U,I,S,S)
class Callbacks(C.Structure):_fields_=[('size',C.c_uint32),('abi',C.c_uint32),('data',P),('request',REQUEST),('cancel',P)]
class Desc(C.Structure):_fields_=[(n,C.c_uint32) for n in ['size','abi','format','max_bytes','capacity','width','height','stride','channels','rate','block','fft']]+[('update',C.c_double),('source',S),('owner',U)]
def export(name,result,*args):
 def wrap(fn):
  cb=C.CFUNCTYPE(result,*args)(fn);refs.append(cb);functions[name.encode()]=C.cast(cb,P).value;return fn
 return wrap
@export('GetResourcePath',P)
def resource_path():return C.addressof(resource)
@export('ReaWeb_RegisterService',I,S,C.POINTER(Callbacks),C.POINTER(U))
def add(name,callbacks,handle):
 global service
 assert name==b'reagba';service=Callbacks.from_buffer_copy(callbacks.contents);handle[0]=1;events.append('register');return 0
@export('ReaWeb_SetServiceInput',I,U,S)
def input_method(handle,method):assert method==b'input';return 0
@export('ReaWeb_SetServiceShutdown',I,U,P)
def on_shutdown(handle,callback):
 global shutdown
 shutdown=C.CFUNCTYPE(None,P)(callback);return 0
@export('ReaWeb_UnregisterService',I,U)
def remove(handle):
 assert not stream_open;events.append('unregister');shutdown(None);return 0
@export('ReaWeb_CreateFrameStream',I,S,C.POINTER(Desc),C.POINTER(U))
def create(name,desc,handle):
 global stream_open
 assert not stream_open and name==b'reagba.video'
 d=desc.contents;assert (d.width,d.height,d.stride,d.capacity,d.format,d.owner)==(240,160,960,3,1,1)
 stream_open=True;handle[0]=7;events.append('create');return 0
@export('ReaWeb_PublishFrame',I,U,P,C.c_uint32,U,C.c_double)
def publish(handle,data,size,sequence,timestamp):
 global frames
 if not (stream_open and size==153600 and threading.current_thread()!=threading.main_thread() and C.string_at(data,4)[3]==255):
  if not failures:failures.append('Invalid producer thread, lifecycle or RGBA frame')
 frames+=1;return 0
@export('ReaWeb_CloseStream',I,U)
def close(handle):
 global stream_open
 stream_open=False;events.append('close');return 0
@export('ReaWeb_CompleteServiceCall',I,U,U,S,I,S)
def complete(handle,request,text,status,error):
 if text and len(text)>1024*1024:return 3
 replies.put((request,status,json.loads(text) if text else error));return 0
REGISTER=C.CFUNCTYPE(I,S,P);GET=C.CFUNCTYPE(P,S)
@REGISTER
def register(name,address):
 name=name.decode()
 if name.startswith('-'):registered.pop(name[1:],None)
 else:registered[name]=address
 return 1
@GET
def get(name):return functions.get(name)
class Info(C.Structure):_fields_=[('version',I),('window',P),('register',REGISTER),('get',GET)]
info=Info(0x20e,None,register,get);lib=C.CDLL(str(a.core.resolve()));entry=lib.ReaperPluginEntry;entry.argtypes=[P,C.POINTER(Info)];entry.restype=I
counter=0
def call(method,data=None):
 global counter
 counter+=1;assert service.request(None,1,counter,1,method.encode(),json.dumps(data).encode())==0
 request,status,result=replies.get(timeout=8);assert request==counter and status==0,(request,status,result)
 if isinstance(result,dict) and '__reagbaResult' in result:
  blob=result['__reagbaResult'];offset=0;parts=[]
  while offset<blob['bytes']:
   part=call('readResult',{'token':blob['token'],'offset':offset});assert part['offset']==offset and part['bytes']<=128*1024
   parts.append(part['text']);offset+=part['bytes']
  assert offset==blob['bytes'];result=json.loads(''.join(parts))
 return result
try:
 assert entry(None,C.byref(info))==1;C.CFUNCTYPE(None)(registered['timer'])();assert service
 call('getState')
 outputs=call('get_audio_outputs');assert outputs['reaper_available'] is False and outputs['status']['audio_output']=='system'
 call('setSettings',{'settings':{'audio_output':'reaper_output'}})
 outputs=call('get_audio_outputs');assert outputs['status']['audio_device'] is False and outputs['status']['audio_error']
 call('setSettings',{'settings':{'audio_output':'system','audio_track':'selected'}})
 outputs=call('get_audio_outputs');assert outputs['status']['audio_device'] is True and outputs['status']['audio_track']=='selected'
 cache=a.output/'Scripts/zaibuyidao Scripts/Modules/ReaGBA/cache/covers';cache.mkdir(parents=True,exist_ok=True)
 def png_chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
 pixels=random.Random(0).randbytes(512*400*4)
 raw=b''.join(b'\0'+pixels[y*2048:(y+1)*2048] for y in range(400))
 png=b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',512,400,8,6,0,0,0))+png_chunk(b'IDAT',zlib.compress(raw))+png_chunk(b'IEND',b'')
 (cache/'TEST.png').write_bytes(png);call('setSettings',{'settings':{'auto_download_covers':True}})
 end=time.monotonic()+5
 while True:
  cover=call('get_cover',{'code':'TEST'})
  if cover['status']=='ready':break
  assert time.monotonic()<end;time.sleep(.02)
 assert len(cover['image'])>1024*1024
 call('setSettings',{'settings':{'auto_download_covers':False}})
 call('loadRom',{'path':str(a.rom.resolve())});call('resume')
 start=time.monotonic();before=frames;time.sleep(3);fps=(frames-before)/(time.monotonic()-start);assert 52<fps<67,fps
 for mask in [1,0]:
  assert service.request(None,1,0,1,b'input',json.dumps({'mask':mask,'fast':False,'active':True}).encode())==0
  deadline=time.monotonic()+.25
  while call('getState')['input_mask']!=mask:
   assert time.monotonic()<deadline,'Input was not consumed by the emulator'
   time.sleep(.002)
 call('pause');call('saveState',{'slot':1});call('loadState',{'slot':1});call('reset');call('closeRom');assert not call('getState')['loaded'];call('loadRom',{'path':str(a.rom.resolve())})
 shutdown(None);before=frames;time.sleep(.1);assert not stream_open and frames==before
 # Re-register after the runtime provider requests shutdown, then unload ReaGBA first.
 C.CFUNCTYPE(None)(registered['timer'])();call('getState');call('loadRom',{'path':str(a.rom.resolve())});call('resume');time.sleep(.1)
 entry(None,None);before=frames;time.sleep(.1);assert not registered and not stream_open and frames==before
 assert events[-2:]==['close','unregister'],events
 assert not failures,failures
 print(json.dumps({'passed':True,'producerFps':fps,'checks':['service ABI','real ROM','input state','controls','large cover reply','save/load','runtime-first shutdown','producer-first unload','no callbacks after close']}))
finally:entry(None,None)
