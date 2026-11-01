#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an owned graphics check with private mount/net AND filesystem X sockets.

Example (privileged Linux lab):
  python3 run-isolated-graphics.py --socket-root /var/tmp/owned-lab/sockets -- \
    python3 check-g3-correctness.py --wine-build ... --output-dir ... --g4-correctness

The nested runner must bound/clean its own Wine/Xvfb processes. No global socket
directory is modified. A nonempty directory is retained, never recursively deleted.
"""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile


def net_identity(fd=None):
    path='/proc/self/ns/net' if fd is None else '/proc/self/fd/'+str(fd)
    link=os.readlink(path)
    if not link.startswith('net:['): raise RuntimeError('Not a network namespace descriptor')
    state=os.stat(path) if fd is None else os.fstat(fd)
    return {'device':state.st_dev,'inode':state.st_ino,'link':link}


def network_state():
    def ip(args):
        return json.loads(subprocess.run(['ip','-j']+args,check=True,timeout=1,capture_output=True,text=True).stdout)
    return {'namespace':net_identity(),'addresses':ip(['address','show']),
            'routes4':ip(['-4','route','show','table','all']),
            'routes6':ip(['-6','route','show','table','all'])}


def require_loopback_only(state,up):
    addresses=state['addresses']
    if (not isinstance(addresses,list) or len(addresses)!=1 or addresses[0].get('ifname')!='lo'
            or addresses[0].get('link_type')!='loopback'):
        raise RuntimeError('Private network must contain ONLY lo')
    lo=addresses[0]; info=lo.get('addr_info',[])
    if up:
        if 'UP' not in lo.get('flags',[]) or not any(a.get('local')=='127.0.0.1' and a.get('prefixlen')==8 for a in info):
            raise RuntimeError('Private lo not UP with 127.0.0.1/8')
        if any((a.get('local'),a.get('prefixlen')) not in (('127.0.0.1',8),('::1',128)) for a in info):
            raise RuntimeError('Unexpected loopback address rejected')
    elif 'UP' in lo.get('flags',[]) or info:
        raise RuntimeError('Expected fresh lo DOWN without addresses')
    for route in state['routes4']+state['routes6']:
        if (not up or route.get('dev')!='lo' or 'gateway' in route
                or not ipaddress.ip_network(route.get('dst','default'),strict=False).is_loopback):
            raise RuntimeError('Non-loopback routing rejected')


def require_private_net(parent_fd):
    parent,current=net_identity(parent_fd),net_identity()
    if parent==current: raise RuntimeError('Refusing loopback in original network namespace')
    return parent


def enable_private_loopback(parent_fd):
    parent=require_private_net(parent_fd)  # BEFORE any modifying command
    before=network_state();require_loopback_only(before,False)
    subprocess.run(['ip','link','set','dev','lo','up'],check=True,timeout=1)
    after=network_state();require_loopback_only(after,True)
    return {'parent_namespace':parent,'before':before,'after':after}


def verify_private_loopback():
    value=os.environ.get('WEX_G4_LOOPBACK_PARENT_FD','')
    if not value.isdigit() or int(value)<3: raise RuntimeError('Missing inherited original-netns descriptor')
    parent=require_private_net(int(value))
    state=network_state();require_loopback_only(state,True)
    return {'parent_namespace':parent,'observed':state}


def main():
    if len(sys.argv)>=6 and sys.argv[1]=='--child-loopback' and sys.argv[4]=='--':
        fd=int(sys.argv[3])
        setup=enable_private_loopback(fd)
        os.set_inheritable(fd,True)  # proof survives runuser; browser children close fds normally
        os.environ['WEX_G4_LOOPBACK_PARENT_FD']=str(fd)
        print('PRIVATE_LOOPBACK_SETUP '+json.dumps(setup),file=sys.stderr,flush=True)
        subprocess.run(['mount','--bind',sys.argv[2],'/tmp/.X11-unix'],check=True,timeout=2)
        os.execvp(sys.argv[5],sys.argv[5:])
    if len(sys.argv) >= 5 and sys.argv[1] == '--child' and sys.argv[3] == '--':
        subprocess.run(['mount', '--bind', sys.argv[2], '/tmp/.X11-unix'], check=True)
        os.execvp(sys.argv[4], sys.argv[4:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket-root', type=Path, required=True)
    parser.add_argument('--private-loopback',action='store_true',help='Only after unshare: verify fresh private netns and raise ONLY lo')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.socket_root.is_absolute():
        parser.error('--socket-root must be an absolute owned lab directory')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('A nested test command is required')
    args.socket_root.mkdir(parents=True, exist_ok=True)
    sockets = Path(tempfile.mkdtemp(prefix='xsocket-', dir=args.socket_root.resolve()))
    sockets.chmod(0o1777)
    process = None
    parent_fd=None
    try:
        child=['--child',str(sockets),'--']
        options={}
        if args.private_loopback:
            parent_fd=os.open('/proc/self/ns/net',os.O_RDONLY)
            child=['--child-loopback',str(sockets),str(parent_fd),'--']
            options['pass_fds']=(parent_fd,)
        process = subprocess.Popen(['unshare', '--mount', '--net', '--propagation', 'private',
                                    sys.executable, str(Path(__file__).resolve())]+child+command,
                                    start_new_session=True,**options)
        return process.wait()
    except KeyboardInterrupt:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        raise
    finally:
        if parent_fd is not None: os.close(parent_fd)
        try:
            sockets.rmdir()  # Remove only this empty, generated directory.
        except OSError as error:
            print('Retained owned socket directory: ' + str(sockets) + ': ' + str(error), file=sys.stderr)


if __name__ == '__main__':
    raise SystemExit(main())
