#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""A stand-in for AccountsService, logind and systemd on a private D-Bus, for the tests of wex-accountsd
(scripts/test_accountsd.py). Usage: accountsd-fake-bus.py BUS-ADDRESS LOGFILE. Prints READY when the names are owned.

The state: alice (1001, administrator), bob (1002, administrator, locked), carol (1003, standard), dave (1004, standard,
logged in), svc (999, a system account), builtin (1005, administrator: the account the helper's configuration names as
the built-in administrator). Every call that changes something is appended to LOGFILE as one JSON line, so that the test
can see what the helper asked for and, above all, that a password hash is only ever sent to SetPassword."""
import json
import os
import sys
import time

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

address, logfile = sys.argv[1], sys.argv[2]
DBusGMainLoop(set_as_default=True)
bus = dbus.bus.BusConnection(address)

users = {}   # name -> dict
next_uid = [2000]


def log(**kw):
    with open(logfile, 'a') as f:
        f.write(json.dumps(kw) + '\n')


def add(name, uid, full='', admin=False, locked=False, system=False):
    users[name] = dict(name=name, uid=uid, full=full, type=1 if admin else 0, locked=locked, system=system)


add('alice', 1001, 'Alice A', admin=True)
add('bob', 1002, 'Bob B', admin=True, locked=True)
add('carol', 1003, 'Carol C')
add('dave', 1004, 'Dave D')
add('svc', 999, 'Service', system=True)
add('builtin', 1005, 'Built-in Administrator', admin=True)
LOGGED_IN = {1004}
# FAKE_MANY=N: N more users with long full names, for the paging of the list (F1)
for i in range(int(os.environ.get('FAKE_MANY', '0'))):
    add('many%04d' % i, 3000 + i, 'M' * 250)


def injected(method):
    """LOGFILE.fail lists the methods that fail now (F2); LOGFILE.slow makes SetRealName take 12 s (F6)"""
    try:
        with open(logfile + '.fail') as f:
            if method in f.read().split():
                raise dbus.exceptions.DBusException('injected failure', name='org.freedesktop.Accounts.Error.Failed')
    except OSError:
        pass


def path_of(name):
    return '/org/freedesktop/Accounts/User%d' % users[name]['uid']


class User(dbus.service.Object):
    def __init__(self, name):
        self.name = name
        dbus.service.Object.__init__(self, bus, path_of(name))

    def u(self):
        return users[self.name]

    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, iface, prop):
        u = self.u()
        if prop == 'Uid': return dbus.UInt64(u['uid'])
        if prop == 'UserName': return dbus.String(u['name'])
        if prop == 'RealName': return dbus.String(u['full'])
        if prop == 'AccountType': return dbus.Int32(u['type'])
        if prop == 'Locked': return dbus.Boolean(u['locked'])
        if prop == 'SystemAccount': return dbus.Boolean(u['system'])
        raise dbus.exceptions.DBusException('no such property', name='org.freedesktop.DBus.Error.InvalidArgs')

    @dbus.service.method('org.freedesktop.Accounts.User', in_signature='s')
    def SetRealName(self, name):
        log(op='SetRealName', user=self.name, value=str(name))
        if os.path.exists(logfile + '.slow'):
            time.sleep(12)
        self.u()['full'] = str(name)

    @dbus.service.method('org.freedesktop.Accounts.User', in_signature='i')
    def SetAccountType(self, t):
        injected('SetAccountType')
        log(op='SetAccountType', user=self.name, value=int(t))
        self.u()['type'] = int(t)

    @dbus.service.method('org.freedesktop.Accounts.User', in_signature='b')
    def SetLocked(self, v):
        log(op='SetLocked', user=self.name, value=bool(v))
        self.u()['locked'] = bool(v)

    @dbus.service.method('org.freedesktop.Accounts.User', in_signature='s')
    def SetUserName(self, name):
        log(op='SetUserName', user=self.name, value=str(name))
        u = users.pop(self.name)
        u['name'] = str(name)
        users[str(name)] = u
        self.name = str(name)

    @dbus.service.method('org.freedesktop.Accounts.User', in_signature='ss')
    def SetPassword(self, password, hint):
        injected('SetPassword')
        log(op='SetPassword', user=self.name, value=str(password))


objects = {}


def make(name):
    objects[name] = User(name)


for n in list(users):
    make(n)


class Accounts(dbus.service.Object):
    def __init__(self):
        dbus.service.Object.__init__(self, bus, '/org/freedesktop/Accounts')

    @dbus.service.method('org.freedesktop.Accounts', in_signature='', out_signature='ao')
    def ListCachedUsers(self):
        return [dbus.ObjectPath(path_of(n)) for n in users]

    @dbus.service.method('org.freedesktop.Accounts', in_signature='s', out_signature='o')
    def FindUserByName(self, name):
        if str(name) not in users:
            raise dbus.exceptions.DBusException('no such user', name='org.freedesktop.Accounts.Error.Failed')
        return dbus.ObjectPath(path_of(str(name)))

    @dbus.service.method('org.freedesktop.Accounts', in_signature='ssi', out_signature='o')
    def CreateUser(self, name, full, t):
        name = str(name)
        log(op='CreateUser', user=name, full=str(full), type=int(t))
        if name in users:
            raise dbus.exceptions.DBusException('exists', name='org.freedesktop.Accounts.Error.UserExists')
        add(name, next_uid[0], str(full), admin=(int(t) == 1))
        next_uid[0] += 1
        make(name)
        return dbus.ObjectPath(path_of(name))

    @dbus.service.method('org.freedesktop.Accounts', in_signature='xb')
    def DeleteUser(self, uid, remove):
        log(op='DeleteUser', uid=int(uid), remove=bool(remove))
        for n, u in list(users.items()):
            if u['uid'] == int(uid):
                objects.pop(n).remove_from_connection()
                del users[n]
                return
        raise dbus.exceptions.DBusException('none', name='org.freedesktop.Accounts.Error.Failed')


class Login(dbus.service.Object):
    def __init__(self):
        dbus.service.Object.__init__(self, bus, '/org/freedesktop/login1')

    @dbus.service.method('org.freedesktop.login1.Manager', in_signature='u', out_signature='o')
    def GetUser(self, uid):
        if int(uid) in LOGGED_IN:
            return dbus.ObjectPath('/org/freedesktop/login1/user/_%d' % int(uid))
        raise dbus.exceptions.DBusException('no user', name='org.freedesktop.login1.NoSuchUser')


class Systemd(dbus.service.Object):
    def __init__(self):
        dbus.service.Object.__init__(self, bus, '/org/freedesktop/systemd1')

    @dbus.service.method('org.freedesktop.systemd1.Manager', in_signature='s', out_signature='o')
    def GetUnit(self, name):
        return dbus.ObjectPath('/org/freedesktop/systemd1/unit/fake')

    @dbus.service.method('org.freedesktop.systemd1.Manager', in_signature='ss', out_signature='o')
    def StartUnit(self, name, mode):
        log(op='StartUnit', unit=str(name), mode=str(mode))
        return dbus.ObjectPath('/org/freedesktop/systemd1/job/1')


names = [dbus.service.BusName(n, bus) for n in
         ('org.freedesktop.Accounts', 'org.freedesktop.login1', 'org.freedesktop.systemd1')]
Accounts()
Login()
Systemd()
print('READY', flush=True)
GLib.MainLoop().run()
