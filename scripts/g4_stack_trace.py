# SPDX-License-Identifier: GPL-2.0-or-later
"""Strict selected-TID WineDbg text evidence, NOT debugger execution/detach proof.

Formats: Wine4315be83 tgt_active.c dbg_attach_debuggee and stack.c
thread_display/backtrace_tid/backtrace. Initial unbound backtraces are NOT mapped.
"""
import re
from g4_stack_commands import identity


def parse(text,record,image):
    record=identity(record)
    if not isinstance(text,str) or len(text.encode())>1024**2: raise ValueError('Debugger text size limit')
    text=text.replace('\r\n','\n')
    if re.search(r"Unknown thread id|No such thread|No process loaded|Can't (?:attach|debug|suspend|backtrace|get context)|"
                 r"Couldn't (?:initiate|load|get first exception)|WineDbg internal error|syntax error|parse error|Undefined symbol|"
                 r"Process of pid=.*has terminated|You must be attached|No symbols found",text,re.I):
        raise ValueError('Debugger attach/parser/thread/frame error')
    attached=[int(v,16) for v in re.findall(r'^WineDbg attached to pid ([0-9a-fA-F]{4,8})$',text,re.M)]
    if attached!=[record['pid']] or text.count('WineDbg attached to pid ')!=1: raise ValueError('Missing/foreign/duplicate attached PID')
    header=re.compile(r'^Backtracing for thread ([0-9a-fA-F]{4,8})(?: \([^\n]{0,256}\))? in process ([0-9a-fA-F]{4,8}) \(([^\n]{1,2048})\):$',re.M)
    sections=list(header.finditer(text));selected=[record['main_tid'],record['worker_tid']]
    if len(sections)!=2 or text.count('Backtracing for thread ')!=2: raise ValueError('Two explicit selected-TID headers required; current-TID/unbound backtrace insufficient')
    result=[]
    for i,h in enumerate(sections):
        tid,pid=int(h[1],16),int(h[2],16)
        if tid!=selected[i] or pid!=record['pid'] or h[3].casefold()!=image.casefold():
            raise ValueError('Wrong selected TID/process/image header')
        body=text[h.end():sections[i+1].start() if i+1<len(sections) else len(text)]
        if body.count('Backtrace:')!=1: raise ValueError('Missing/ambiguous bound Backtrace')
        frames=[]
        for line in body.splitlines():
            m=re.fullmatch(r'(?:=>|  )(\d+) ((?:0x)?[0-9a-fA-F]{4,})(.*?) \(((?:0x)?[0-9a-fA-F]{4,})\)',line)
            if not m:
                if re.match(r'(?:=>|  )\d+ ',line): raise ValueError('Malformed frame record')
                continue
            module=re.search(r'\bin ([A-Za-z0-9_.-]+)(?:\s|$)',m[3])
            if not module or not 0<int(m[2],16)<2**64 or not 0<=int(m[4],16)<2**64: raise ValueError('Unusable frame/module')
            frames.append({'index':int(m[1]),'address':m[2],'module':module[1].casefold(),'text':line})
        if not frames or [f['index'] for f in frames]!=list(range(len(frames))): raise ValueError('Missing/invalid frame records')
        modules={f['module'].removesuffix('.dll').removesuffix('.exe') for f in frames}
        if 'stack-waiter' not in modules or not modules.intersection({'ntdll','kernelbase','kernel32'}):
            raise ValueError('Useful original-waiter and Windows-wait-module frames required')
        result.append({'win32_tid':tid,'win32_pid':pid,'frames':frames})
    return {'attached_win32_pid':record['pid'],'selected_stacks':result,
            'detach_api_return_proven':False,'note':'Detach is silent; separate post-identity/release/exit proof required'}
