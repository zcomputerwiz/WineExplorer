# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixed QMP powerdown observation only; no VM lifecycle CLI or graceful verdict.

Caller owns endpoint/process identity, QEMU wait/exit0 and absence of fallback.
One shared <=50s deadline includes connect, negotiation, ACKs and event receipt.
"""
import base64
from collections import deque
import json
import math
import secrets
import socket
import time

FRAME_BYTES = 8192
TRANSCRIPT_BYTES = 65536
TRANSCRIPT_RECORDS = 256
MESSAGES = 128


def _object(raw):
    def pairs(items):
        result = {}
        for key,value in items:
            if key in result:raise ValueError('Duplicate QMP JSON key')
            result[key]=value
        return result
    def constant(value):raise ValueError('Nonfinite QMP JSON')
    result=json.loads(raw.decode('utf-8','strict'),object_pairs_hook=pairs,parse_constant=constant)
    if type(result) is not dict:raise ValueError('QMP message must be object')
    return result


def _keys(value,required,optional=()):
    if type(value) is not dict or not set(required)<=set(value)<=set(required)|set(optional):
        raise ValueError('QMP schema mismatch')


class _Stream:
    def __init__(self, connection, result, end, now):
        self.connection,self.result,self.end,self.now=connection,result,end,now
        self.buffer=b'';self.pending=deque();self.byte_count=0;self.message_count=0

    def remaining(self):
        seconds=self.end-self.now()
        if seconds<=0:raise TimeoutError('Whole QMP observation deadline')
        return seconds

    def record(self,direction,data,**metadata):
        self.byte_count+=len(data)
        if self.byte_count>TRANSCRIPT_BYTES or len(self.result['transcript'])>=TRANSCRIPT_RECORDS:
            raise ValueError('QMP transcript quota')
        self.result['transcript'].append(dict(direction=direction,monotonic=self.now(),
            bytes=len(data),base64=base64.b64encode(data).decode(),**metadata))

    def receive(self,blocking=True):
        self.connection.settimeout(self.remaining() if blocking else 0)
        try:data=self.connection.recv(4096)
        except BlockingIOError:return False
        if not data:raise EOFError('QMP EOF before required observation completed')
        pre=not self.result['powerdown_requested']
        self.record('rx',data,before_powerdown_request=pre)
        self.buffer+=data
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            if len(line)>FRAME_BYTES:raise ValueError('QMP frame quota')
            self.message_count+=1
            if self.message_count>MESSAGES:raise ValueError('QMP message quota')
            self.pending.append((_object(line.rstrip(b'\r')),pre))
        if len(self.buffer)>FRAME_BYTES:raise ValueError('QMP partial frame quota')
        return True

    def next(self):
        while not self.pending:self.receive()
        self.remaining()
        return self.pending.popleft()

    def send(self,command,request_id):
        self.connection.settimeout(self.remaining())
        data=(json.dumps(dict(execute=command,id=request_id),separators=(',',':'))+'\r\n').encode()
        self.record('tx',data,completed=False)
        self.connection.sendall(data)
        self.remaining()
        self.result['transcript'][-1]['completed']=True


def observe_shutdown(endpoint, *, maximum_seconds=50, socket_factory=None, now=time.monotonic):
    """Return bounded protocol evidence, including primary/cleanup errors.

    Never sends quit/reset, signals a process, waits for QEMU or independently
    claims graceful shutdown. All socket ownership belongs to this call.
    """
    if type(maximum_seconds) not in (int,float) or not math.isfinite(maximum_seconds) or not 0<maximum_seconds<=50:
        raise ValueError('QMP observation budget must be >0 and <=50 seconds')
    start=now();end=start+maximum_seconds
    token=secrets.token_hex(16)
    ids=dict(capabilities=token+'-capabilities',powerdown=token+'-powerdown')
    result=dict(started_monotonic=start,maximum_seconds=maximum_seconds,request_ids=ids,
        capabilities_ack=False,powerdown_requested=False,powerdown_ack=False,
        guest_shutdown_observed=False,protocol_complete=False,events=[],transcript=[])
    connection=None
    try:
        connection=(socket_factory or (lambda:socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)))()
        stream=_Stream(connection,result,end,now)
        connection.settimeout(stream.remaining());connection.connect(endpoint)
        greeting,pre=stream.next()
        _keys(greeting,('QMP',));_keys(greeting['QMP'],('version','capabilities'))
        version=greeting['QMP']['version'];_keys(version,('qemu','package'))
        _keys(version['qemu'],('major','minor','micro'))
        if (type(version['package']) is not str or
                any(type(n) is not int or n<0 for n in version['qemu'].values()) or
                type(greeting['QMP']['capabilities']) is not list or
                any(type(v) is not str for v in greeting['QMP']['capabilities'])):
            raise ValueError('Invalid QMP greeting types')
        result['greeting']=greeting

        def event(message,before):
            _keys(message,('event','timestamp'),('data',))
            if type(message['event']) is not str or not message['event']:
                raise ValueError('Invalid QMP event name')
            stamp=message['timestamp'];_keys(stamp,('seconds','microseconds'))
            seconds,micros=stamp['seconds'],stamp['microseconds']
            if (type(seconds) is not int or type(micros) is not int or
                    not ((seconds==micros==-1) or (seconds>=0 and 0<=micros<1000000))):
                raise ValueError('Invalid QMP event timestamp')
            if 'data' in message and type(message['data']) is not dict:
                raise ValueError('Invalid QMP event data')
            result['events'].append(dict(message=message,before_powerdown_request=before,
                                         observed_monotonic=now()))
            if message['event']=='SHUTDOWN':
                data=message.get('data');_keys(data,('guest','reason'))
                if type(data['guest']) is not bool or type(data['reason']) is not str:
                    raise ValueError('Invalid SHUTDOWN data types')
                if not before:
                    if data['guest'] is not True or data['reason']!='guest-shutdown':
                        raise ValueError('Non-guest-shutdown event is not completion')
                    result['guest_shutdown_observed']=True
            if message['event']=='RESET' and not before:
                raise ValueError('RESET is not guest shutdown completion')

        def reply(message,before,expected):
            if 'event' in message:
                event(message,before);return False
            if 'error' in message:
                _keys(message,('error',),('id',))
                _keys(message['error'],('class','desc'))
                if any(type(v) is not str for v in message['error'].values()):
                    raise ValueError('Invalid QMP error types')
                result['qmp_error']=message
                raise ValueError('QMP explicit error response')
            _keys(message,('return','id'))
            if type(message['id']) is not str or message['id']!=expected:
                raise ValueError('Wrong/duplicate/unexpected QMP reply ID')
            if type(message['return']) is not dict or message['return']!={}:
                raise ValueError('Expected empty fixed-command QMP return')
            return True

        stream.send('qmp_capabilities',ids['capabilities'])
        while not result['capabilities_ack']:
            message,before=stream.next()
            result['capabilities_ack']=reply(message,before,ids['capabilities'])

        # Drain queued/coalesced input and any incomplete frame BEFORE sending
        # our request. A pre-request SHUTDOWN must never be attributed to it.
        while True:
            while stream.pending:
                message,before=stream.pending.popleft()
                reply(message,before,None)
            if stream.buffer:stream.receive();continue
            if not stream.receive(blocking=False):break
        stream.send('system_powerdown',ids['powerdown'])
        result['powerdown_requested']=True
        result['powerdown_request_monotonic']=now()
        while True:
            message,before=stream.next()
            expected=None if result['powerdown_ack'] else ids['powerdown']
            if reply(message,before,expected):result['powerdown_ack']=True
            if result['powerdown_ack'] and result['guest_shutdown_observed']:
                # Validate all already-buffered frames rather than ignoring a
                # wrong-ID/error coalesced after the apparently sufficient pair.
                if stream.pending:continue
                if stream.buffer:stream.receive();continue
                stream.remaining();result['protocol_complete']=True;break
    except Exception as error:
        result['error']=repr(error)
    finally:
        if connection is not None:
            try:connection.close()
            except Exception as cleanup:
                result['cleanup_error']=repr(cleanup);result['protocol_complete']=False
        result['finished_monotonic']=now()
        result['elapsed_seconds']=result['finished_monotonic']-start
        if result['finished_monotonic']>end:
            result.setdefault('error','Whole QMP observation deadline exceeded')
            result['protocol_complete']=False
    return result
