# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import json
import os
from pathlib import Path
import socket
import tempfile
import threading
import unittest
from unittest import mock

import g4_qmp_shutdown as qmp

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
GREETING={'QMP':{'version':{'qemu':{'major':9,'minor':2,'micro':0},'package':''},'capabilities':[]}}


def frame(value):return json.dumps(value,separators=(',',':')).encode()+b'\r\n'
def ack(command):return {'return':{},'id':command['id']}
def event(name='SHUTDOWN',guest=True,reason='guest-shutdown'):
    value={'event':name,'timestamp':{'seconds':123,'microseconds':456}}
    if name=='SHUTDOWN':value['data']={'guest':guest,'reason':reason}
    return value


class Clock:
    def __init__(self):self.value=0
    def now(self):return self.value


class FakeSocket:
    def __init__(self,oncommand=None,greeting=None,clock=None):
        self.queue=[frame(GREETING) if greeting is None else greeting]
        self.commands=[];self.timeouts=[];self.timeout=None;self.closed=False
        self.oncommand=oncommand or self.healthy;self.clock=clock or Clock()
    def settimeout(self,value):self.timeout=value;self.timeouts.append(value)
    def connect(self,path):self.endpoint=path
    def sendall(self,data):
        command=json.loads(data);self.commands.append(command)
        self.queue.extend(self.oncommand(command))
    def healthy(self,command):
        if command['execute']=='qmp_capabilities':return [frame(ack(command))]
        return [frame(ack(command))+frame(event())]
    def recv(self,n):
        if not self.queue:
            if self.timeout==0:raise BlockingIOError()
            self.clock.value+=self.timeout
            raise TimeoutError('fake socket receive timeout')
        data=self.queue.pop(0)
        if isinstance(data,Exception):raise data
        if len(data)>n:self.queue.insert(0,data[n:])
        return data[:n]
    def close(self):self.closed=True


class QmpTests(unittest.TestCase):
    def observe(self,**kwargs):
        client=FakeSocket(**kwargs)
        result=qmp.observe_shutdown('/owned/qmp.sock',socket_factory=lambda:client,now=client.clock.now)
        self.assertTrue(client.closed)
        self.assertFalse(any(c['execute'] not in ('qmp_capabilities','system_powerdown') for c in client.commands))
        self.assertNotIn('graceful',result);self.assertNotIn('graceful_powerdown',result)
        return result,client

    def test_matched_ack_and_guest_event_only_protocol_evidence(self):
        result,client=self.observe()
        self.assertTrue(result['protocol_complete']);self.assertTrue(result['guest_shutdown_observed'])
        self.assertTrue(result['capabilities_ack']);self.assertTrue(result['powerdown_ack'])
        self.assertEqual([c['execute'] for c in client.commands],['qmp_capabilities','system_powerdown'])
        self.assertNotEqual(client.commands[0]['id'],client.commands[1]['id'])
        other,_=self.observe();self.assertNotEqual(result['request_ids'],other['request_ids'])

    def test_post_request_event_before_ack_retained(self):
        def commands(c):
            if c['execute']=='qmp_capabilities':return [frame(ack(c))]
            return [frame(event('POWERDOWN'))+frame(event())+frame(ack(c))]
        result,_=self.observe(oncommand=commands)
        self.assertTrue(result['protocol_complete'])
        self.assertEqual([v['message']['event'] for v in result['events']],['POWERDOWN','SHUTDOWN'])
        self.assertTrue(all(not v['before_powerdown_request'] for v in result['events']))

    def test_pre_request_coalesced_or_partial_shutdown_not_attributed(self):
        for partial in (False,True):
            def commands(c):
                if c['execute']=='qmp_capabilities':
                    data=frame(event())
                    return [frame(ack(c))+data[:20],data[20:]] if partial else [frame(ack(c))+data]
                return [frame(ack(c)),b'']
            result,_=self.observe(oncommand=commands)
            self.assertFalse(result['protocol_complete']);self.assertFalse(result['guest_shutdown_observed'])
            self.assertTrue(result['events'][0]['before_powerdown_request'])
            self.assertIn('EOF',result['error'])

    def test_powerdown_ack_or_exit_eof_never_sufficient(self):
        for messages in ([],[event('POWERDOWN')]):
            def commands(c):
                return [frame(ack(c))] if c['execute']=='qmp_capabilities' else [frame(ack(c))+b''.join(frame(m) for m in messages),b'']
            result,_=self.observe(oncommand=commands)
            self.assertFalse(result['protocol_complete']);self.assertTrue(result['powerdown_ack'])
            self.assertFalse(result['guest_shutdown_observed'])

    def test_shutdown_event_without_ack_is_incomplete(self):
        result,_=self.observe(oncommand=lambda c:[frame(ack(c))] if c['execute']=='qmp_capabilities' else [frame(event()),b''])
        self.assertTrue(result['guest_shutdown_observed']);self.assertFalse(result['powerdown_ack'])
        self.assertFalse(result['protocol_complete'])

    def test_non_guest_reasons_and_reset_not_accepted(self):
        invalid=[event(guest=False,reason='host-signal'),event(reason='guest-reset'),
                 event(guest=False,reason='host-qmp-quit'),event('RESET'),event(guest=1)]
        for message in invalid:
            result,_=self.observe(oncommand=lambda c:[frame(ack(c))] if c['execute']=='qmp_capabilities' else [frame(ack(c))+frame(message)])
            self.assertFalse(result['protocol_complete']);self.assertIn('error',result)

    def test_explicit_error_missing_or_wrong_id_retained(self):
        for phase in ('qmp_capabilities','system_powerdown'):
            for has_id in (False,True):
                def commands(c):
                    if c['execute']!=phase:return [frame(ack(c))]
                    message={'error':{'class':'GenericError','desc':'original error'}}
                    if has_id:message['id']=c['id']
                    return [frame(message)]
                result,_=self.observe(oncommand=commands)
                self.assertFalse(result['protocol_complete'])
                self.assertEqual(result['qmp_error']['error']['desc'],'original error')

    def test_wrong_duplicate_and_coalesced_tail_ids_fail(self):
        for mode in ('wrong','duplicate','tail'):
            def commands(c):
                if c['execute']=='qmp_capabilities':return [frame(ack(c))]
                wrong={'return':{},'id':'foreign'}
                if mode=='wrong':return [frame(wrong)]
                if mode=='duplicate':return [frame(ack(c))+frame(ack(c))+frame(event())]
                return [frame(ack(c))+frame(event())+frame(wrong)]
            result,_=self.observe(oncommand=commands)
            self.assertFalse(result['protocol_complete']);self.assertIn('ID',result['error'])

    def test_malformed_duplicate_nonfinite_and_partial_frames(self):
        for data in (b'[]\n',b'{"QMP":{},"QMP":{}}\n',b'{"QMP":NaN}\n',b'\xff\n',
                     b'not json\n',b'\n',b'{}\n',b'x'*(qmp.FRAME_BYTES+1)):
            result,_=self.observe(greeting=data)
            self.assertFalse(result['protocol_complete']);self.assertIn('error',result)
            self.assertTrue(result['transcript'])
        client=FakeSocket(greeting=b'{"QMP":');client.queue.append(b'')
        result=qmp.observe_shutdown('fake',socket_factory=lambda:client,now=client.clock.now)
        self.assertIn('EOF',result['error']);self.assertFalse(result['protocol_complete'])

    def test_greeting_and_event_schema_types(self):
        invalid=[{'QMP':{'version':{'qemu':{'major':True,'minor':0,'micro':0},'package':''},'capabilities':[]}},
                 {'QMP':{'version':{'qemu':{'major':1,'minor':0,'micro':0},'package':''},'capabilities':[False]}}]
        for message in invalid:
            result,_=self.observe(greeting=frame(message));self.assertFalse(result['protocol_complete'])
        for stamp in ({'seconds':True,'microseconds':0},{'seconds':1,'microseconds':1000000},
                      {'seconds':-1,'microseconds':0}):
            message=event();message['timestamp']=stamp
            result,_=self.observe(oncommand=lambda c:[frame(ack(c))] if c['execute']=='qmp_capabilities' else [frame(ack(c))+frame(message)])
            self.assertFalse(result['protocol_complete'])

    def test_limits_frames_messages_transcript_bytes_and_records(self):
        for name,value in (('FRAME_BYTES',32),('MESSAGES',2),('TRANSCRIPT_BYTES',100),('TRANSCRIPT_RECORDS',2)):
            with mock.patch.object(qmp,name,value):
                result,_=self.observe()
            self.assertFalse(result['protocol_complete']);self.assertIn('quota',result['error'])

    def test_single_whole_deadline_and_connect_send_cleanup_failures(self):
        clock=Clock();client=FakeSocket(clock=clock)
        client.oncommand=lambda c:[frame(ack(c))]  # no SHUTDOWN
        result=qmp.observe_shutdown('fake',maximum_seconds=7,socket_factory=lambda:client,now=clock.now)
        self.assertEqual(result['elapsed_seconds'],7);self.assertFalse(result['protocol_complete'])
        self.assertTrue(all(0<=v<=7 for v in client.timeouts))
        # Negotiation time consumes the SAME budget, not a fresh event timeout.
        clock=Clock();client=FakeSocket(clock=clock)
        recv,send,connect=client.recv,client.sendall,client.connect
        def delayed_recv(n):
            if client.queue:clock.value+=.25
            return recv(n)
        def delayed_send(data):clock.value+=.25;send(data)
        def delayed_connect(path):clock.value+=1;connect(path)
        client.recv=delayed_recv;client.sendall=delayed_send;client.connect=delayed_connect
        client.oncommand=lambda c:[frame(ack(c))]
        result=qmp.observe_shutdown('fake',maximum_seconds=7,socket_factory=lambda:client,now=clock.now)
        self.assertEqual(result['elapsed_seconds'],7);self.assertLess(client.timeouts[-1],7)
        client=FakeSocket()
        with mock.patch.object(client,'connect',side_effect=OSError('primary connect')), \
             mock.patch.object(client,'close',side_effect=OSError('secondary close')):
            result=qmp.observe_shutdown('fake',socket_factory=lambda:client,now=client.clock.now)
        self.assertIn('primary connect',result['error']);self.assertIn('secondary close',result['cleanup_error'])
        client=FakeSocket()
        with mock.patch.object(client,'sendall',side_effect=TimeoutError('send deadline')):
            result=qmp.observe_shutdown('fake',socket_factory=lambda:client,now=client.clock.now)
        self.assertIn('send deadline',result['error']);self.assertFalse(result['transcript'][-1]['completed'])
        for limit in (True,0,51,float('inf'),float('nan')):
            with self.assertRaises(ValueError):qmp.observe_shutdown('unused',maximum_seconds=limit)

    @unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'Linux/D: private storage required')
    def test_real_socketpair_fragmentation_coalescing_and_event_order(self):
        for before_ack in (False,True,'pre-request'):
            with tempfile.TemporaryDirectory(prefix='qmp-socketpair-',dir=LAB) as temp:
                client,server=socket.socketpair();server.settimeout(2)
                failures=[];commands=[]
                def peer():
                    try:
                        data=frame(GREETING)
                        for part in (data[:3],data[3:17],data[17:]):server.sendall(part)
                        reader=server.makefile('rb')
                        cap=json.loads(reader.readline());commands.append(cap)
                        server.sendall(frame(ack(cap))+(frame(event()) if before_ack=='pre-request' else b''))
                        down=json.loads(reader.readline());commands.append(down)
                        parts=([ack(down)] if before_ack=='pre-request' else
                               [event('POWERDOWN'),event(),ack(down)] if before_ack else [ack(down),event()])
                        wire=b''.join(frame(m) for m in parts)
                        server.sendall(wire[:7]);server.sendall(wire[7:])
                        reader.close()
                    except Exception as error:failures.append(repr(error))
                    finally:server.close()
                worker=threading.Thread(target=peer,daemon=True);worker.start()
                class Connected:
                    def connect(self,path):pass
                    def settimeout(self,n):client.settimeout(n)
                    def recv(self,n):return client.recv(n)
                    def sendall(self,b):client.sendall(b)
                    def close(self):client.close()
                result=qmp.observe_shutdown('original socketpair',maximum_seconds=2,socket_factory=Connected)
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive());self.assertEqual(failures,[])
                self.assertEqual(result['protocol_complete'],before_ack!='pre-request',result.get('error'))
                self.assertEqual([c['execute'] for c in commands],['qmp_capabilities','system_powerdown'])
                self.assertTrue(all(e['before_powerdown_request']==(before_ack=='pre-request') for e in result['events']))
                self.assertTrue(any(base64.b64decode(t['base64']) for t in result['transcript']))
                (Path(temp)/'socketpair.json').write_text(json.dumps(result))


if __name__=='__main__':unittest.main()
