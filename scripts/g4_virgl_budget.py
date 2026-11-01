# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure UTC budget calculation; never proves live launcher/child ownership."""
from datetime import datetime, timezone
import json
import math
import re

LAUNCHER_SECONDS = 900


def _aware(value):
    if isinstance(value, str):
        if not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})', value):
            raise ValueError('Timestamp must be explicit ISO-8601 with a timezone')
        value = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if not isinstance(value, datetime) or value.tzinfo is None or value.utcoffset() is None:
        raise ValueError('Timestamp must be timezone-aware')
    return value.astimezone(timezone.utc)


def check_budget(raw_session, expected_run, required_seconds, *, now=None):
    """Return fail-closed evidence from RAW JSON and an injected aware clock.

    The Windows controller start precedes the Linux 900-second launcher clock.
    This conservative UTC estimate does not replace exact Linux identity checks.
    No filesystem, process, network access, sleeps, or timezone-local conversion.
    """
    result = {'passed': False, 'lifetime_seconds': LAUNCHER_SECONDS,
              'ownership_proven': False}
    try:
        if (type(required_seconds) not in (int, float) or not math.isfinite(required_seconds)
                or not 0 < required_seconds <= LAUNCHER_SECONDS):
            raise ValueError('Required budget must be finite and within (0, 900] seconds')
        result['required_seconds'] = required_seconds
        if not isinstance(expected_run, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,79}', expected_run):
            raise ValueError('Invalid expected run name')
        result['expected_run'] = expected_run
        if not isinstance(raw_session, (str, bytes)):
            raise ValueError('Supply raw session JSON, not an already-converted object')
        if isinstance(raw_session, bytes):
            raw_session = raw_session.decode('utf-8-sig')
        def unique(pairs):
            obj = {}
            for key, value in pairs:
                if key in obj:
                    raise ValueError('Duplicate session JSON key: ' + key)
                obj[key] = value
            return obj
        def invalid_constant(value):
            raise ValueError('Invalid JSON constant: ' + value)
        session = json.loads(raw_session.removeprefix('\ufeff'), object_pairs_hook=unique,
                             parse_constant=invalid_constant)
        if not isinstance(session, dict) or session.get('runName') != expected_run:
            raise ValueError('Session run name does not match expected run')
        if session.get('state') != 'running':
            raise ValueError('Session is not running')
        if type(session.get('windowsPid')) is not int or session['windowsPid'] <= 0:
            raise ValueError('Missing/invalid Windows session PID')
        # A controller timestamp MUST remain a JSON string, never an object/date.
        stamp = session.get('windowsStartTimeUtc')
        if not isinstance(stamp, str):
            raise ValueError('Missing/invalid raw controller timestamp')
        start = _aware(stamp)
        observed = _aware(datetime.now(timezone.utc) if now is None else now)
        result.update(controller_start_utc=start.isoformat(), observed_utc=observed.isoformat(),
                      windows_pid=session['windowsPid'])
        elapsed = (observed - start).total_seconds()
        if not math.isfinite(elapsed) or elapsed < 0:
            raise ValueError('Controller timestamp is in the future or elapsed time is invalid')
        remaining = LAUNCHER_SECONDS - elapsed
        result.update(elapsed_seconds=elapsed, remaining_seconds=remaining)
        if remaining < required_seconds:
            raise ValueError('Insufficient launcher budget')
        result['passed'] = True
    except (ValueError, TypeError, OverflowError, UnicodeError) as error:
        result['error'] = str(error)
    return result
