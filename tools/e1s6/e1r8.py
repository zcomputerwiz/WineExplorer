#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""WO-19: E1R8 Python codec (R8 step 2, part 2).

Standard-library-only (struct) byte codec for the E1-S18e R8 attempt-tuple
messages, the S7 version 2 PROMPT and the secret password answer, per the
frozen contract docs/E1_S18E_R8_SPEC.md (v5.1) sections 1, 2, 4, 5, 6, 7 and
11. It mirrors the reviewed C codec src/programs/wexelevate/e1r8_codec.h
(WO-18) exactly: the same header, tuple, enum, type and string rules, the
same offsets and the same rejections.

Little-endian, no padding, exact length. Every decoder takes (data, expected)
and the expected sealed tuple, returns a dataclass on success, and raises
E1R8Error (with the reason in the message) on any rejection. UTF-8 is checked
by an explicit byte-level validator implementing section 4, not by
bytes.decode alone.
"""
import struct
from dataclasses import dataclass

#
# Sizes (bytes), per spec section 4.
#
HEADER_SIZE = 76
TAIL_SIZE = 160
PRE_ENDORSEMENT_SIZE = 204
SEALED_SIZE = 220
S1_RECORD_SIZE = 156
S7_PROMPT_SIZE = 178

#
# Message type constants, per spec sections 5 and 6.
#
TYPE_INIT = 1
TYPE_ACK = 2
TYPE_SELECT_OK = 3
TYPE_SELECT = 4
TYPE_READY = 5
TYPE_RESULT = 6
TYPE_ANSWER = 7
TYPE_DONE = 8
TYPE_ATTACH_OK = 9
TYPE_ENDORSED = 16
TYPE_START = 17
TYPE_WORKER_READY = 18
TYPE_DELEGATED = 19
TYPE_RESULT_V = 20
TYPE_CANCEL = 21
TYPE_W_ACK = 22
TYPE_ATTACH_ACK = 23
TYPE_PROMPT_RELEASE = 24
TYPE_RELEASE = 25
TYPE_RELEASED = 26
TYPE_W_SETUP = 27
# R9 (docs/E1_S18E_R9_SPEC.md): the grant held by V, and the A<->B0 channel.
TYPE_GRANT = 32
TYPE_GRANTED = 33
TYPE_REQUEST = 40
TYPE_DECISION = 41
TYPE_ENDORSED_LOCAL = 42
TYPE_TO_D = 43
TYPE_FROM_D = 44
TYPE_RECHECK = 45
TYPE_RECHECKED = 46
TYPE_LAUNCH = 47
TYPE_LAUNCHED = 48
TYPE_CANCEL_R9 = 49
TYPE_END = 50
TYPE_NEXT_ATTEMPT = 51

#
# Enum values, per spec sections 5 and 6.
#
DESKTOP_MODE_SECURE = 1
DESKTOP_MODE_INSECURE = 2
RESULT_AUTHENTICATED = 1
RESULT_REFUSED = 2
DONE_COMPLETED = 1
DONE_CANCELLED = 2
CANCEL_REASON = 1
RELEASE_COMPLETED = 1
RELEASE_CANCELLED = 2
# R9 enum values, per docs/E1_S18E_R9_SPEC.md section 4.
REQUEST_KIND_RUNAS = 1
REQUEST_KIND_MANIFEST = 2
DECISION_PROMPT = 1
DECISION_DENY = 2
DECISION_REASON_POLICY = 1
DECISION_REASON_ATTEMPTS = 2
DECISION_REASON_BUSY = 3
ENDORSED_LOCAL_ENDORSED = 1
ENDORSED_LOCAL_REFUSED = 2
RECHECK_STEP_ATTACH = 1
RECHECK_STEP_READY = 2
RECHECK_STEP_RESULT = 3
RECHECK_STEP_CONFIRM = 4
RECHECKED_OK = 1
RECHECKED_REFUSED = 2
LAUNCHED_COMMITTED = 1
LAUNCHED_FAILED_BEFORE = 2
LAUNCHED_FAILED_AFTER = 3
CANCEL_R9_USER = 1
CANCEL_R9_D_GONE = 2
CANCEL_R9_DEADLINE = 3
CANCEL_R9_PEER_GONE = 4
CANCEL_R9_PROTOCOL = 5
CANCEL_R9_RECHECK = 6
END_COMPLETED = 1
END_CANCELLED = 2
END_DENIED = 3

#
# S1 answer kinds (kind-2 record), per docs/E1_S1_CONTRACTS.md record 2.
#
S1_KIND = 2
S1_VERSION = 1
S1_ANSWER_TEXT = 1
S1_ANSWER_CONFIRM = 2
S1_ANSWER_CANCEL = 3


class E1R8Error(Exception):
    """Any codec rejection; the reason is in the message."""


@dataclass
class E1R8Tuple:
    """Sealed tuple fields (spec section 2)."""
    request_id: int = 0
    attempt: int = 0
    nonce: bytes = b""
    channel_id: bytes = b""
    binding_digest: bytes = b""
    request_digest: bytes = b""
    deadline: int = 0
    root_gen: int = 0
    server_gen: int = 0
    broker_gen: int = 0
    verifier_gen: int = 0
    map_gen: int = 0
    policy_gen: int = 0
    caller_session: int = 0
    desktop_route: bytes = b""
    desktop_mode: int = 0
    dialog_id: bytes = b""


@dataclass
class Init:
    tuple: E1R8Tuple
    verifier_owner: bytes
    requester: bytes
    image: bytes
    publisher: bytes


@dataclass
class Ack:
    binding_digest: bytes
    request_digest: bytes
    dialog_id: bytes


@dataclass
class Select:
    account: bytes


@dataclass
class Result:
    status: int
    account: bytes
    challenge: bytes


@dataclass
class Answer:
    challenge: bytes
    kind: int
    version: int
    length: int
    required_caps: int
    request_id: int
    attempt: int
    round: int
    message_index: int
    answer_kind: int
    nonce: bytes
    binding_digest: bytes
    request_digest: bytes
    channel_id: bytes
    payload_len: int
    payload: bytes


@dataclass
class Done:
    status: int


@dataclass
class Endorsed:
    tuple: E1R8Tuple
    pid: int
    uid: int


@dataclass
class Start:
    account: bytes
    account_key: bytes


@dataclass
class ResultV:
    status: int
    account_key: bytes
    account: bytes
    evidence_ref: bytes


@dataclass
class Cancel:
    reason: int


@dataclass
class Release:
    outcome: int


@dataclass
class WSetup:
    tuple: E1R8Tuple
    pid: int
    uid: int


#
# R9 message body dataclasses (docs/E1_S18E_R9_SPEC.md section 4).
#

@dataclass
class Request:
    kind: int
    deadline: int
    binding_digest: bytes
    request_digest: bytes
    root_gen: int
    server_gen: int
    broker_gen: int
    verifier_gen: int
    map_gen: int
    policy_gen: int
    desktop_route: bytes
    secure_desktop: int
    requester: bytes
    image: bytes
    publisher: bytes


@dataclass
class Decision:
    status: int
    tuple: E1R8Tuple
    reason: int


@dataclass
class EndorsedLocal:
    dialog_id: bytes
    status: int


@dataclass
class ToD:
    type: int
    frame: bytes
    frame_len: int


@dataclass
class FromD:
    type: int
    frame: bytes
    frame_len: int


@dataclass
class Recheck:
    step: int


@dataclass
class Rechecked:
    step: int
    status: int


@dataclass
class Launch:
    account: bytes
    evidence_ref: bytes


@dataclass
class Launched:
    status: int
    win32: int


@dataclass
class CancelR9:
    reason: int


@dataclass
class End:
    outcome: int


@dataclass
class NextAttempt:
    tuple: E1R8Tuple


@dataclass
class Grant:
    account_key: bytes
    evidence_ref: bytes


#
# String rules (spec section 4).
#

def valid_account_name(s):
    """ASCII, 1..63 bytes, S4 grammar (valid_name, wine/server/accounts.c)."""
    n = len(s)
    if n < 1 or n > 63:
        return False
    if s[0] == ord("-") or (n == 1 and s[0] == ord(".")) or \
            (n == 2 and s[0] == ord(".") and s[1] == ord(".")):
        return False
    for i, c in enumerate(s):
        if c > 127:
            return False
        if not ((ord("a") <= c <= ord("z")) or
                (ord("A") <= c <= ord("Z")) or
                (ord("0") <= c <= ord("9")) or
                c == ord("_") or c == ord("-") or c == ord(".") or
                (c == ord("$") and i == n - 1 and n > 1)):
            return False
    return True


def valid_display_string(s):
    """UTF-16LE, even length, 0..520 bytes, valid surrogates, no NUL/control."""
    n = len(s)
    if n % 2 != 0:
        return False
    if n > 520:
        return False
    i = 0
    while i < n:
        ch = s[i] | (s[i + 1] << 8)
        if 0xd800 <= ch <= 0xdbff:
            if i + 4 > n:
                return False
            lo = s[i + 2] | (s[i + 3] << 8)
            if not (0xdc00 <= lo <= 0xdfff):
                return False
            i += 4
        elif 0xdc00 <= ch <= 0xdfff:
            return False
        else:
            if ch <= 0x1f or (0x7f <= ch <= 0x9f):
                return False
            i += 2
    return True


def valid_verifier_owner(s):
    """ASCII D-Bus unique name ":<digits>.<digits>", 4..255 bytes."""
    n = len(s)
    if n < 4 or n > 255:
        return False
    if s[0] != ord(":"):
        return False
    found_dot = False
    dot = 0
    for i in range(1, n):
        c = s[i]
        if c == ord("."):
            if found_dot:
                return False
            found_dot = True
            dot = i
        elif not (ord("0") <= c <= ord("9")):
            return False
    if not found_dot:
        return False
    if dot == 1:
        return False
    if dot + 1 >= n:
        return False
    return True


def valid_utf8(s):
    """Strict RFC 3629 (spec section 4): no truncated/missing continuation,
    no overlong, no surrogates, nothing above U+10FFFF, no C0, C1, F5..FF,
    no NUL. Length 1..4096 bytes."""
    n = len(s)
    if n < 1 or n > 4096:
        return False
    i = 0
    while i < n:
        b = s[i]
        if b == 0:
            return False
        if b < 0x80:
            i += 1
        elif 0xc2 <= b <= 0xdf:
            if i + 1 >= n:
                return False
            c = s[i + 1]
            if not (0x80 <= c <= 0xbf):
                return False
            i += 2
        elif 0xe0 <= b <= 0xef:
            if i + 2 >= n:
                return False
            c = s[i + 1]
            d = s[i + 2]
            if not (0x80 <= c <= 0xbf):
                return False
            if not (0x80 <= d <= 0xbf):
                return False
            if b == 0xe0 and c < 0xa0:
                return False
            if b == 0xed and c > 0x9f:
                return False
            i += 3
        elif 0xf0 <= b <= 0xf4:
            if i + 3 >= n:
                return False
            c = s[i + 1]
            d = s[i + 2]
            e = s[i + 3]
            if not (0x80 <= c <= 0xbf):
                return False
            if not (0x80 <= d <= 0xbf):
                return False
            if not (0x80 <= e <= 0xbf):
                return False
            if b == 0xf0 and c < 0x90:
                return False
            if b == 0xf4 and c > 0x8f:
                return False
            i += 4
        else:
            return False
    return True


#
# Wire string: a u32 byte length followed by exactly that many bytes, no NUL.
#

def _put_string(s):
    return struct.pack("<I", len(s)) + s


def _get_string(data, off):
    n = len(data)
    if off > n or n - off < 4:
        raise E1R8Error("short: string length")
    slen = struct.unpack_from("<I", data, off)[0]
    if slen > n - off - 4:
        raise E1R8Error("short: string data")
    return data[off + 4:off + 4 + slen], off + 4 + slen


#
# Common header (76 bytes), spec section 4 offsets.
#

def _put_header(type, total_len, t):
    return struct.pack("<4sHHIQII32s16s", b"E1R8", 1, type, total_len,
                       t.request_id, t.attempt, 0, t.nonce, t.channel_id)


def _get_header(data, type, expected):
    if len(data) < HEADER_SIZE:
        raise E1R8Error("short: header")
    (magic, version, mtype, length, request_id, attempt, reserved,
     nonce, channel_id) = struct.unpack_from("<4sHHIQII32s16s", data, 0)
    if magic != b"E1R8":
        raise E1R8Error("magic")
    if version != 1:
        raise E1R8Error("version")
    if mtype != type:
        raise E1R8Error("type")
    if length != len(data):
        raise E1R8Error("length")
    if request_id != expected.request_id:
        raise E1R8Error("request_id")
    if attempt != expected.attempt:
        raise E1R8Error("attempt")
    if reserved != 0:
        raise E1R8Error("reserved")
    if nonce != expected.nonce:
        raise E1R8Error("nonce")
    if channel_id != expected.channel_id:
        raise E1R8Error("channel_id")


#
# Tuple tail (160 bytes), spec section 4 offsets.
#

_TAIL_FMT = "<32s32sQQQQQQQI16sI16s"


def encode_tuple_tail(t):
    if len(t.binding_digest) != 32 or len(t.request_digest) != 32 or \
            len(t.desktop_route) != 16 or len(t.dialog_id) != 16:
        raise E1R8Error("tuple field size")
    return struct.pack(_TAIL_FMT, t.binding_digest, t.request_digest,
                       t.deadline, t.root_gen, t.server_gen, t.broker_gen,
                       t.verifier_gen, t.map_gen, t.policy_gen,
                       t.caller_session, t.desktop_route, t.desktop_mode,
                       t.dialog_id)


def decode_tuple_tail(data):
    if len(data) != TAIL_SIZE:
        raise E1R8Error("tail length")
    (binding_digest, request_digest, deadline, root_gen, server_gen,
     broker_gen, verifier_gen, map_gen, policy_gen, caller_session,
     desktop_route, desktop_mode, dialog_id) = struct.unpack(_TAIL_FMT, data)
    if desktop_mode not in (DESKTOP_MODE_SECURE, DESKTOP_MODE_INSECURE):
        raise E1R8Error("desktop_mode enum")
    return E1R8Tuple(binding_digest=binding_digest,
                     request_digest=request_digest, deadline=deadline,
                     root_gen=root_gen, server_gen=server_gen,
                     broker_gen=broker_gen, verifier_gen=verifier_gen,
                     map_gen=map_gen, policy_gen=policy_gen,
                     caller_session=caller_session,
                     desktop_route=desktop_route, desktop_mode=desktop_mode,
                     dialog_id=dialog_id)


#
# Pre-endorsement tuple (204 bytes) and sealed tuple (220 bytes).
#

_PRE_FMT = "<QI32s16s32s32sQQQQQQQI16sI"
_SEALED_FMT = _PRE_FMT + "16s"


def encode_pre_endorsement_tuple(t):
    if len(t.nonce) != 32 or len(t.channel_id) != 16:
        raise E1R8Error("tuple field size")
    return struct.pack(_PRE_FMT, t.request_id, t.attempt, t.nonce,
                       t.channel_id, t.binding_digest, t.request_digest,
                       t.deadline, t.root_gen, t.server_gen, t.broker_gen,
                       t.verifier_gen, t.map_gen, t.policy_gen,
                       t.caller_session, t.desktop_route, t.desktop_mode)


def decode_pre_endorsement_tuple(data):
    if len(data) != PRE_ENDORSEMENT_SIZE:
        raise E1R8Error("pre-endorsement length")
    (request_id, attempt, nonce, channel_id, binding_digest, request_digest,
     deadline, root_gen, server_gen, broker_gen, verifier_gen, map_gen,
     policy_gen, caller_session, desktop_route, desktop_mode) = \
        struct.unpack(_PRE_FMT, data)
    if desktop_mode not in (DESKTOP_MODE_SECURE, DESKTOP_MODE_INSECURE):
        raise E1R8Error("desktop_mode enum")
    return E1R8Tuple(request_id=request_id, attempt=attempt, nonce=nonce,
                     channel_id=channel_id, binding_digest=binding_digest,
                     request_digest=request_digest, deadline=deadline,
                     root_gen=root_gen, server_gen=server_gen,
                     broker_gen=broker_gen, verifier_gen=verifier_gen,
                     map_gen=map_gen, policy_gen=policy_gen,
                     caller_session=caller_session,
                     desktop_route=desktop_route, desktop_mode=desktop_mode)


def encode_sealed_tuple(t):
    if len(t.dialog_id) != 16:
        raise E1R8Error("tuple field size")
    return encode_pre_endorsement_tuple(t) + t.dialog_id


def decode_sealed_tuple(data):
    if len(data) != SEALED_SIZE:
        raise E1R8Error("sealed length")
    t = decode_pre_endorsement_tuple(data[:PRE_ENDORSEMENT_SIZE])
    t.dialog_id = data[PRE_ENDORSEMENT_SIZE:SEALED_SIZE]
    return t


def check_tuple_tail(data, off, expected):
    """Verify a tuple tail at data[off:] against the expected sealed tuple."""
    t = decode_tuple_tail(data[off:off + TAIL_SIZE])
    if t.binding_digest != expected.binding_digest:
        raise E1R8Error("tuple binding_digest")
    if t.request_digest != expected.request_digest:
        raise E1R8Error("tuple request_digest")
    if t.deadline != expected.deadline:
        raise E1R8Error("tuple deadline")
    if t.root_gen != expected.root_gen:
        raise E1R8Error("tuple root_gen")
    if t.server_gen != expected.server_gen:
        raise E1R8Error("tuple server_gen")
    if t.broker_gen != expected.broker_gen:
        raise E1R8Error("tuple broker_gen")
    if t.verifier_gen != expected.verifier_gen:
        raise E1R8Error("tuple verifier_gen")
    if t.map_gen != expected.map_gen:
        raise E1R8Error("tuple map_gen")
    if t.policy_gen != expected.policy_gen:
        raise E1R8Error("tuple policy_gen")
    if t.caller_session != expected.caller_session:
        raise E1R8Error("tuple caller_session")
    if t.desktop_route != expected.desktop_route:
        raise E1R8Error("tuple desktop_route")
    if t.desktop_mode != expected.desktop_mode:
        raise E1R8Error("tuple desktop_mode")
    if t.dialog_id != expected.dialog_id:
        raise E1R8Error("tuple dialog_id")


#
# Type 1: INIT (B->D). Body: tuple tail (160), verifier_owner:str,
# requester:str, image:str, publisher:str. Total 256..2067.
#

def encode_init(t, verifier_owner, requester, image, publisher):
    if not valid_verifier_owner(verifier_owner):
        raise E1R8Error("init verifier_owner")
    if not valid_display_string(requester):
        raise E1R8Error("init requester")
    if not valid_display_string(image):
        raise E1R8Error("init image")
    if not valid_display_string(publisher):
        raise E1R8Error("init publisher")
    total = HEADER_SIZE + TAIL_SIZE + (4 + len(verifier_owner)) + \
        (4 + len(requester)) + (4 + len(image)) + (4 + len(publisher))
    body = encode_tuple_tail(t) + _put_string(verifier_owner) + \
        _put_string(requester) + _put_string(image) + _put_string(publisher)
    return _put_header(TYPE_INIT, total, t) + body


def decode_init(data, expected):
    if not (256 <= len(data) <= 2067):
        raise E1R8Error("init length")
    _get_header(data, TYPE_INIT, expected)
    check_tuple_tail(data, HEADER_SIZE, expected)
    off = HEADER_SIZE + TAIL_SIZE
    verifier_owner, off = _get_string(data, off)
    if not valid_verifier_owner(verifier_owner):
        raise E1R8Error("init verifier_owner")
    requester, off = _get_string(data, off)
    if not valid_display_string(requester):
        raise E1R8Error("init requester")
    image, off = _get_string(data, off)
    if not valid_display_string(image):
        raise E1R8Error("init image")
    publisher, off = _get_string(data, off)
    if not valid_display_string(publisher):
        raise E1R8Error("init publisher")
    if off != len(data):
        raise E1R8Error("init trailing")
    return Init(tuple=expected, verifier_owner=verifier_owner,
                requester=requester, image=image, publisher=publisher)


#
# Type 2: ACK (D->B). Body: binding_digest:32, request_digest:32,
# dialog_id:16. Total 156.
#

def encode_ack(t):
    return _put_header(TYPE_ACK, 156, t) + t.binding_digest + \
        t.request_digest + t.dialog_id


def decode_ack(data, expected):
    if len(data) != 156:
        raise E1R8Error("ack length")
    _get_header(data, TYPE_ACK, expected)
    binding_digest = data[76:108]
    request_digest = data[108:140]
    dialog_id = data[140:156]
    if binding_digest != expected.binding_digest:
        raise E1R8Error("ack binding_digest")
    if request_digest != expected.request_digest:
        raise E1R8Error("ack request_digest")
    if dialog_id != expected.dialog_id:
        raise E1R8Error("ack dialog_id")
    return Ack(binding_digest=binding_digest, request_digest=request_digest,
               dialog_id=dialog_id)


#
# Type 3: SELECT_OK (B->D). Body: none. Total 76.
#

def encode_select_ok(t):
    return _put_header(TYPE_SELECT_OK, HEADER_SIZE, t)


def decode_select_ok(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("select_ok length")
    _get_header(data, TYPE_SELECT_OK, expected)
    return None


#
# Type 4: SELECT (D->B). Body: account:str. Total 81..143.
#

def encode_select(t, account):
    if not valid_account_name(account):
        raise E1R8Error("select account")
    total = HEADER_SIZE + (4 + len(account))
    return _put_header(TYPE_SELECT, total, t) + _put_string(account)


def decode_select(data, expected):
    if not (81 <= len(data) <= 143):
        raise E1R8Error("select length")
    _get_header(data, TYPE_SELECT, expected)
    account, off = _get_string(data, HEADER_SIZE)
    if not valid_account_name(account):
        raise E1R8Error("select account")
    if off != len(data):
        raise E1R8Error("select trailing")
    return Select(account=account)


#
# Type 5: READY (B->D). Body: none. Total 76.
#

def encode_ready(t):
    return _put_header(TYPE_READY, HEADER_SIZE, t)


def decode_ready(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("ready length")
    _get_header(data, TYPE_READY, expected)
    return None


#
# Type 6: RESULT (B->D). Body: status:u32 (1 authenticated, 2 refused),
# account:str (empty if refused), challenge:32 (zero if refused). Total
# 116 refused, 117..179 authenticated.
#

def encode_result(t, status, account, challenge):
    if status not in (RESULT_AUTHENTICATED, RESULT_REFUSED):
        raise E1R8Error("result status enum")
    if status == RESULT_REFUSED:
        if len(account) != 0:
            raise E1R8Error("refused account")
        if any(challenge):
            raise E1R8Error("refused challenge")
        total = HEADER_SIZE + 4 + 4 + 32
    else:
        if not valid_account_name(account):
            raise E1R8Error("result account")
        total = HEADER_SIZE + 4 + (4 + len(account)) + 32
    body = struct.pack("<I", status) + _put_string(account) + challenge
    return _put_header(TYPE_RESULT, total, t) + body


def decode_result(data, expected):
    if not (116 <= len(data) <= 179):
        raise E1R8Error("result length")
    _get_header(data, TYPE_RESULT, expected)
    status = struct.unpack_from("<I", data, 76)[0]
    off = 80
    if status == RESULT_REFUSED:
        account, off = _get_string(data, off)
        if len(account) != 0:
            raise E1R8Error("refused account")
        if off + 32 > len(data):
            raise E1R8Error("short: challenge")
        challenge = data[off:off + 32]
        off += 32
        if any(challenge):
            raise E1R8Error("refused challenge")
    elif status == RESULT_AUTHENTICATED:
        account, off = _get_string(data, off)
        if not valid_account_name(account):
            raise E1R8Error("result account")
        if off + 32 > len(data):
            raise E1R8Error("short: challenge")
        challenge = data[off:off + 32]
        off += 32
    else:
        raise E1R8Error("result status enum")
    if off != len(data):
        raise E1R8Error("result trailing")
    return Result(status=status, account=account, challenge=challenge)


#
# Type 7: ANSWER (D->B). Body: challenge:32, one unchanged 156-byte S1
# kind-2 record. Total 264 for Confirm/Cancel.
#

def encode_answer(t, challenge, answer_kind, round, index, required_caps):
    if answer_kind == S1_ANSWER_CONFIRM:
        if round != 2 or index != 0:
            raise E1R8Error("answer confirm round/index")
    elif answer_kind == S1_ANSWER_CANCEL:
        if round != 0 or index != 0:
            raise E1R8Error("answer cancel round/index")
        if any(challenge):
            raise E1R8Error("answer cancel challenge")
    else:
        raise E1R8Error("answer kind")
    record = struct.pack("<HHIQQIIII", S1_KIND, S1_VERSION, S1_RECORD_SIZE,
                         required_caps, t.request_id, t.attempt, round,
                         index, answer_kind) + t.nonce + t.binding_digest + \
        t.request_digest + t.channel_id + struct.pack("<I", 0)
    return _put_header(TYPE_ANSWER, 264, t) + challenge + record


def decode_answer(data, expected, caps_mask):
    if len(data) != 264:
        raise E1R8Error("answer length")
    _get_header(data, TYPE_ANSWER, expected)
    challenge = data[76:108]
    off = 108
    if off + S1_RECORD_SIZE > len(data):
        raise E1R8Error("short: s1 record")
    (kind, version, length, required_caps, request_id, attempt, round,
     message_index, answer_kind) = struct.unpack_from("<HHIQQIIII", data, off)
    nonce = data[off + 40:off + 72]
    binding_digest = data[off + 72:off + 104]
    request_digest = data[off + 104:off + 136]
    channel_id = data[off + 136:off + 152]
    payload_len = struct.unpack_from("<I", data, off + 152)[0]
    off += S1_RECORD_SIZE

    if kind != S1_KIND:
        raise E1R8Error("s1 kind")
    if version != S1_VERSION:
        raise E1R8Error("s1 version")
    if length != S1_RECORD_SIZE:
        raise E1R8Error("s1 length")
    if required_caps & ~caps_mask:
        raise E1R8Error("s1 caps outside mask")
    if request_id != expected.request_id:
        raise E1R8Error("s1 request_id")
    if attempt != expected.attempt:
        raise E1R8Error("s1 attempt")
    if nonce != expected.nonce:
        raise E1R8Error("s1 nonce")
    if binding_digest != expected.binding_digest:
        raise E1R8Error("s1 binding_digest")
    if request_digest != expected.request_digest:
        raise E1R8Error("s1 request_digest")
    if channel_id != expected.channel_id:
        raise E1R8Error("s1 channel_id")

    if answer_kind == S1_ANSWER_CONFIRM:
        if round != 2:
            raise E1R8Error("s1 confirm round")
        if message_index != 0:
            raise E1R8Error("s1 confirm index")
        if payload_len != 0:
            raise E1R8Error("s1 confirm payload")
    elif answer_kind == S1_ANSWER_CANCEL:
        if round != 0:
            raise E1R8Error("s1 cancel round")
        if message_index != 0:
            raise E1R8Error("s1 cancel index")
        if payload_len != 0:
            raise E1R8Error("s1 cancel payload")
        if any(challenge):
            raise E1R8Error("s1 cancel challenge")
    else:
        raise E1R8Error("s1 answer_kind")
    if off != len(data):
        raise E1R8Error("answer trailing")
    return Answer(challenge=challenge, kind=kind, version=version,
                  length=length, required_caps=required_caps,
                  request_id=request_id, attempt=attempt, round=round,
                  message_index=message_index, answer_kind=answer_kind,
                  nonce=nonce, binding_digest=binding_digest,
                  request_digest=request_digest, channel_id=channel_id,
                  payload_len=payload_len, payload=b"")


#
# Type 8: DONE (B->D). Body: status:u32 (1 completed, 2 cancelled).
# Total 80.
#

def encode_done(t, status):
    if status not in (DONE_COMPLETED, DONE_CANCELLED):
        raise E1R8Error("done status enum")
    return _put_header(TYPE_DONE, 80, t) + struct.pack("<I", status)


def decode_done(data, expected):
    _get_header(data, TYPE_DONE, expected)
    status = struct.unpack_from("<I", data, 76)[0]
    if status not in (DONE_COMPLETED, DONE_CANCELLED):
        raise E1R8Error("done status enum")
    return Done(status=status)


#
# Type 9: ATTACH_OK (B->D). Body: none. Total 76.
#

def encode_attach_ok(t):
    return _put_header(TYPE_ATTACH_OK, HEADER_SIZE, t)


def decode_attach_ok(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("attach_ok length")
    _get_header(data, TYPE_ATTACH_OK, expected)
    return None


#
# Type 16: ENDORSED (S->V control socket). Body: tuple tail, D's pin
# pid:u32, uid:u32; D's pidfd as SCM_RIGHTS (not in the body). Total 244.
#

def encode_endorsed(t, pid, uid):
    return _put_header(TYPE_ENDORSED, 244, t) + encode_tuple_tail(t) + \
        struct.pack("<II", pid, uid)


def decode_endorsed(data, expected):
    if len(data) != 244:
        raise E1R8Error("endorsed length")
    _get_header(data, TYPE_ENDORSED, expected)
    check_tuple_tail(data, 76, expected)
    pid, uid = struct.unpack_from("<II", data, 236)
    return Endorsed(tuple=expected, pid=pid, uid=uid)


#
# Type 17: START (B->V S6 data). Body: account:str, account_key:16 (zero
# for an unknown name: decoy). Total 97..159.
#

def encode_start(t, account, account_key):
    if not valid_account_name(account):
        raise E1R8Error("start account")
    total = HEADER_SIZE + (4 + len(account)) + 16
    return _put_header(TYPE_START, total, t) + _put_string(account) + \
        account_key


def decode_start(data, expected):
    if not (97 <= len(data) <= 159):
        raise E1R8Error("start length")
    _get_header(data, TYPE_START, expected)
    account, off = _get_string(data, 76)
    if not valid_account_name(account):
        raise E1R8Error("start account")
    if off + 16 > len(data):
        raise E1R8Error("short: account_key")
    account_key = data[off:off + 16]
    off += 16
    if off != len(data):
        raise E1R8Error("start trailing")
    return Start(account=account, account_key=account_key)


#
# Type 18: WORKER_READY (V->B). Body: none. Total 76.
#

def encode_worker_ready(t):
    return _put_header(TYPE_WORKER_READY, HEADER_SIZE, t)


def decode_worker_ready(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("worker_ready length")
    _get_header(data, TYPE_WORKER_READY, expected)
    return None


#
# Type 19: DELEGATED (V->B). Body: none. Total 76.
#

def encode_delegated(t):
    return _put_header(TYPE_DELEGATED, HEADER_SIZE, t)


def decode_delegated(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("delegated length")
    _get_header(data, TYPE_DELEGATED, expected)
    return None


#
# Type 20: RESULT (V->B). Body: status:u32 (1 authenticated, 2 refused),
# account_key:16, canonical account:str, evidence_ref:16; refused: all
# zero/empty. Total 116 refused, 117..179 authenticated.
#

def encode_result_v(t, status, account_key, account, evidence_ref):
    if status not in (RESULT_AUTHENTICATED, RESULT_REFUSED):
        raise E1R8Error("result_v status enum")
    if status == RESULT_REFUSED:
        if len(account) != 0:
            raise E1R8Error("refused account")
        if any(account_key) or any(evidence_ref):
            raise E1R8Error("refused key/evidence")
        total = HEADER_SIZE + 4 + 16 + 4 + 16
    else:
        if not valid_account_name(account):
            raise E1R8Error("result_v account")
        total = HEADER_SIZE + 4 + 16 + (4 + len(account)) + 16
    body = struct.pack("<I", status) + account_key + _put_string(account) + \
        evidence_ref
    return _put_header(TYPE_RESULT_V, total, t) + body


def decode_result_v(data, expected):
    if not (116 <= len(data) <= 179):
        raise E1R8Error("result_v length")
    _get_header(data, TYPE_RESULT_V, expected)
    status = struct.unpack_from("<I", data, 76)[0]
    account_key = data[80:96]
    off = 96
    if status == RESULT_REFUSED:
        account, off = _get_string(data, off)
        if len(account) != 0:
            raise E1R8Error("refused account")
        if off + 16 > len(data):
            raise E1R8Error("short: evidence_ref")
        evidence_ref = data[off:off + 16]
        off += 16
        if any(account_key) or any(evidence_ref):
            raise E1R8Error("refused key/evidence")
    elif status == RESULT_AUTHENTICATED:
        account, off = _get_string(data, off)
        if not valid_account_name(account):
            raise E1R8Error("result_v account")
        if off + 16 > len(data):
            raise E1R8Error("short: evidence_ref")
        evidence_ref = data[off:off + 16]
        off += 16
    else:
        raise E1R8Error("result_v status enum")
    if off != len(data):
        raise E1R8Error("result_v trailing")
    return ResultV(status=status, account_key=account_key, account=account,
                   evidence_ref=evidence_ref)


#
# Type 21: CANCEL (B<->V). Body: reason:u32 = 1. Total 80.
#

def encode_cancel(t, reason):
    if reason != CANCEL_REASON:
        raise E1R8Error("cancel reason enum")
    return _put_header(TYPE_CANCEL, 80, t) + struct.pack("<I", reason)


def decode_cancel(data, expected):
    _get_header(data, TYPE_CANCEL, expected)
    reason = struct.unpack_from("<I", data, 76)[0]
    if reason != CANCEL_REASON:
        raise E1R8Error("cancel reason enum")
    return Cancel(reason=reason)


#
# Type 22: W_ACK (W->V supervisor). Body: none. Total 76.
#

def encode_w_ack(t):
    return _put_header(TYPE_W_ACK, HEADER_SIZE, t)


def decode_w_ack(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("w_ack length")
    _get_header(data, TYPE_W_ACK, expected)
    return None


#
# Type 23: ATTACH_ACK (D->W secret socket). Body: none. Total 76.
#

def encode_attach_ack(t):
    return _put_header(TYPE_ATTACH_ACK, HEADER_SIZE, t)


def decode_attach_ack(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("attach_ack length")
    _get_header(data, TYPE_ATTACH_ACK, expected)
    return None


#
# Type 24: PROMPT_RELEASE (V->W supervisor). Body: none. Total 76.
#

def encode_prompt_release(t):
    return _put_header(TYPE_PROMPT_RELEASE, HEADER_SIZE, t)


def decode_prompt_release(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("prompt_release length")
    _get_header(data, TYPE_PROMPT_RELEASE, expected)
    return None


#
# Type 25: RELEASE (B->V S6 data). Body: outcome:u32 (1 completed, 2
# cancelled). Total 80.
#

def encode_release(t, outcome):
    if outcome not in (RELEASE_COMPLETED, RELEASE_CANCELLED):
        raise E1R8Error("release outcome enum")
    return _put_header(TYPE_RELEASE, 80, t) + struct.pack("<I", outcome)


def decode_release(data, expected):
    _get_header(data, TYPE_RELEASE, expected)
    outcome = struct.unpack_from("<I", data, 76)[0]
    if outcome not in (RELEASE_COMPLETED, RELEASE_CANCELLED):
        raise E1R8Error("release outcome enum")
    return Release(outcome=outcome)


#
# Type 26: RELEASED (V->B). Body: none. Total 76.
#

def encode_released(t):
    return _put_header(TYPE_RELEASED, HEADER_SIZE, t)


def decode_released(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("released length")
    _get_header(data, TYPE_RELEASED, expected)
    return None


#
# Type 27: W_SETUP (supervisor->W at spawn). Body: tuple tail; D's
# pid:u32, uid:u32; D's pidfd as SCM_RIGHTS (not in the body). Total 244.
#

def encode_w_setup(t, pid, uid):
    return _put_header(TYPE_W_SETUP, 244, t) + encode_tuple_tail(t) + \
        struct.pack("<II", pid, uid)


def decode_w_setup(data, expected):
    if len(data) != 244:
        raise E1R8Error("w_setup length")
    _get_header(data, TYPE_W_SETUP, expected)
    check_tuple_tail(data, 76, expected)
    pid, uid = struct.unpack_from("<II", data, 236)
    return WSetup(tuple=expected, pid=pid, uid=uid)


#
# R9 helpers (docs/E1_S18E_R9_SPEC.md).
#

def _is_zero16(p):
    """True when a 16-byte field is all zero."""
    return all(b == 0 for b in p)


def check_pre_endorsement(data, off, expected):
    """Verify a pre-endorsement tuple at data[off:] against the expected
    sealed tuple (dialog_id is not part of the pre-endorsement tuple)."""
    t = decode_pre_endorsement_tuple(data[off:off + PRE_ENDORSEMENT_SIZE])
    if t.request_id != expected.request_id:
        raise E1R8Error("pre-endorsement request_id")
    if t.attempt != expected.attempt:
        raise E1R8Error("pre-endorsement attempt")
    if t.nonce != expected.nonce:
        raise E1R8Error("pre-endorsement nonce")
    if t.channel_id != expected.channel_id:
        raise E1R8Error("pre-endorsement channel_id")
    if t.binding_digest != expected.binding_digest:
        raise E1R8Error("pre-endorsement binding_digest")
    if t.request_digest != expected.request_digest:
        raise E1R8Error("pre-endorsement request_digest")
    if t.deadline != expected.deadline:
        raise E1R8Error("pre-endorsement deadline")
    if t.root_gen != expected.root_gen:
        raise E1R8Error("pre-endorsement root_gen")
    if t.server_gen != expected.server_gen:
        raise E1R8Error("pre-endorsement server_gen")
    if t.broker_gen != expected.broker_gen:
        raise E1R8Error("pre-endorsement broker_gen")
    if t.verifier_gen != expected.verifier_gen:
        raise E1R8Error("pre-endorsement verifier_gen")
    if t.map_gen != expected.map_gen:
        raise E1R8Error("pre-endorsement map_gen")
    if t.policy_gen != expected.policy_gen:
        raise E1R8Error("pre-endorsement policy_gen")
    if t.caller_session != expected.caller_session:
        raise E1R8Error("pre-endorsement caller_session")
    if t.desktop_route != expected.desktop_route:
        raise E1R8Error("pre-endorsement desktop_route")
    if t.desktop_mode != expected.desktop_mode:
        raise E1R8Error("pre-endorsement desktop_mode")


def valid_to_d_frame(type, length):
    """The embedded frame of a TO_D (type 43): one of the R8 B->D types,
    within that type's R8 length bounds."""
    if type == TYPE_INIT:
        return 256 <= length <= 2067
    if type in (TYPE_SELECT_OK, TYPE_ATTACH_OK, TYPE_READY):
        return length == HEADER_SIZE
    if type == TYPE_RESULT:
        return 116 <= length <= 179
    if type == TYPE_DONE:
        return length == 80
    if type == 31:  # LOCKED: the lockout notice (WO-39), sent in place of ATTACH_OK; header only
        return length == HEADER_SIZE
    return False


def valid_from_d_frame(type, length):
    """The embedded frame of a FROM_D (type 44): one of the R8 D->B types,
    within that type's R8 length bounds."""
    if type == TYPE_ACK:
        return length == 156
    if type == TYPE_SELECT:
        return 81 <= length <= 143
    if type == TYPE_ANSWER:
        return length == 264
    return False


def check_embedded(frame, frame_len, valid):
    """Check the embedded frame's header (magic, version, type, length,
    reserved) and its type/length bounds. Only the header is checked, not
    the embedded body. Returns the embedded type."""
    if frame_len < HEADER_SIZE:
        raise E1R8Error("short: embedded header")
    if frame[0:4] != b"E1R8":
        raise E1R8Error("embedded magic")
    version, mtype, length = struct.unpack_from("<HHI", frame, 4)
    if version != 1:
        raise E1R8Error("embedded version")
    if length != frame_len:
        raise E1R8Error("embedded length")
    reserved = struct.unpack_from("<I", frame, 24)[0]
    if reserved != 0:
        raise E1R8Error("embedded reserved")
    if not valid(mtype, length):
        raise E1R8Error("embedded type")
    return mtype


#
# Type 32: GRANT (B0->V on B0's channel). Body: account_key:16,
# evidence_ref:16, both nonzero. Total 108.
#

def encode_grant(t, account_key, evidence_ref):
    if _is_zero16(account_key) or _is_zero16(evidence_ref):
        raise E1R8Error("grant zero key/evidence")
    return _put_header(TYPE_GRANT, 108, t) + account_key + evidence_ref


def decode_grant(data, expected):
    if len(data) != 108:
        raise E1R8Error("grant length")
    _get_header(data, TYPE_GRANT, expected)
    account_key = data[76:92]
    evidence_ref = data[92:108]
    if _is_zero16(account_key) or _is_zero16(evidence_ref):
        raise E1R8Error("grant zero key/evidence")
    return Grant(account_key=account_key, evidence_ref=evidence_ref)


#
# Type 33: GRANTED (V->B0). Body: none. Total 76.
#

def encode_granted(t):
    return _put_header(TYPE_GRANTED, HEADER_SIZE, t)


def decode_granted(data, expected):
    if len(data) != HEADER_SIZE:
        raise E1R8Error("granted length")
    _get_header(data, TYPE_GRANTED, expected)
    return None


#
# Type 40: REQUEST (A->B0). Body: kind:u32 (1 runas, 2 manifest),
# deadline:u64, binding_digest:32, request_digest:32, root_gen:u64,
# server_gen:u64, broker_gen:u64, verifier_gen:u64, map_gen:u64,
# policy_gen:u64, desktop_route:16, secure_desktop:u32 (0 or 1),
# requester:str, image:str, publisher:str (display strings). Total
# 232..1792. The header's attempt is 0 and its channel_id is zero; the
# nonce is the request's.
#

def encode_request(t, kind, secure_desktop, requester, image, publisher):
    if kind not in (REQUEST_KIND_RUNAS, REQUEST_KIND_MANIFEST):
        raise E1R8Error("request kind enum")
    if secure_desktop not in (0, 1):
        raise E1R8Error("request secure_desktop enum")
    if not valid_display_string(requester):
        raise E1R8Error("request requester")
    if not valid_display_string(image):
        raise E1R8Error("request image")
    if not valid_display_string(publisher):
        raise E1R8Error("request publisher")
    total = HEADER_SIZE + 4 + 8 + 32 + 32 + 6 * 8 + 16 + 4 + \
        (4 + len(requester)) + (4 + len(image)) + (4 + len(publisher))
    # The header's attempt is 0 and its channel_id is zero; the nonce is the request's.
    h = E1R8Tuple(request_id=t.request_id, attempt=0, nonce=t.nonce,
                  channel_id=bytes(16))
    body = struct.pack("<I", kind) + struct.pack("<Q", t.deadline) + \
        t.binding_digest + t.request_digest + \
        struct.pack("<6Q", t.root_gen, t.server_gen, t.broker_gen,
                    t.verifier_gen, t.map_gen, t.policy_gen) + \
        t.desktop_route + struct.pack("<I", secure_desktop) + \
        _put_string(requester) + _put_string(image) + _put_string(publisher)
    return _put_header(TYPE_REQUEST, total, h) + body


def decode_request(data, expected):
    if not (232 <= len(data) <= 1792):
        raise E1R8Error("request length")
    # The header's attempt is 0 and its channel_id is zero; the nonce is the request's.
    h = E1R8Tuple(request_id=expected.request_id, attempt=0, nonce=expected.nonce,
                  channel_id=bytes(16))
    _get_header(data, TYPE_REQUEST, h)
    off = HEADER_SIZE
    kind = struct.unpack_from("<I", data, off)[0]
    off += 4
    if kind not in (REQUEST_KIND_RUNAS, REQUEST_KIND_MANIFEST):
        raise E1R8Error("request kind enum")
    deadline = struct.unpack_from("<Q", data, off)[0]
    off += 8
    binding_digest = data[off:off + 32]
    off += 32
    request_digest = data[off:off + 32]
    off += 32
    root_gen, server_gen, broker_gen, verifier_gen, map_gen, policy_gen = \
        struct.unpack_from("<6Q", data, off)
    off += 48
    desktop_route = data[off:off + 16]
    off += 16
    secure_desktop = struct.unpack_from("<I", data, off)[0]
    off += 4
    if secure_desktop not in (0, 1):
        raise E1R8Error("request secure_desktop enum")
    requester, off = _get_string(data, off)
    if not valid_display_string(requester):
        raise E1R8Error("request requester")
    image, off = _get_string(data, off)
    if not valid_display_string(image):
        raise E1R8Error("request image")
    publisher, off = _get_string(data, off)
    if not valid_display_string(publisher):
        raise E1R8Error("request publisher")
    if off != len(data):
        raise E1R8Error("request trailing")
    return Request(kind=kind, deadline=deadline,
                   binding_digest=binding_digest,
                   request_digest=request_digest, root_gen=root_gen,
                   server_gen=server_gen, broker_gen=broker_gen,
                   verifier_gen=verifier_gen, map_gen=map_gen,
                   policy_gen=policy_gen, desktop_route=desktop_route,
                   secure_desktop=secure_desktop,
                   requester=requester, image=image, publisher=publisher)


#
# Type 41: DECISION (B0->A). Body: status:u32 (1 prompt, 2 deny), then
# for 1 the pre-endorsement tuple (204), for 2 reason:u32 (1 policy, 2
# attempts, 3 busy). Total 284 prompt, 84 deny. For prompt the header's
# request_id, attempt, nonce and channel_id equal the tuple's; for deny
# the header's attempt is 0 and its nonce and channel_id are zero.
#

def encode_decision(t, status, reason):
    if status == DECISION_PROMPT:
        body = struct.pack("<I", status) + encode_pre_endorsement_tuple(t)
        return _put_header(TYPE_DECISION, 284, t) + body
    elif status == DECISION_DENY:
        if reason not in (DECISION_REASON_POLICY, DECISION_REASON_ATTEMPTS,
                          DECISION_REASON_BUSY):
            raise E1R8Error("decision reason enum")
        h = E1R8Tuple(request_id=t.request_id, attempt=0, nonce=t.nonce,
                      channel_id=bytes(16))
        body = struct.pack("<II", status, reason)
        return _put_header(TYPE_DECISION, 84, h) + body
    raise E1R8Error("decision status enum")


def decode_decision(data, expected):
    if not (84 <= len(data) <= 284):
        raise E1R8Error("decision length")
    status = struct.unpack_from("<I", data, 76)[0]
    if status == DECISION_PROMPT:
        if len(data) != 284:
            raise E1R8Error("decision length")
        _get_header(data, TYPE_DECISION, expected)
        check_pre_endorsement(data, 80, expected)
        return Decision(status=status, tuple=expected, reason=0)
    elif status == DECISION_DENY:
        if len(data) != 84:
            raise E1R8Error("decision length")
        h = E1R8Tuple(request_id=expected.request_id, attempt=0,
                      nonce=expected.nonce, channel_id=bytes(16))
        _get_header(data, TYPE_DECISION, h)
        reason = struct.unpack_from("<I", data, 80)[0]
        if reason not in (DECISION_REASON_POLICY, DECISION_REASON_ATTEMPTS,
                          DECISION_REASON_BUSY):
            raise E1R8Error("decision reason enum")
        return Decision(status=status, tuple=E1R8Tuple(), reason=reason)
    raise E1R8Error("decision status enum")


#
# Type 42: ENDORSED_LOCAL (A->B0). Body: dialog_id:16, status:u32
# (1 endorsed, 2 refused). Total 96.
#

def encode_endorsed_local(t, dialog_id, status):
    if status not in (ENDORSED_LOCAL_ENDORSED, ENDORSED_LOCAL_REFUSED):
        raise E1R8Error("endorsed_local status enum")
    return _put_header(TYPE_ENDORSED_LOCAL, 96, t) + dialog_id + \
        struct.pack("<I", status)


def decode_endorsed_local(data, expected):
    if len(data) != 96:
        raise E1R8Error("endorsed_local length")
    _get_header(data, TYPE_ENDORSED_LOCAL, expected)
    dialog_id = data[76:92]
    status = struct.unpack_from("<I", data, 92)[0]
    if status not in (ENDORSED_LOCAL_ENDORSED, ENDORSED_LOCAL_REFUSED):
        raise E1R8Error("endorsed_local status enum")
    return EndorsedLocal(dialog_id=dialog_id, status=status)


#
# Type 43: TO_D (B0->A). Body: one complete R8 B->D frame (types 1, 3,
# 9, 5, 6, 8). Only the embedded header is checked, not the embedded
# body. Total 152..2143.
#

def encode_to_d(t, frame):
    frame_len = len(frame)
    mtype = check_embedded(frame, frame_len, valid_to_d_frame)
    total = HEADER_SIZE + frame_len
    return _put_header(TYPE_TO_D, total, t) + frame


def decode_to_d(data, expected):
    if not (152 <= len(data) <= 2143):
        raise E1R8Error("to_d length")
    _get_header(data, TYPE_TO_D, expected)
    frame = data[HEADER_SIZE:]
    frame_len = len(frame)
    mtype = check_embedded(frame, frame_len, valid_to_d_frame)
    return ToD(type=mtype, frame=frame, frame_len=frame_len)


#
# Type 44: FROM_D (A->B0). Body: one complete R8 D->B frame (types 2,
# 4, 7) as read from D. Only the embedded header is checked, not the
# embedded body. Total 157..340.
#

def encode_from_d(t, frame):
    frame_len = len(frame)
    mtype = check_embedded(frame, frame_len, valid_from_d_frame)
    total = HEADER_SIZE + frame_len
    return _put_header(TYPE_FROM_D, total, t) + frame


def decode_from_d(data, expected):
    if not (157 <= len(data) <= 340):
        raise E1R8Error("from_d length")
    _get_header(data, TYPE_FROM_D, expected)
    frame = data[HEADER_SIZE:]
    frame_len = len(frame)
    mtype = check_embedded(frame, frame_len, valid_from_d_frame)
    return FromD(type=mtype, frame=frame, frame_len=frame_len)


#
# Type 45: RECHECK (B0->A). Body: step:u32 (1 attach, 2 ready, 3 result,
# 4 confirm). Total 80.
#

def encode_recheck(t, step):
    if not (RECHECK_STEP_ATTACH <= step <= RECHECK_STEP_CONFIRM):
        raise E1R8Error("recheck step enum")
    return _put_header(TYPE_RECHECK, 80, t) + struct.pack("<I", step)


def decode_recheck(data, expected):
    if len(data) != 80:
        raise E1R8Error("recheck length")
    _get_header(data, TYPE_RECHECK, expected)
    step = struct.unpack_from("<I", data, 76)[0]
    if not (RECHECK_STEP_ATTACH <= step <= RECHECK_STEP_CONFIRM):
        raise E1R8Error("recheck step enum")
    return Recheck(step=step)


#
# Type 46: RECHECKED (A->B0). Body: step:u32 (1..4), status:u32
# (1 ok, 2 refused). Total 84.
#

def encode_rechecked(t, step, status):
    if not (RECHECK_STEP_ATTACH <= step <= RECHECK_STEP_CONFIRM):
        raise E1R8Error("rechecked step enum")
    if status not in (RECHECKED_OK, RECHECKED_REFUSED):
        raise E1R8Error("rechecked status enum")
    return _put_header(TYPE_RECHECKED, 84, t) + struct.pack("<II", step, status)


def decode_rechecked(data, expected):
    if len(data) != 84:
        raise E1R8Error("rechecked length")
    _get_header(data, TYPE_RECHECKED, expected)
    step, status = struct.unpack_from("<II", data, 76)
    if not (RECHECK_STEP_ATTACH <= step <= RECHECK_STEP_CONFIRM):
        raise E1R8Error("rechecked step enum")
    if status not in (RECHECKED_OK, RECHECKED_REFUSED):
        raise E1R8Error("rechecked status enum")
    return Rechecked(step=step, status=status)


#
# Type 47: LAUNCH (B0->A). Body: account:str (canonical), evidence_ref:16.
# Total 97..159.
#

def encode_launch(t, account, evidence_ref):
    if not valid_account_name(account):
        raise E1R8Error("launch account")
    total = HEADER_SIZE + (4 + len(account)) + 16
    return _put_header(TYPE_LAUNCH, total, t) + _put_string(account) + \
        evidence_ref


def decode_launch(data, expected):
    if not (97 <= len(data) <= 159):
        raise E1R8Error("launch length")
    _get_header(data, TYPE_LAUNCH, expected)
    account, off = _get_string(data, 76)
    if not valid_account_name(account):
        raise E1R8Error("launch account")
    if off + 16 > len(data):
        raise E1R8Error("short: evidence_ref")
    evidence_ref = data[off:off + 16]
    off += 16
    if off != len(data):
        raise E1R8Error("launch trailing")
    return Launch(account=account, evidence_ref=evidence_ref)


#
# Type 48: LAUNCHED (A->B0). Body: status:u32 (1 committed, 2 failed
# before commit, 3 failed after commit), win32:u32. Total 84.
#

def encode_launched(t, status, win32):
    if status not in (LAUNCHED_COMMITTED, LAUNCHED_FAILED_BEFORE,
                      LAUNCHED_FAILED_AFTER):
        raise E1R8Error("launched status enum")
    return _put_header(TYPE_LAUNCHED, 84, t) + struct.pack("<II", status, win32)


def decode_launched(data, expected):
    if len(data) != 84:
        raise E1R8Error("launched length")
    _get_header(data, TYPE_LAUNCHED, expected)
    status, win32 = struct.unpack_from("<II", data, 76)
    if status not in (LAUNCHED_COMMITTED, LAUNCHED_FAILED_BEFORE,
                      LAUNCHED_FAILED_AFTER):
        raise E1R8Error("launched status enum")
    return Launched(status=status, win32=win32)


#
# Type 49: CANCEL (A<->B0). Body: reason:u32 (1 user, 2 D gone, 3
# deadline, 4 peer gone, 5 protocol, 6 recheck). Total 80. Distinct from
# R8's type 21 CANCEL.
#

def encode_cancel_r9(t, reason):
    if not (CANCEL_R9_USER <= reason <= CANCEL_R9_RECHECK):
        raise E1R8Error("cancel_r9 reason enum")
    return _put_header(TYPE_CANCEL_R9, 80, t) + struct.pack("<I", reason)


def decode_cancel_r9(data, expected):
    if len(data) != 80:
        raise E1R8Error("cancel_r9 length")
    _get_header(data, TYPE_CANCEL_R9, expected)
    reason = struct.unpack_from("<I", data, 76)[0]
    if not (CANCEL_R9_USER <= reason <= CANCEL_R9_RECHECK):
        raise E1R8Error("cancel_r9 reason enum")
    return CancelR9(reason=reason)


#
# Type 50: END (B0->A). Body: outcome:u32 (1 completed, 2 cancelled,
# 3 denied). Total 80.
#

def encode_end(t, outcome):
    if outcome not in (END_COMPLETED, END_CANCELLED, END_DENIED):
        raise E1R8Error("end outcome enum")
    return _put_header(TYPE_END, 80, t) + struct.pack("<I", outcome)


def decode_end(data, expected):
    if len(data) != 80:
        raise E1R8Error("end length")
    _get_header(data, TYPE_END, expected)
    outcome = struct.unpack_from("<I", data, 76)[0]
    if outcome not in (END_COMPLETED, END_CANCELLED, END_DENIED):
        raise E1R8Error("end outcome enum")
    return End(outcome=outcome)


#
# Type 51: NEXT_ATTEMPT (B0->A). Body: the next attempt's
# pre-endorsement tuple (204). Total 280. The header's request_id,
# attempt, nonce and channel_id equal the tuple's; the attempt is 2 or 3.
#

def encode_next_attempt(t):
    if t.attempt not in (2, 3):
        raise E1R8Error("next_attempt attempt enum")
    return _put_header(TYPE_NEXT_ATTEMPT, 280, t) + \
        encode_pre_endorsement_tuple(t)


def decode_next_attempt(data, expected):
    if len(data) != 280:
        raise E1R8Error("next_attempt length")
    _get_header(data, TYPE_NEXT_ATTEMPT, expected)
    if expected.attempt not in (2, 3):
        raise E1R8Error("next_attempt attempt enum")
    check_pre_endorsement(data, 76, expected)
    return NextAttempt(tuple=expected)


#
# D (INIT) and W (W_SETUP) learn the sealed tuple from the message itself:
# they know only the request id, attempt and channel id beforehand. Build
# the tuple from the header's nonce and the tail, then run the normal
# decoder against it, so every header, tail and body check still applies.
#

def learn_tuple(data, request_id, attempt, channel_id):
    if len(data) < HEADER_SIZE + TAIL_SIZE:
        raise E1R8Error("short: learn tuple")
    t = E1R8Tuple(request_id=struct.unpack_from("<Q", data, 12)[0],
                  attempt=struct.unpack_from("<I", data, 20)[0],
                  nonce=data[28:60], channel_id=data[60:76])
    if t.request_id != request_id or t.attempt != attempt or \
            t.channel_id != channel_id:
        raise E1R8Error("learn id mismatch")
    tail = decode_tuple_tail(data[HEADER_SIZE:HEADER_SIZE + TAIL_SIZE])
    t.binding_digest = tail.binding_digest
    t.request_digest = tail.request_digest
    t.deadline = tail.deadline
    t.root_gen = tail.root_gen
    t.server_gen = tail.server_gen
    t.broker_gen = tail.broker_gen
    t.verifier_gen = tail.verifier_gen
    t.map_gen = tail.map_gen
    t.policy_gen = tail.policy_gen
    t.caller_session = tail.caller_session
    t.desktop_route = tail.desktop_route
    t.desktop_mode = tail.desktop_mode
    t.dialog_id = tail.dialog_id
    return t


def decode_init_learn(data, request_id, attempt, channel_id):
    if not (256 <= len(data) <= 2067):
        raise E1R8Error("init length")
    t = learn_tuple(data, request_id, attempt, channel_id)
    return decode_init(data, t)


def decode_w_setup_learn(data, request_id, attempt, channel_id):
    if len(data) != 244:
        raise E1R8Error("w_setup length")
    t = learn_tuple(data, request_id, attempt, channel_id)
    return decode_w_setup(data, t)


#
# S7 version 2 PROMPT (spec section 7 step 4): the exact 178-byte prompt.
# 72-byte S7 header + channel_id:16, binding_digest:32, request_digest:32,
# dialog_id:16 from the sealed tuple + ASCII "Password: " (10 bytes, no
# NUL).
#

def encode_s7_prompt(t):
    header = struct.pack("<8sHHIQIIII", b"E1S7PAM\x00", 2, 2, S7_PROMPT_SIZE,
                         t.request_id, t.attempt, 1, 0, 1) + t.nonce
    return header + t.channel_id + t.binding_digest + t.request_digest + \
        t.dialog_id + b"Password: "


def decode_s7_prompt(data, expected):
    if len(data) != S7_PROMPT_SIZE:
        raise E1R8Error("s7 prompt length")
    if data[0:8] != b"E1S7PAM\x00":
        raise E1R8Error("s7 magic")
    version, mtype, length = struct.unpack_from("<HHI", data, 8)
    if version != 2:
        raise E1R8Error("s7 version")
    if mtype != 2:
        raise E1R8Error("s7 type")
    if length != S7_PROMPT_SIZE:
        raise E1R8Error("s7 length")
    request_id, attempt, round, index, style = \
        struct.unpack_from("<QIIII", data, 16)
    if request_id != expected.request_id:
        raise E1R8Error("s7 request_id")
    if attempt != expected.attempt:
        raise E1R8Error("s7 attempt")
    if round != 1:
        raise E1R8Error("s7 round")
    if index != 0:
        raise E1R8Error("s7 index")
    if style != 1:
        raise E1R8Error("s7 style")
    if data[40:72] != expected.nonce:
        raise E1R8Error("s7 nonce")
    if data[72:88] != expected.channel_id:
        raise E1R8Error("s7 channel_id")
    if data[88:120] != expected.binding_digest:
        raise E1R8Error("s7 binding_digest")
    if data[120:152] != expected.request_digest:
        raise E1R8Error("s7 request_digest")
    if data[152:168] != expected.dialog_id:
        raise E1R8Error("s7 dialog_id")
    if data[168:178] != b"Password: ":
        raise E1R8Error("s7 prompt text")
    return None


#
# Secret password answer (spec section 7 step 5): an S1 kind-2 record
# with answer_kind 1, round 1, index 0 and a payload of 1..4096 bytes
# (total 157..4252) that passes a strict RFC 3629 UTF-8 check.
#

def encode_secret_answer(t, required_caps, password):
    if len(password) < 1 or len(password) > 4096:
        raise E1R8Error("secret password length")
    if not valid_utf8(password):
        raise E1R8Error("secret password utf8")
    total = S1_RECORD_SIZE + len(password)
    record = struct.pack("<HHIQQIIII", S1_KIND, S1_VERSION, total,
                         required_caps, t.request_id, t.attempt, 1, 0,
                         S1_ANSWER_TEXT) + t.nonce + t.binding_digest + \
        t.request_digest + t.channel_id + struct.pack("<I", len(password)) + \
        password
    return record


def decode_secret_answer(data, expected, caps_mask):
    if not (157 <= len(data) <= 4252):
        raise E1R8Error("secret length")
    (kind, version, length, required_caps, request_id, attempt, round,
     message_index, answer_kind) = struct.unpack_from("<HHIQQIIII", data, 0)
    nonce = data[40:72]
    binding_digest = data[72:104]
    request_digest = data[104:136]
    channel_id = data[136:152]
    pw_len = struct.unpack_from("<I", data, 152)[0]
    payload = data[156:156 + pw_len]

    if kind != S1_KIND:
        raise E1R8Error("s1 kind")
    if version != S1_VERSION:
        raise E1R8Error("s1 version")
    if length != S1_RECORD_SIZE + pw_len:
        raise E1R8Error("s1 length")
    if required_caps & ~caps_mask:
        raise E1R8Error("s1 caps outside mask")
    if request_id != expected.request_id:
        raise E1R8Error("s1 request_id")
    if attempt != expected.attempt:
        raise E1R8Error("s1 attempt")
    if round != 1:
        raise E1R8Error("s1 round")
    if message_index != 0:
        raise E1R8Error("s1 index")
    if answer_kind != S1_ANSWER_TEXT:
        raise E1R8Error("s1 answer_kind")
    if nonce != expected.nonce:
        raise E1R8Error("s1 nonce")
    if binding_digest != expected.binding_digest:
        raise E1R8Error("s1 binding_digest")
    if request_digest != expected.request_digest:
        raise E1R8Error("s1 request_digest")
    if channel_id != expected.channel_id:
        raise E1R8Error("s1 channel_id")
    if pw_len < 1 or pw_len > 4096:
        raise E1R8Error("secret password length")
    if S1_RECORD_SIZE + pw_len != len(data):
        raise E1R8Error("secret trailing")
    if not valid_utf8(payload):
        raise E1R8Error("secret password utf8")
    return Answer(challenge=bytes(32), kind=kind, version=version,
                  length=length, required_caps=required_caps,
                  request_id=request_id, attempt=attempt, round=round,
                  message_index=message_index, answer_kind=answer_kind,
                  nonce=nonce, binding_digest=binding_digest,
                  request_digest=request_digest, channel_id=channel_id,
                  payload_len=pw_len, payload=payload)
