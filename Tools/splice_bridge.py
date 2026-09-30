#!/usr/bin/env python3
"""File-queue helper for the ChopShop <-> Claude Splice bridge.

Subcommands (all output JSON on stdout):
  pending                       list work: new searches and approved downloads
  wait [--timeout SEC]          block until there is work (heartbeats meanwhile), then print it
  heartbeat                     mark the bridge as online
  status ID STATUS [MESSAGE]    set a result's status (searching|results|downloading|done|error)
  candidates ID < json          store candidates: [{uuid,name,bpm,key,type,duration,url}, ...] (max 5 kept)
  approved ID                   print approved UUIDs, filtered to that request's candidates
  add-file ID UUID PATH         record a downloaded file (must be a non-empty audio file inside the Splice folder)
  dest ID UUID [EXT]            print a safe destination path for a candidate (name taken from the stored candidate)
  fetch ID UUID < url           download an https URL (read from stdin) to that candidate's destination and record it
  prune                         delete queue files older than a week (downloaded audio is kept)
"""
import json, os, re, shutil, sys, time, urllib.request
from pathlib import Path

LIB = Path.home() / "Music" / "ChopShop Library"
ROOT = LIB / ".bridge"
REQ, RES, APP = (ROOT / d for d in ("requests", "results", "approvals"))
DL = LIB / "Splice"
ID_RE = re.compile(r"^req-\d+$")
UUID_RE = re.compile(r"^[A-Za-z0-9-]{1,64}$")
AUDIO_EXTS = ("wav", "aif", "aiff", "flac", "mp3", "m4a", "caf", "ogg")
MAX_AGE = 7 * 24 * 3600


def ensure():
    for d in (REQ, RES, APP, DL):
        d.mkdir(parents=True, exist_ok=True)


def check_id(i):
    if not ID_RE.match(i):
        sys.exit(f"bad id: {i!r}")
    return i


def load(p, default=None):
    try:
        return json.loads(p.read_text())
    except Exception:
        return default


def save(p, obj):
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(p.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, indent=2))
    os.replace(tmp, p)


def result(i):
    return load(RES / f"{i}.json", {"id": i, "status": "", "message": "", "candidates": [], "files": []})


def heartbeat():
    ensure()
    (ROOT / "bridge.alive").write_text(time.strftime("%Y-%m-%dT%H:%M:%S"))


def prune():
    """Drop requests/results/approvals older than MAX_AGE so the queue scans stay small."""
    ensure()
    cutoff = (time.time() - MAX_AGE) * 1000
    removed = 0
    for d in (REQ, RES, APP):
        for f in d.glob("req-*.json"):
            if ID_RE.match(f.stem) and int(f.stem[4:]) < cutoff:
                f.unlink(missing_ok=True)
                removed += 1
    return removed


def pending():
    ensure()
    work = {"searches": [], "downloads": []}
    for f in sorted(REQ.glob("req-*.json")):
        r = load(f)
        if not r or not ID_RE.match(str(r.get("id", ""))):
            continue
        if not (RES / f.name).exists():
            work["searches"].append({"id": r["id"], "query": str(r.get("query", ""))[:300],
                                     "host_bpm": r.get("host_bpm", 0)})
    for f in sorted(APP.glob("req-*.json")):
        i = f.stem
        if ID_RE.match(i) and result(i).get("status") == "results":
            work["downloads"].append({"id": i})
    return work


def candidate(i, uuid):
    c = next((c for c in result(i).get("candidates", []) if c.get("uuid") == uuid), None)
    if c is None:
        sys.exit(f"unknown candidate: {uuid!r}")
    return c


def dest_path(i, uuid, ext="wav"):
    # The file name comes from the stored candidate, never from the command line, so untrusted
    # Splice titles are never interpolated into a shell command.
    ext = ext.lower().lstrip(".")
    if ext not in AUDIO_EXTS:
        sys.exit(f"bad extension: {ext!r}")
    stem = re.sub(r"[^A-Za-z0-9_ -]+", "_", str(candidate(i, uuid).get("name", ""))).strip(" _") or uuid
    DL.mkdir(parents=True, exist_ok=True)
    return DL / f"{stem[:120]}.{ext}"


def sniff_ext(head):
    if head[:4] == b"RIFF" and head[8:12] == b"WAVE":
        return "wav"
    if head[:4] == b"FORM" and head[8:12] in (b"AIFF", b"AIFC"):
        return "aif"
    if head[:4] == b"fLaC":
        return "flac"
    if head[:4] == b"OggS":
        return "ogg"
    if head[:3] == b"ID3" or head[:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2"):
        return "mp3"
    if head[4:8] == b"ftyp":
        return "m4a"
    sys.exit("downloaded file is not a recognised audio format")


def record_file(i, uuid, path):
    """Record a downloaded file. Only audio files directly inside the Splice folder are accepted."""
    path = path.resolve()
    if path.parent != DL.resolve() or path.suffix.lower().lstrip(".") not in AUDIO_EXTS:
        sys.exit(f"not an audio file in {DL}: {path}")
    candidate(i, uuid)
    if not path.is_file() or path.stat().st_size == 0:
        sys.exit(f"missing or empty file: {path}")
    r = result(i)
    r["files"] = [f for f in r.get("files", []) if f.get("uuid") != uuid] + [{"uuid": uuid, "path": str(path)}]
    save(RES / f"{i}.json", r)
    return {"id": i, "files": len(r["files"])}


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == "pending":
        print(json.dumps(pending(), indent=2))
    elif cmd == "prune":
        print(json.dumps({"removed": prune()}))
    elif cmd == "heartbeat":
        heartbeat()
        print("{}")
    elif cmd == "wait":
        timeout = float(a[2]) if len(a) > 2 and a[1] == "--timeout" else 1500
        prune()
        last_hb = 0
        start = time.time()
        while True:
            if time.time() - last_hb > 30:
                heartbeat()
                last_hb = time.time()
            w = pending()
            if w["searches"] or w["downloads"] or time.time() - start > timeout:
                print(json.dumps(w, indent=2))
                return
            time.sleep(1)
    elif cmd == "status":
        i = check_id(a[1])
        r = result(i)
        r["status"] = a[2]
        r["message"] = a[3] if len(a) > 3 else ""
        save(RES / f"{i}.json", r)
        print(json.dumps({"id": i, "status": a[2]}))
    elif cmd == "candidates":
        i = check_id(a[1])
        raw = json.load(sys.stdin)
        keep = []
        for c in raw[:5]:
            if not UUID_RE.match(str(c.get("uuid", ""))):
                continue
            keep.append({k: c.get(k, "" if k not in ("bpm", "duration") else 0)
                         for k in ("uuid", "name", "bpm", "key", "type", "duration", "url")})
        r = result(i)
        r.update(candidates=keep, status="results")
        save(RES / f"{i}.json", r)
        print(json.dumps({"id": i, "count": len(keep)}))
    elif cmd == "approved":
        i = check_id(a[1])
        ap = load(APP / f"{i}.json", {}) or {}
        allowed = {c["uuid"] for c in result(i).get("candidates", [])}
        print(json.dumps([u for u in ap.get("uuids", []) if u in allowed][:5]))
    elif cmd == "dest":
        print(dest_path(check_id(a[1]), a[2], a[3] if len(a) > 3 else "wav"))
    elif cmd == "fetch":
        i, uuid = check_id(a[1]), a[2]
        url = sys.stdin.read().strip()
        if not url.startswith("https://"):
            sys.exit("url must be https")
        tmp = dest_path(i, uuid).with_suffix(".part")
        try:
            with urllib.request.urlopen(url, timeout=120) as resp, open(tmp, "wb") as out:
                shutil.copyfileobj(resp, out)
            with open(tmp, "rb") as f:
                ext = sniff_ext(f.read(12))
            path = dest_path(i, uuid, ext)
            os.replace(tmp, path)
        finally:
            tmp.unlink(missing_ok=True)
        print(json.dumps(record_file(i, uuid, path)))
    elif cmd == "add-file":
        print(json.dumps(record_file(check_id(a[1]), a[2], Path(a[3]))))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
