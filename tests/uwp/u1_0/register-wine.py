#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Development registration of the self-owned U1.0 fixture in a Wine prefix.

This is not a package installer: it verifies no signature, resolves no
dependency and handles only the loose files of our own fixture. It lays a
staged package directory out in the profile of the prefix's user
(AppData/Local/WineExplorer/WindowsApps) and publishes the record the Wine
server reads (appx-catalog/PackageFullName.wexappx in the prefix, see
wine/server/uwp_context.h).

  register-wine.py stage CASE ARCH BUILD_DIR STAGE_DIR [--name N --publisher P --version V]
  register-wine.py register PREFIX STAGE_DIR      prints full name, family name, package directory (tabs)
  register-wine.py unregister PREFIX FULL_NAME
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
NS = {'f': 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
CATALOG = 'appx-catalog'
STORE = ('AppData', 'Local', 'WineExplorer', 'WindowsApps')   # in the user's profile
SUFFIX = '.wexappx'


def store(prefix):
    """The per-user package directory of the prefix, as a Unix path and as a path on drive C:."""
    user = os.environ.get('USER') or 'wine'
    if not re.match(r'^[A-Za-z0-9._-]+$', user) or not os.path.isdir(os.path.join(prefix, 'drive_c', 'users', user)):
        fail('no profile for %r in the prefix' % user)
    return (os.path.join(prefix, 'drive_c', 'users', user, *STORE),
            '\\'.join(('C:', 'users', user) + STORE))
SERVER = ('<Extensions><Extension Category="windows.activatableClass.inProcessServer"><InProcessServer>'
          '<Path>component.dll</Path><ActivatableClass ActivatableClassId="WineExplorer.U1Oracle.Component" '
          'ThreadingModel="both" /></InProcessServer></Extension></Extensions>')
NAME_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$')
ID_RE = re.compile(r'^[A-Za-z0-9.]{1,64}$')
CLASS_RE = re.compile(r'^[A-Za-z0-9._-]{1,255}$')
PATH_RE = re.compile(r'^[A-Za-z0-9._ -]+(\\[A-Za-z0-9._ -]+)*$')


def fail(message):
    sys.exit('register-wine.py: ' + message)


def publisher_id(publisher):
    """First 64 bits of SHA-256 over the UTF-16LE publisher, as 13 base32 characters."""
    digest = hashlib.sha256(publisher.encode('utf-16-le')).digest()[:8]
    bits = ''.join('{:08b}'.format(b) for b in digest) + '0'
    alphabet = '0123456789abcdefghjkmnpqrstvwxyz'
    return ''.join(alphabet[int(bits[i:i + 5], 2)] for i in range(0, 65, 5))


def relative_path(path):
    if not PATH_RE.match(path) or any(part in ('.', '..') or part.endswith(('.', ' ')) for part in path.split('\\')):
        fail('unsupported path in the manifest: %r' % path)
    return path


def read_manifest(stage):
    root = ET.parse(os.path.join(stage, 'AppxManifest.xml')).getroot()
    identity = root.find('f:Identity', NS)
    name, version = identity.get('Name'), identity.get('Version')
    arch, resource = identity.get('ProcessorArchitecture', 'neutral'), identity.get('ResourceId', '')
    if not NAME_RE.match(name) or not re.match(r'^\d+\.\d+\.\d+\.\d+$', version) or arch not in ('x64', 'x86'):
        fail('unsupported package identity')
    if resource:
        fail('resource packages are not supported')
    pubid = publisher_id(identity.get('Publisher'))
    apps = []
    for app in root.findall('f:Applications/f:Application', NS):
        if not ID_RE.match(app.get('Id')):
            fail('unsupported application id')
        apps.append((app.get('Id'), relative_path(app.get('Executable'))))
    classes = []
    for ext in root.findall('f:Extensions/f:Extension', NS):
        if ext.get('Category') != 'windows.activatableClass.inProcessServer':
            continue
        server = ext.find('f:InProcessServer', NS)
        path = relative_path(server.find('f:Path', NS).text.strip())
        for cls in server.findall('f:ActivatableClass', NS):
            if not CLASS_RE.match(cls.get('ActivatableClassId')):
                fail('unsupported class id')
            classes.append((cls.get('ActivatableClassId'), path))
    if not apps:
        fail('the manifest declares no application')
    full = '%s_%s_%s_%s_%s' % (name, version, arch, resource, pubid)
    family = '%s_%s' % (name, pubid)
    return full, family, apps, classes


def cmd_stage(args):
    if args.case not in ('positive', 'missing_class', 'missing_dll'):
        fail('unknown case')
    os.mkdir(args.stage)
    shutil.copyfile(os.path.join(args.build, 'probe.exe'), os.path.join(args.stage, 'probe.exe'))
    if args.case != 'missing_dll':
        shutil.copyfile(os.path.join(args.build, 'component.dll'), os.path.join(args.stage, 'component.dll'))
    with open(os.path.join(HERE, 'AppxManifest.xml'), encoding='utf-8') as f:
        manifest = f.read()
    values = {'@NAME@': args.name, '@PUBLISHER@': args.publisher, '@VERSION@': args.version, '@ARCH@': args.arch,
              '@SERVER@': '' if args.case == 'missing_class' else SERVER}
    for key, value in values.items():
        manifest = manifest.replace(key, value)
    with open(os.path.join(args.stage, 'AppxManifest.xml'), 'w', encoding='utf-8', newline='') as f:
        f.write(manifest)


def cmd_register(args):
    full, family, apps, classes = read_manifest(args.stage)
    catalog, dos_catalog = store(args.prefix)
    target = os.path.join(catalog, full)
    record = os.path.join(args.prefix, CATALOG, full + SUFFIX)
    os.makedirs(catalog, exist_ok=True)
    os.makedirs(os.path.join(args.prefix, CATALOG), exist_ok=True)
    if os.path.lexists(target) or os.path.lexists(record):
        fail('%s is already registered' % full)
    for _, exe in apps:
        if not os.path.isfile(os.path.join(args.stage, *exe.split('\\'))):
            fail('the executable %s is not in the package' % exe)
    # Stage next to the destination, then publish the directory and, last, the record.
    staging = os.path.join(catalog, '.staging-%d' % os.getpid())
    os.mkdir(staging)
    try:
        for base, dirs, files in os.walk(args.stage):
            rel = os.path.relpath(base, args.stage)
            for d in dirs:
                if os.path.islink(os.path.join(base, d)):
                    fail('symbolic link in the package')
                os.mkdir(os.path.join(staging, rel, d))
            for name in files:
                src = os.path.join(base, name)
                if os.path.islink(src) or not os.path.isfile(src):
                    fail('unsupported file in the package')
                shutil.copyfile(src, os.path.join(staging, rel, name))
        os.rename(staging, target)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    root = dos_catalog + '\\' + full
    text = 'WEXAPPX 1\n'
    text += 'root\t%s\n' % root
    text += ''.join('app\t%s\t%s\n' % app for app in apps)
    text += ''.join('class\t%s\t%s\n' % cls for cls in classes)
    temp = record + '.tmp-%d' % os.getpid()
    with open(temp, 'xb') as f:
        f.write(text.encode('utf-16-le'))
    os.rename(temp, record)
    print(full, family, root, sep='\t')


def cmd_unregister(args):
    if not re.match(r'^[A-Za-z0-9.-]+_[0-9.]+_[a-z0-9]+__[a-z0-9]{13}$', args.full_name):
        fail('not a package full name')
    target = os.path.join(store(args.prefix)[0], args.full_name)
    record = os.path.join(args.prefix, CATALOG, args.full_name + SUFFIX)
    if not os.path.isfile(record):
        fail('%s is not registered' % args.full_name)
    os.unlink(record)   # the record goes first: the package stops being visible
    shutil.rmtree(target)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
sub = parser.add_subparsers(dest='command', required=True)
p = sub.add_parser('stage')
p.add_argument('case')
p.add_argument('arch', choices=['x64', 'x86'])
p.add_argument('build')
p.add_argument('stage')
p.add_argument('--name', default='WineExplorer.U1Oracle.Probe')
p.add_argument('--publisher', default='CN=WineExplorer U1 Oracle')
p.add_argument('--version', default='1.0.0.0')
p.set_defaults(func=cmd_stage)
p = sub.add_parser('register')
p.add_argument('prefix')
p.add_argument('stage')
p.set_defaults(func=cmd_register)
p = sub.add_parser('unregister')
p.add_argument('prefix')
p.add_argument('full_name')
p.set_defaults(func=cmd_unregister)
args = parser.parse_args()
args.func(args)
