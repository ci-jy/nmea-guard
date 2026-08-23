"""Independent Python statement of which sentences count as clean.

The C++ parser is not consulted here. A sentence is clean when it frames
correctly, its checksum matches, and every field it carries is well formed
for its type. The differential test checks that nmea-guard accepts exactly
these sentences, and that its decoded values match pynmea2 on all of them.
"""

import re

DECODED_TYPES = ("GGA", "RMC", "GSA", "VTG")

_FRAME = re.compile(r"^\$([^*]*)\*([0-9A-Fa-f]{2})$")
_DECIMAL = re.compile(r"^-?(\d+\.?\d*|\.\d+)$")
_UINT = re.compile(r"^\d{1,9}$")
_TIME = re.compile(r"^(\d\d)(\d\d)(\d\d)(\.\d*)?$")
_DATE = re.compile(r"^(\d\d)(\d\d)(\d\d)$")
MODE_LETTERS = "ADEMNSFRPC"

MIN_FIELDS = {"GGA": 14, "RMC": 12, "GSA": 18, "VTG": 9}


def framed_sentences(data: bytes):
    """Yield every checksum-valid sentence in a raw log, the way a streaming
    framer sees them: from the last '$' on a line to the line end."""
    for line in re.split(rb"[\r\n]", data):
        start = line.rfind(b"$")
        if start < 0:
            continue
        chunk = line[start:]
        if any(b < 0x20 or b > 0x7E for b in chunk):
            continue
        text = chunk.decode("ascii")
        m = _FRAME.match(text)
        if not m:
            continue
        body = m.group(1)
        checksum = 0
        for ch in body:
            checksum ^= ord(ch)
        if checksum == int(m.group(2), 16) and len(body) >= 5:
            yield text, body


def _decimal(s):
    return s == "" or bool(_DECIMAL.match(s))


def _uint(s):
    return s == "" or bool(_UINT.match(s))


def _time(s):
    if s == "":
        return True
    m = _TIME.match(s)
    if not m:
        return False
    h, mi, se = (int(x) for x in m.groups()[:3])
    return h <= 23 and mi <= 59 and se <= 60


def _date(s):
    if s == "":
        return True
    m = _DATE.match(s)
    return bool(m) and 1 <= int(m.group(1)) <= 31 and 1 <= int(m.group(2)) <= 12


def _angle(value, hemi, deg_digits, max_deg, letters):
    if value == "" and hemi == "":
        return True
    if value == "" or hemi not in letters or len(hemi) != 1:
        return False
    whole, _, frac = value.partition(".")
    if len(whole) != deg_digits + 2 or not whole.isdigit():
        return False
    if frac and not frac.isdigit():
        return False
    minutes = float(value[deg_digits:])
    return minutes < 60 and int(whole[:deg_digits]) + minutes / 60 <= max_deg


def _flag(s, allowed):
    return s == "" or (len(s) == 1 and s in allowed)


def _unit(s, letter):
    return s in ("", letter)


def sentence_type(body: str) -> str | None:
    addr = body.split(",", 1)[0]
    if len(addr) == 5 and addr[0] != "P" and re.match(r"^[A-Z0-9]{5}$", addr) and addr[2:] in DECODED_TYPES:
        return addr[2:]
    return None


def is_clean(body: str) -> bool:
    kind = sentence_type(body)
    if kind is None:
        return False
    f = body.split(",")
    if len(f) < MIN_FIELDS[kind] or len(f) > 40:
        return False
    f += [""] * 20  # read past the end as blanks
    if kind == "GGA":
        return (_time(f[1]) and _angle(f[2], f[3], 2, 90, "NS") and _angle(f[4], f[5], 3, 180, "EW")
                and _uint(f[6]) and (f[6] == "" or int(f[6]) <= 9) and _uint(f[7])
                and _decimal(f[8]) and _decimal(f[9]) and _unit(f[10], "M")
                and _decimal(f[11]) and _unit(f[12], "M") and _decimal(f[13]))
    if kind == "RMC":
        ok = (_time(f[1]) and _flag(f[2], "AV") and _angle(f[3], f[4], 2, 90, "NS")
              and _angle(f[5], f[6], 3, 180, "EW") and _decimal(f[7]) and _decimal(f[8])
              and _date(f[9]) and _decimal(f[10])
              and (f[11] == "" or (len(f[11]) == 1 and f[11].isupper())))
        return ok and _flag(f[12], MODE_LETTERS)
    if kind == "GSA":
        prns_ok = all(_uint(p) and (p == "" or int(p) <= 999) for p in f[3:15])
        return (_flag(f[1], "AM") and _uint(f[2]) and (f[2] == "" or 1 <= int(f[2]) <= 3)
                and prns_ok and _decimal(f[15]) and _decimal(f[16]) and _decimal(f[17]))
    if kind == "VTG":
        return (_decimal(f[1]) and _unit(f[2], "T") and _decimal(f[3]) and _unit(f[4], "M")
                and _decimal(f[5]) and _unit(f[6], "N") and _decimal(f[7]) and _unit(f[8], "K")
                and _flag(f[9], MODE_LETTERS))
    return False
