"""Keep only the qualified P008 evdev input open and drain its events.

Opening evdev enables the existing usbhid interrupt polling. This is not a
touchscreen UI: no USB claims/resets, feature writes, GPIO, PL or input injection.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import struct
import time

REPORT_SHA256='d394982f1108740d7c19e93c7e669e75b0e24a94c59fd45a67b54ce48eb26d16'
EVENT=struct.Struct('@llHHi')


def discover():
    matches=[]
    for h in Path('/sys/bus/hid/devices').glob('*:1A86:E5E3.*'):
        try:
            if hashlib.sha256((h/'report_descriptor').read_bytes()).hexdigest()!=REPORT_SHA256:continue
            nodes=list((h/'input').glob('input*/event*'))
            if len(nodes)!=1:continue
            usb=h.resolve()
            while usb.parent!=usb and not (usb/'idVendor').exists():usb=usb.parent
            if not (usb/'devnum').exists():continue
            matches.append(dict(hid=h.name,event='/dev/input/'+nodes[0].name,port=usb.name,
                devnum=(usb/'devnum').read_text().strip(),descriptor_sha256=REPORT_SHA256))
        except OSError:continue
    return matches[0] if len(matches)==1 else None


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--seconds',type=int,default=120,help='0 for a supervised persistent service')
    outputs=parser.add_mutually_exclusive_group(required=True)
    outputs.add_argument('--output',type=Path)
    outputs.add_argument('--output-directory',type=Path,help='Create a distinct receipt per supervised service start')
    args=parser.parse_args()
    if args.output_directory is not None:
        assert args.output_directory.is_dir()
        args.output=args.output_directory/('poll_'+str(time.monotonic_ns())+'.json')
    assert args.seconds==0 or 1<=args.seconds<=600
    assert not args.output.exists(),'Preserve earlier receipt; select a new output path'
    stopped=False;fd=None;device=None;start=time.monotonic();next_discovery=start;next_save=start
    result=dict(scope='Qualified P008 kernel evdev polling only; no touch menu/input injection/USB reset',
        USB_reset=False,driver_detached=False,MMIO_access=False,requested_seconds=args.seconds,
        started=start,opens=[],disconnects=[],errors=[],events=0,absolute_events=0,key_events=0,syn_dropped=0)
    def stop(signum,frame):
        nonlocal stopped
        stopped=True;result['stop_signal']=signum
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    def save():
        result['updated']=time.monotonic();result['elapsed_seconds']=result['updated']-start
        target=args.output.with_suffix(args.output.suffix+'.next')
        target.write_text(json.dumps(result,indent=2)+'\n');os.replace(target,args.output)
    try:
        while not stopped and (args.seconds==0 or time.monotonic()-start<args.seconds):
            now=time.monotonic()
            if fd is None and now>=next_discovery:
                device=discover();next_discovery=now+.5
                if device is not None:
                    try:
                        fd=os.open(device['event'],os.O_RDONLY|os.O_NONBLOCK)
                        result['opens'].append(dict(at=time.monotonic(),**device))
                        print('P008_POLL_OPEN',device['port'],device['devnum'],flush=True);save()
                    except OSError as e:
                        result['errors'].append(dict(at=now,stage='open',error=repr(e)));fd=None
            if fd is not None:
                try:
                    readable,_,_=select.select([fd],[],[],.2)
                    if readable:
                        raw=os.read(fd,EVENT.size*128)
                        if not raw:raise OSError(errno.ENODEV,'P008 event stream ended')
                        assert len(raw)%EVENT.size==0
                        for sec,usec,kind,code,value in EVENT.iter_unpack(raw):
                            result['events']+=1
                            result['absolute_events']+=kind==3
                            result['key_events']+=kind==1
                            result['syn_dropped']+=kind==0 and code==3
                except OSError as e:
                    result['disconnects'].append(dict(at=time.monotonic(),device=device,error=repr(e)))
                    os.close(fd);fd=None
            else:time.sleep(.1)
            if time.monotonic()>=next_save:save();next_save=time.monotonic()+10
        result['finished']=True
    except Exception as e:result['error']=repr(e);raise
    finally:
        if fd is not None:os.close(fd)
        result['handle_closed']=True;save()
        print('P008_POLL_FINISHED',result['elapsed_seconds'],'events',result['events'],
              'opens',len(result['opens']),'disconnects',len(result['disconnects']),flush=True)


if __name__=='__main__':main()
