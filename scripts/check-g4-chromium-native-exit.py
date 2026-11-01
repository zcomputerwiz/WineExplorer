#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ordinary native parent/held-handle Chromium status diagnostic; always exit1."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import time

from g4_diagnostic_inventory import observe_file,require_regular_canonical
from g4_edge_http_fixture import FixtureServer
import g4_chromium_cache as cache

def load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

cal=load('check-g4-native-exit-calibration')
LAB,BUILD,PREFIX=cal.LAB,cal.BUILD,cal.PREFIX
HOST='chromium-native-exit-r1-20261024'
STAGE=LAB/(HOST+'-stage')
OUTPUT=LAB/'chromium-headful-native-exit-r1-20261023'
PROFILE=LAB/'chromium-headful-native-exit-r1-20261023/profile'
RUNTIME=LAB/'chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win'
HELPER=STAGE/'original/native-exit.exe'
MANIFEST=STAGE/'runtime-manifest.json'
FIXTURE=STAGE/'scripts/g4-edge-probe/index.html'
MARKER='8fc037ca464bc22952c9f9d63447cf0feb7dcab07e20489cb2d3a4ef3e1a8ba7'
TOTAL,CLEANUP,OPERATION=285,55,82 # startup30 + native wait45 + reap5 + margin2
BASE_DEBUG='-all,+timestamp,+pid,+tid,+winediag,+process'
DEBUG=BASE_DEBUG
MODE='r1'
HELPER_PREVIOUS={'sha256':cal.PES['native-exit.exe']}

def configure(mode):
    """Fixed diagnostic contracts only; no executable/flag/path passthrough."""
    global HOST,STAGE,OUTPUT,PROFILE,HELPER,MANIFEST,FIXTURE,DEBUG,MODE,HELPER_PREVIOUS,BUILD,PREFIX
    if mode in ('token-wait-r9','token-wait-r10','token-pipe-r11','token-pipe-r12','token-wait-r13','token-wait-r14'):
        configure('token-fixed-r8')
        MODE=mode;HOST='chromium-'+mode+'-20261031'
        if mode in ('token-wait-r13','token-wait-r14'):HOST='chromium-'+mode+'-20261101'
        STAGE=LAB/(HOST+'-stage');OUTPUT=LAB/HOST;PROFILE=OUTPUT/'profile'
        HELPER=STAGE/'original/native-exit.exe';MANIFEST=STAGE/'runtime-manifest.json'
        FIXTURE=STAGE/'scripts/g4-edge-probe/index.html'
        BUILD=LAB/(HOST+'-wine/build/wow64');PREFIX=LAB/(HOST+'-prefix')
        cal.BUILD=BUILD;cal.PREFIX=PREFIX;cal.APPROVED_LOADER=BUILD/'loader/wine'
        if mode=='token-pipe-r11':
            # WO-20261031-CHROMIUM-TOKEN-PIPE-R11: add the named-pipe transport channels
            # (browser-side CreateNamedPipeW/"sync", child-side CreateFileW/"file") on top
            # of the r8 enriched mask; no wineserver -d request trace yet.
            DEBUG=DEBUG+',+file,+sync'
        elif mode=='token-pipe-r12':
            DEBUG=DEBUG+',+wex_mojo'
        elif mode=='token-wait-r14':
            DEBUG=DEBUG+',+wex_process'
        return
    if mode not in ('r1','module-warn-r2','userenv-r3','userenv-r4','userenv-r6','runtime-unblock','renderer-unblock','observation-control','observation-control-r2','memory-8g','navigation-unblock','navigation-phase','browser-boundary','diagnostic','verify-r1','verify-r2','verify-r3','verify-r4','verify-r5','verify-r6','token-fixed-r7','token-fixed-r8'):raise ValueError('Unknown fixed Chromium mode')
    MODE=mode
    HOST={'r1':'chromium-native-exit-r1-20261024','module-warn-r2':'chromium-native-module-r2-20261024','userenv-r3':'chromium-userenv-r3-20261024','userenv-r4':'chromium-userenv-r4-20261025','userenv-r6':'chromium-registration-r6-20261026','runtime-unblock':'chromium-runtime-unblock-20261026','renderer-unblock':'chromium-renderer-unblock-20261026','observation-control':'chromium-observation-control-20261026','observation-control-r2':'chromium-observation-control-r2-20261026','memory-8g':'chromium-memory-8g-20261028','navigation-unblock':'chromium-navigation-unblock-20261028','navigation-phase':'chromium-navigation-phase-20261028','browser-boundary':'chromium-browser-boundary-20261028','diagnostic':'chromium-browser-diagnostic-20261028','verify-r1':'chromium-browser-verify-r1-20261029','verify-r2':'chromium-browser-verify-r2-20261029','verify-r3':'chromium-browser-verify-r3-20261029','verify-r4':'chromium-browser-verify-r4-20261029','verify-r5':'chromium-browser-verify-r5-20261029','verify-r6':'chromium-combined-r6-20261030','token-fixed-r7':'chromium-token-fixed-r7-20261031','token-fixed-r8':'chromium-token-fixed-r8-20261031'}[mode]
    STAGE=LAB/(HOST+'-stage')
    OUTPUT=LAB/('chromium-headful-native-exit-r1-20261023' if mode=='r1' else HOST)
    PROFILE=OUTPUT/'profile' # nested under the fresh OUTPUT; aligns with native WEX_EXIT_OUTPUT+"\profile"
    HELPER=STAGE/'original/native-exit.exe';MANIFEST=STAGE/'runtime-manifest.json'
    FIXTURE=STAGE/'scripts/g4-edge-probe/index.html'
    DEBUG=BASE_DEBUG+(',warn+module,trace+seh' if mode!='r1' else '')
    if mode in ('diagnostic','verify-r1','verify-r2','verify-r3','verify-r4','verify-r5','verify-r6','token-fixed-r7','token-fixed-r8'):
        # WO-20261028-CHROMIUM-TARGETED-DIAGNOSTIC / WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1/2/3/4/5:
        # enriched channels to localize the FIRST failing gate in the Chromium kNetwork sandbox launch
        # pipeline (userenv profile + security/token calls). verify-r1/r2/r3/r4/r5 reuse this exact WINEDEBUG.
        DEBUG='-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security'
    HELPER_PREVIOUS={'sha256':cal.PES['native-exit.exe']} if mode=='r1' else None
    if mode in ('memory-8g','navigation-unblock','navigation-phase','browser-boundary','diagnostic'):
        # Reuse the last deployed matched combined candidate from the control-channel run
        # (02d4ef2 tracepoints over fffa7de); do NOT rebuild Wine or redeploy traces. Point
        # launch + CORE/loader observation + identity loader at it. (memory-8g: RAM-only;
        # navigation-unblock: bounded NetLog boundary diagnostic on the same candidate;
        # navigation-phase: combined scoped-VLOG + fixed-NetLog + startup-trace phase probe.)
        BUILD=LAB/'wine-control-channel-20261027/build/wow64'
        cal.BUILD=BUILD
        cal.APPROVED_LOADER=BUILD/'loader/wine' # identity attestation must name the candidate loader, not the import-time build
    elif mode in ('verify-r1','verify-r2','verify-r3','verify-r4','verify-r5','verify-r6'):
        # WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1/2/3/4/5: ISOLATED coherent candidate at my AppContainer
        # fix base (002153a + userenv/kernelbase rebuilt from f01bba45). The verify trial deploys it to a
        # FRESH private path via hardlink-copy of the control-channel candidate + overlay of exactly the
        # components that differ between the bases (userenv/kernelbase/ntdll x2, wineserver), verified by
        # SHA-256. NOT the reused control-channel path -- this run must test the fix in isolation.
        BUILD=LAB/('wine-verify-r6-20261030/build/wow64' if mode=='verify-r6' else 'wine-verify-r5-20261029/build/wow64' if mode=='verify-r5' else 'wine-verify-r4-20261029/build/wow64' if mode=='verify-r4' else 'wine-verify-r3-20261029/build/wow64' if mode=='verify-r3' else 'wine-verify-r2-20261029/build/wow64' if mode=='verify-r2' else 'wine-verify-r1-20261029/build/wow64')
        cal.BUILD=BUILD
        cal.APPROVED_LOADER=BUILD/'loader/wine' # identity attestation must name the candidate loader, not the import-time build
        cal.APPROVED_LOADER=BUILD/'loader/wine' # identity attestation must name the candidate loader, not the import-time build
    elif mode in ('token-fixed-r7','token-fixed-r8'):
        # WO-20261031-CHROMIUM-TOKEN-FIXED-R7: real Chromium against the preserved recovered R6
        # runtime (wine-recovered-r6-20261031), in a FRESH guest copy carrying the accepted
        # server/token.c fix (Wine 59f4bc31, server 196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800).
        # Never the shared edge-install prefix: a dedicated clone of the preserved stopped browser
        # prefix, so this run cannot leave behind state the next diagnostic would inherit.
        BUILD=LAB/(HOST+'-wine/build/wow64')
        cal.BUILD=BUILD
        cal.APPROVED_LOADER=BUILD/'loader/wine' # identity attestation must name the candidate loader, not the import-time build
        PREFIX=LAB/(HOST+'-prefix')
        cal.PREFIX=PREFIX # guards()/run() read PREFIX from this module; ownership/dosdevices checks are unchanged

def child_args(nonce,url):
    if (not isinstance(nonce,str) or not re.fullmatch('[0-9a-f]{32}',nonce)
            or not isinstance(url,str) or not re.fullmatch(r'http://127\.0\.0\.1:([1-9][0-9]{0,4})/'+nonce+'/index.html#nonce='+nonce,url)
            or not 1<=int(url.split(':')[2].split('/')[0])<=65535):raise ValueError('Exact nonce/private loopback URL required')
    args=[cal.win(RUNTIME/'chrome.exe'),'--app='+url,'--user-data-dir='+cal.win(PROFILE),
        '--no-first-run','--no-default-browser-check','--enable-logging','--log-file='+cal.win(OUTPUT/'chromium.log'),'--v=1']
    if MODE=='navigation-unblock':
        # Bounded logging-only NetLog (Default capture, 8MiB) that flushes before the 45s cutoff;
        # must exactly mirror the WEX_NETLOG helper contract so the held-handle binding matches.
        args+=['--log-net-log='+cal.win(OUTPUT/'netlog.json'),'--net-log-max-size-mb=8','--net-log-duration=40s']
    elif MODE in ('navigation-phase','browser-boundary'):
        # WO-20261028-CHROMIUM-NAVIGATION-PHASE: combined bounded diagnostic (scoped VLOG + fixed
        # integer NetLog 40s + best-effort startup trace 35s). Must EXACTLY mirror the WEX_NAVPHASE
        # helper contract (same flags, same order) or binding() rejects the held-handle command line.
        args+=['--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2',
               '--log-net-log='+cal.win(OUTPUT/'netlog.json'),'--net-log-max-size-mb=8','--net-log-duration=40',
               '--trace-startup','--trace-startup-duration=35','--trace-startup-file='+cal.win(OUTPUT/'trace.json')]
    elif MODE in ('diagnostic','verify-r1'):
        # WO-20261028-CHROMIUM-TARGETED-DIAGNOSTIC / WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1: same
        # combined bounded diagnostic as navigation-phase, plus */sandbox/* and */base/process/* in the
        # scoped VLOG to expose the kNetwork launch pipeline's policy/token/CreateProcess gates. verify-r1
        # reuses this exact child-arg set (identical WO-required VLOG). Must EXACTLY mirror the WEX_DIAGNOSTIC
        # helper contract (same flags, same order) or binding() rejects the held-handle command line.
        args+=['--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2',
               '--log-net-log='+cal.win(OUTPUT/'netlog.json'),'--net-log-max-size-mb=8','--net-log-duration=40',
               '--trace-startup','--trace-startup-duration=35','--trace-startup-file='+cal.win(OUTPUT/'trace.json')]
    elif MODE in ('verify-r2','verify-r3','verify-r4','verify-r5','verify-r6','token-fixed-r7','token-fixed-r8','token-wait-r9','token-wait-r10','token-pipe-r11','token-pipe-r12','token-wait-r13','token-wait-r14'):
        # WO-20261029-CHROMIUM-VERIFICATION-ITERATION-2/3/4/5 + WO-20261030-CHROMIUM-COMBINED r6 +
        # WO-20261031-CHROMIUM-TOKEN-FIXED-R7: level 3 scoped VLOG to expose the decisive pre-spawn
        # policy decision and failure cause, plus startup NetLog and trace. Same shape for all three
        # so binding()/parse() need no changes and the sandbox decision stays directly comparable.
        args+=['--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3',
               '--log-net-log='+cal.win(OUTPUT/'netlog.json'),'--net-log-max-size-mb=8','--net-log-duration=40',
               '--trace-startup','--trace-startup-duration=35','--trace-startup-file='+cal.win(OUTPUT/'trace.json')]
    return args

def environment(output):
    env=cal.install.environment(output)
    env.update(WINEPREFIX=str(PREFIX),WINEDEBUG=DEBUG)
    if MODE in ('token-fixed-r7','token-fixed-r8','token-wait-r9','token-wait-r10','token-pipe-r11','token-pipe-r12','token-wait-r13','token-wait-r14'):
        env.update(WINE=str(BUILD/'wine'),WINESERVER=str(BUILD/'server/wineserver'))
    return env

def command(nonce,url):
    child_args(nonce,url)
    return [str(BUILD/'wine'),str(HELPER),'--chromium','--image',cal.win(RUNTIME/'chrome.exe'),
        '--profile',cal.win(PROFILE),'--log',cal.win(OUTPUT/'chromium.log'),
        '--events',cal.win(OUTPUT/'native-exit.jsonl'),'--nonce',nonce,'--url',url]

def binding(value,nonce,image,url):
    keys={'v','event','mode','nonce','helper_pid','child_pid','creation_time','image','command_line','creation_flags',
          'inherit_handles','environment','manifest_sha256','manifest_verified_by_native','tick_ms'}
    if (not isinstance(value,dict) or set(value)!=keys or type(value['v']) is not int or value['v']!=1
            or value['event']!='bound' or value['mode']!='chromium' or value['nonce']!=nonce
            or not cal.uint(value['helper_pid'],32) or not value['helper_pid'] or not cal.uint(value['child_pid'],32)
            or not value['child_pid'] or value['child_pid']==value['helper_pid'] or not cal.uint(value['creation_time']) or not value['creation_time']
            or not isinstance(value['image'],str) or value['image'].casefold()!=image.casefold()
            or value['command_line']!=' '.join('"'+s+'"' for s in child_args(nonce,url))
            or type(value['creation_flags']) is not int or value['creation_flags']!=0 or value['inherit_handles'] is not False
            or value['environment']!='inherited_NULL' or value['manifest_sha256']!=MARKER or value['manifest_verified_by_native'] is not False
            or not cal.uint(value['tick_ms'])):raise ValueError('Invalid Chromium held-handle binding')
    return value

def parse(raw,nonce,image,url):
    if type(raw) is not bytes or len(raw)>cal.EVENT_LIMIT or not raw.endswith(b'\n') or not 1<=raw.count(b'\n')<=3:
        raise ValueError('Missing/partial/bounded native journal')
    rows=[cal.decode(line) for line in raw.decode('utf-8').splitlines()]
    if not rows or any(not isinstance(v,dict) for v in rows):raise ValueError('Native event objects required')
    first=None
    if rows[0].get('event')=='bound':first=binding(rows.pop(0),nonce,image,url)
    if len(rows)!=1:raise ValueError('Exactly one terminal native event required')
    end=rows[0];event=end.get('event')
    if event=='failure':
        keys={'v','event','nonce','phase','win32_error','child_pid','wait_result','tick_ms'}
        phases={'create','close_thread','identity_race_before','identity_query','identity_race_after','binding_flush','timeout_flush','wait','exit_query','exit_flush'}
        if (set(end)!=keys or end['phase'] not in phases or not cal.uint(end['win32_error'],32)
                or not cal.uint(end['child_pid'],32) or not cal.uint(end['wait_result'],32)):raise ValueError('Malformed native failure')
    elif event in ('exit','timeout'):
        if first is None:raise ValueError('Terminal status without native binding')
        extra={'exit_code','exit_hex'} if event=='exit' else {'wait_ms'}
        if (set(end)!={'v','event','nonce','child_pid','creation_time','wait_result','tick_ms'}|extra
                or type(end['child_pid']) is not int or end['child_pid']!=first['child_pid']
                or type(end['creation_time']) is not int or end['creation_time']!=first['creation_time']):raise ValueError('Terminal native identity mismatch')
        if event=='exit':
            if (type(end['wait_result']) is not int or end['wait_result']!=0 or not cal.uint(end['exit_code'],32)
                    or end['exit_hex']!=f"0x{end['exit_code']:08x}"):raise ValueError('Unsigned signaled native status required')
        elif type(end['wait_result']) is not int or end['wait_result']!=258 or type(end['wait_ms']) is not int or end['wait_ms']!=45000:
            raise ValueError('Invalid native45s timeout')
    else:raise ValueError('Unknown terminal native event')
    if (type(end.get('v')) is not int or end['v']!=1 or end.get('nonce')!=nonce or not cal.uint(end.get('tick_ms'))
            or (first is not None and not 0<=end['tick_ms']-first['tick_ms']<=50000)
            or (first is not None and end.get('child_pid')!=first['child_pid'])):raise ValueError('Native terminal nonce/time/PID mismatch')
    result=dict(outcome='native_'+event,bound=first,terminal=end,manifest_marker_verified=False)
    if event=='exit':result.update(native_child_status=end['exit_code'],native_child_hex=end['exit_hex'])
    return result

def runtime_state(manifest=None,*,zip_path=None,extract=False,now=time.monotonic,deadline=None):
    """Reuse complete runtime read-only; extract ONLY into absent fixed directory."""
    end=min(now()+30,deadline) if deadline is not None else now()+30
    record=json.loads(require_regular_canonical(MANIFEST if manifest is None else manifest).read_text());expected=record['files']
    if not expected or any(not n.startswith('chrome-win/') or '..' in Path(n).parts or Path(n).is_absolute() for n in expected):raise ValueError('Invalid runtime inventory paths')
    if RUNTIME.resolve()!=RUNTIME or RUNTIME.is_symlink():raise ValueError('Noncanonical fixed runtime')
    if not os.path.lexists(RUNTIME):
        if os.path.lexists(RUNTIME.parent):raise ValueError('Existing partial runtime directory; never repair')
        required=sum(v['bytes'] for v in expected.values())+2*1024**3
        if not extract:return dict(state='absent',required_free_bytes=required,runtime=str(RUNTIME))
        if zip_path is None or cache.digest(zip_path)!=record['zip_sha256']:raise ValueError('Current ZIP transfer checksum mismatch')
        if shutil.disk_usage(LAB).free<required:raise ValueError('Insufficient space before runtime extraction')
        RUNTIME.parent.parent.mkdir(parents=True,exist_ok=True)
        cache.extract(zip_path,RUNTIME.parent,now=now,timeout=120)
        end=now()+30 if deadline is None else min(now()+30,deadline)
    require_regular_canonical(RUNTIME/'chrome.exe')
    paths=list(RUNTIME.rglob('*'))
    if any(p.is_symlink() for p in paths):raise ValueError('Runtime link not allowed')
    actual={p.relative_to(RUNTIME.parent).as_posix():p for p in paths if p.is_file()}
    missing=set(expected)-set(actual)
    if missing:raise ValueError('Missing required runtime files (%d): %s'%(len(missing),sorted(missing)[:5]))
    extra=sorted(set(actual)-set(expected)) # benign generated/surplus files; tolerated, not a failure
    observed={}
    for name in expected:
        if now()>=end:raise TimeoutError('Runtime observation deadline')
        observed[name]=observe_file(actual[name],expected[name],deadline=end,now=now)
    for name in ('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll'):cache.pe(RUNTIME/name)
    return dict(state='complete',runtime=str(RUNTIME),files=observed,extras=extra,required_free_bytes=1024**3,
        version_source='Actual per-file hashes; historical manifest version is not actual version attestation')

def guards(output,helpers,deadline,now):
    if output!=OUTPUT or os.path.lexists(output) or output.resolve()!=output:raise ValueError('Exact fresh compiled Chromium output required')
    for path in (STAGE,HELPER,FIXTURE,MANIFEST,PREFIX):
        if path.resolve(strict=True)!=path:raise ValueError('Noncanonical required path')
    for path in (BUILD/'wine',BUILD/'loader/wine',BUILD/'server/wineserver'):
        resolved=path.resolve(strict=True)
        if not resolved.is_relative_to(BUILD.resolve(strict=True)):raise ValueError('Wine path outside intended build')
        require_regular_canonical(resolved)
    require_regular_canonical(LAB/'labwc')
    if helpers.matching_servers(BUILD/'server/wineserver',PREFIX) or cal.graphics_resources.sample(PREFIX):raise ValueError('Owned prefix not idle')
    lo=cal.isolation.verify_private_loopback()
    if (PREFIX/'dosdevices/z:').resolve(strict=True)!=Path('/'):raise ValueError('Wrong Z: mapping')
    if (PREFIX/'dosdevices/c:').resolve(strict=True)!=(PREFIX/'drive_c').resolve(strict=True):raise ValueError('Wrong C: mapping')
    if shutil.disk_usage(LAB).free<1024**3:raise ValueError('Need1GiB before profile/output creation')
    cal.install.pe_metadata(HELPER)
    runtime=runtime_state(now=now,deadline=deadline)
    if runtime['state']!='complete':raise ValueError('Fixed runtime absent; stage separately before consumer')
    environment=[BUILD/p for p in cal.CORE]+[cal.EDGE,LAB/'labwc']
    return dict(private_loopback=lo,runtime=runtime,helper=observe_file(HELPER,HELPER_PREVIOUS,deadline=deadline,now=now),
        environment={str(p):observe_file(p,deadline=deadline,now=now) for p in environment},
        additional_loader=observe_file(BUILD/'loader/wine',deadline=deadline,now=now))

def log_quota(output):
    cal.logs(output)
    path=output/'chromium.log'
    if path.is_symlink() or (path.exists() and path.stat().st_size>20*1024**2):raise ValueError('Chromium log quota/path')

def run(output,helpers,*,now=time.monotonic,sleep=time.sleep,popen=subprocess.Popen):
    start=now();deadline=start+TOTAL
    approved=guards(output,helpers,deadline-CLEANUP,now) # no output/listener/process on rejected guard
    output.mkdir();result=dict(passed=False,diagnostic_exit=1,observation_complete=False,mode=MODE,
        classification='Changed parentage: ordinary CreateProcess held-handle Chromium status ONLY; never rendering/security acceptance',approved=approved)
    env=None;comp=server=None
    def bound(cap,cleanup=False):
        left=deadline-now()-(0 if cleanup else CLEANUP)
        if left<=0:raise TimeoutError('Whole Chromium native diagnostic deadline')
        return min(cap,left)
    def cleanup(reason):result.setdefault('cleanup_started',dict(reason=reason,monotonic=now()))
    try:
        env=environment(output)
        for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'):Path(env[key]).mkdir()
        result['environment']={k:v for k,v in env.items() if k.startswith(('WINE','WEX_','XDG_')) or k in ('HOME','TMPDIR')}
        helpers.configure_gl_driver(env,'virgl')
        compositor_options={'strip_wineprefix':True} if MODE in ('userenv-r4','userenv-r6','runtime-unblock','renderer-unblock','observation-control','observation-control-r2','memory-8g','navigation-unblock') else {}
        comp=cal.OwnedWexcomp(LAB/'labwc',LAB/'wayland',output,env,'gles2',**compositor_options)
        if bound(55)<55:raise TimeoutError('Compositor budget')
        result['compositor']=comp.start();result['outer']=comp.verify_gpu_renderer('virgl')
        result['inner']=cal.verify_native_gl_driver(env,output,'virgl')
        if MODE in ('userenv-r3','userenv-r4'):
            import g4_userenv_probe
            if bound(129)<129:raise TimeoutError('Insufficient probe45 + browser82 + HTTP2 budget')
            g4_userenv_probe.phase(output,env,result,STAGE/'original/userenv-probe.exe',BUILD,PREFIX,helpers,comp.verify,cal.graphics_resources.sample,
                deadline-CLEANUP,now=now,sleep=sleep,popen=popen)
        elif MODE in ('userenv-r6','runtime-unblock','renderer-unblock','observation-control','observation-control-r2'):
            import g4_userenv_r6_probe
            if bound(129)<129:raise TimeoutError('Insufficient probe45 + browser82 + HTTP2 budget')
            g4_userenv_r6_probe.phase(output,env,result,STAGE/'original/userenv-r6-probe.exe',BUILD,PREFIX,helpers,comp.verify,cal.graphics_resources.sample,
                deadline-CLEANUP,now=now,sleep=sleep,popen=popen)
        nonce=secrets.token_hex(16);server=FixtureServer(FIXTURE,nonce);result['http_fixture']=server.record
        url=server.start(timeout=bound(2));result['url']=url
        observed_at=now()
        def monitor():
            comp.verify();server.check();log_quota(output)
            if MODE=='token-wait-r10' and now()-observed_at>=5 and 'wait_snapshot' not in result:
                from g4_chromium_wait_snapshot import observe
                result['wait_snapshot']=observe(output,env,BUILD,PREFIX,maximum_seconds=bound(8))
            elif MODE in ('token-wait-r13','token-wait-r14') and now()-observed_at>=5 and 'wait_snapshot' not in result:
                from g4_chromium_wait_snapshot import observe_root
                if bound(10)<10:raise TimeoutError('Root snapshot requires ten-second observation budget')
                result['wait_snapshot']=observe_root(output,env,BUILD,PREFIX)
        contract=dict(operation_seconds=OPERATION,nonce=nonce,image=cal.win(RUNTIME/'chrome.exe'),tolerant_periodic_identity=True,
            command=lambda out,n:command(n,url),binding=lambda v,n,i:binding(v,n,i,url),parse=lambda raw,n,i:parse(raw,n,i,url))
        cal.collect(output,env,result,deadline-CLEANUP,cleanup,monitor,now=now,sleep=sleep,popen=popen,contract=contract)
        result['observation_complete']=result['native'].get('outcome')=='native_exit'
    except Exception as error:result['error']=repr(error);result['observation_complete']=False
    finally:
        # Preserve and interpret flushed failure/timeout journal even helper1/early race.
        try:
            raw=cal.journal(output)
            if raw and result.get('nonce') and result.get('url'):
                result['journal_outcome']=parse(raw,result['nonce'],cal.win(RUNTIME/'chrome.exe'),result['url'])
                result['journal_outcome']['accepted_observation']=result['observation_complete']
        except Exception as error:result['journal_parse_error']=repr(error)
        # One optional front, never required for native status or retried.
        if comp is not None:
            try:
                comp.verify();result['front_exit']=helpers.run_logged(['grim','-o','HEADLESS-1',str(output/'terminal-front.png')],env,output,'terminal-front',bound(3)).returncode
            except Exception as error:result['front_error']=repr(error)
        cleanup('prefix_teardown')
        if env is not None and (not result.get('helper_started') or result.get('helper_reaped') is True) and (not result.get('probe_started') or result.get('probe_reaped') is True):
            for label,argv in [('kill',['-k']),('wait',['-w'])]:
                try:result['cleanup_'+label]=helpers.run_logged([str(BUILD/'server/wineserver'),*argv],env,output,'cleanup-'+label,bound(10,True)).returncode
                except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        elif result.get('helper_started') or result.get('probe_started'):result.setdefault('cleanup_errors',[]).append('Prefix teardown skipped: observer not reaped')
        if server is not None:
            try:server.close(timeout=bound(2,True))
            except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        try:
            if comp is not None:comp.stop()
            result['remaining_servers']=helpers.matching_servers(BUILD/'server/wineserver',PREFIX)
            result['remaining_prefix_processes']=cal.graphics_resources.sample(PREFIX)
            result['private_loopback_after']=cal.isolation.verify_private_loopback();log_quota(output)
        except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        result['elapsed_seconds']=now()-start
        result['cleanup_complete']=(not result.get('cleanup_errors') and result.get('cleanup_kill') in (0,1) and result.get('cleanup_wait')==0
            and result.get('remaining_servers')==[] and result.get('remaining_prefix_processes')=={} and result.get('private_loopback_after')==approved['private_loopback'])
        try:cal.save(output/'results.json',result)
        except Exception as error:
            result['evidence_write_error']=repr(error);print(json.dumps(result),file=sys.stderr)
    return 1

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path)
    selection=parser.add_mutually_exclusive_group()
    selection.add_argument('--module-warn-r2',action='store_true',help='Fixed fresh R2 paths and warn+module/trace+seh logging only')
    selection.add_argument('--userenv-r3',action='store_true',help='Fixed R3 normal-view API probe before browser')
    selection.add_argument('--userenv-r4',action='store_true',help='Fixed R4 normal view with native compositor prefix scope')
    selection.add_argument('--userenv-r6',action='store_true',help='Fixed R6 registration metadata view + pre-browser sanity probe')
    selection.add_argument('--runtime-unblock',action='store_true',help='Fixed runtime-unblock diagnostic identity; tolerate benign surplus runtime files')
    selection.add_argument('--renderer-unblock',action='store_true',help='Fixed renderer-unblock diagnostic identity')
    selection.add_argument('--observation-control',action='store_true',help='Fixed observation-control diagnostic identity')
    selection.add_argument('--observation-control-r2',action='store_true',help='Fixed observation-control R2 diagnostic identity')
    selection.add_argument('--memory-8g',action='store_true',help='RAM-only control at 8GiB reusing the deployed control-channel candidate; no Wine rebuild or trace redeploy')
    selection.add_argument('--navigation-unblock',action='store_true',help='Bounded NetLog boundary diagnostic for the loopback navigation, reusing the deployed control-channel candidate; no Wine rebuild or trace redeploy')
    selection.add_argument('--navigation-phase',action='store_true',help='Combined bounded phase probe (scoped VLOG + fixed integer NetLog 40s + best-effort startup trace 35s) to isolate the unfinished NavigationRequest phase, reusing the deployed control-channel candidate; no Wine rebuild or trace redeploy')
    selection.add_argument('--browser-boundary',action='store_true',help='WO-20261028-CHROMIUM-BROWSER-BOUNDARY normal 8GiB verification: combined bounded diagnostic (scoped VLOG + fixed integer NetLog 40s + startup trace 35s) reusing the deployed candidate now carrying the CreateAppContainerProfile fix; no Wine rebuild or trace redeploy')
    selection.add_argument('--diagnostic',action='store_true',help='WO-20261028-CHROMIUM-TARGETED-DIAGNOSTIC normal 8GiB diagnostic: enriched WINEDEBUG (userenv+security) + scoped VLOG with */sandbox/* and */base/process/* to localize the first failing kNetwork sandbox launch gate; reuses the deployed control-channel candidate; no Wine rebuild or trace redeploy')
    selection.add_argument('--verify-r1',action='store_true',help='WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + scoped VLOG with */sandbox/* and */base/process/* against the ISOLATED coherent candidate at the AppContainer fix base; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--verify-r2',action='store_true',help='WO-20261029-CHROMIUM-VERIFICATION-ITERATION-2 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the ISOLATED coherent candidate at Wine 871f618; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--verify-r3',action='store_true',help='WO-20261029-CHROMIUM-VERIFICATION-ITERATION-3 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the ISOLATED coherent candidate at Wine 3c60ba90; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--verify-r4',action='store_true',help='WO-20261029-CHROMIUM-VERIFICATION-ITERATION-4 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the ISOLATED coherent candidate at Wine 4afbb4cd; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--verify-r5',action='store_true',help='WO-20261029-CHROMIUM-VERIFICATION-ITERATION-5 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the ISOLATED coherent candidate at Wine f01bba45; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--verify-r6',action='store_true',help='WO-20261030-CHROMIUM-COMBINED-BUILD normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the worker reconciled-build candidate at wine ce87bac0 (full self-contained deploy); verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--token-fixed-r7',action='store_true',help='WO-20261031-CHROMIUM-TOKEN-FIXED-R7 normal 8GiB verification: enriched WINEDEBUG (userenv+security) + level 3 scoped VLOG against the preserved recovered R6 runtime carrying the accepted server/token.c fix (wine 59f4bc31, server 196f24db...); dedicated prefix clone, never the shared edge-install prefix; verifies kNetwork stays sandboxed (--service-sandbox-type=network) and the fixture loads HTTP 200')
    selection.add_argument('--token-fixed-r8',action='store_true',help='Fresh token correction verification with matching compiled level-3 diagnostic contract and candidate WINE/WINESERVER environment')
    selection.add_argument('--token-wait-r9',action='store_true',help='Fixed token correction diagnostic with one bounded private-prefix thread wait snapshot')
    selection.add_argument('--token-wait-r10',action='store_true',help='Fixed token correction diagnostic with one read-only private-prefix Linux thread wait snapshot')
    selection.add_argument('--token-pipe-r11',action='store_true',help='Fixed token correction diagnostic with WINEDEBUG +file,+sync named-pipe transport channels added to the r8 enriched mask; no wineserver -d, no WineDbg/proc observer')
    selection.add_argument('--token-pipe-r12',action='store_true',help='Fresh fixed pipe-only instrumentation diagnostic; same Chromium command, private runtime and sandbox checks')
    selection.add_argument('--token-wait-r13',action='store_true',help='Fixed original token-corrected runtime with one root-scoped read-only thread snapshot')
    selection.add_argument('--token-wait-r14',action='store_true',help='Bound browser Wine/Unix mapping with a root-scoped live-thread wait snapshot')
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--runtime-probe',action='store_true')
    mode.add_argument('--stage-runtime',type=Path)
    args=parser.parse_args()
    if args.token_wait_r14:
        configure('token-wait-r14')
        if args.runtime_probe or args.stage_runtime:
            print(json.dumps(runtime_state(zip_path=args.stage_runtime,extract=args.stage_runtime is not None),indent=2));return 0
        return run(OUTPUT if args.output_dir is None else args.output_dir,load('check-g3-correctness'))
    configure('token-pipe-r12' if args.token_pipe_r12 else 'token-pipe-r11' if args.token_pipe_r11 else 'token-wait-r10' if args.token_wait_r10 else 'token-wait-r9' if args.token_wait_r9 else 'token-fixed-r8' if args.token_fixed_r8 else 'token-fixed-r7' if args.token_fixed_r7 else 'verify-r6' if args.verify_r6 else 'verify-r5' if args.verify_r5 else 'verify-r4' if args.verify_r4 else 'verify-r3' if args.verify_r3 else 'verify-r2' if args.verify_r2 else 'verify-r1' if args.verify_r1 else 'diagnostic' if args.diagnostic else 'browser-boundary' if args.browser_boundary else 'navigation-phase' if args.navigation_phase else 'navigation-unblock' if args.navigation_unblock else 'memory-8g' if args.memory_8g else 'observation-control-r2' if args.observation_control_r2 else 'observation-control' if args.observation_control else 'renderer-unblock' if args.renderer_unblock else 'runtime-unblock' if args.runtime_unblock else 'userenv-r6' if args.userenv_r6 else 'userenv-r4' if args.userenv_r4 else 'userenv-r3' if args.userenv_r3 else 'module-warn-r2' if args.module_warn_r2 else 'r1')
    if args.runtime_probe or args.stage_runtime:
        if args.token_wait_r13:configure('token-wait-r13')
        print(json.dumps(runtime_state(zip_path=args.stage_runtime,extract=args.stage_runtime is not None),indent=2));return 0
    if args.token_wait_r13:configure('token-wait-r13')
    return run(OUTPUT if args.output_dir is None else args.output_dir,load('check-g3-correctness'))

if __name__=='__main__':raise SystemExit(main())
