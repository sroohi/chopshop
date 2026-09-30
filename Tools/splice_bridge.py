#!/usr/bin/env python3
"""File-queue helper for the ChopShop <-> Claude Splice bridge.

Subcommands (all output JSON on stdout):
  pending                       list work: new searches and approved downloads
  wait [--timeout SEC]          block until there is work (heartbeats meanwhile), then print it
  heartbeat                     mark the bridge as online
  status ID STATUS [MESSAGE]    set a result's status (searching|results|downloading|done|error)
  candidates ID < json          store candidates: [{uuid,name,bpm,key,type,duration,url}, ...] (max 5 kept)
  approved ID                   print approved UUIDs, filtered to that request's candidates
  add-file ID UUID PATH         record a downloaded file (must exist and be non-empty)
  dest ID NAME                  print a safe destination path for a download
"""
import json, os, re, sys, time
from pathlib import Path

LIB = Path.home() / "Music" / "ChopShop Library"
ROOT = LIB / ".bridge"
REQ, RES, APP = (ROOT / d for d in ("requests", "results", "approvals"))
DL = LIB / "Splice"
ID_RE = re.compile(r"^req-\d+$")


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
    tmp = p.with_suffix(p.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, indent=2))
    os.replace(tmp, p)


def result(i):
    return load(RES / f"{i}.json", {"id": i, "status": "", "message": "", "candidates": [], "files": []})


def heartbeat():
    ensure()
    (ROOT / "bridge.alive").write_text(time.strftime("%Y-%m-%dT%H:%M:%S"))


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


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == "pending":
        print(json.dumps(pending(), indent=2))
    elif cmd == "heartbeat":
        heartbeat()
        print("{}")
    elif cmd == "wait":
        timeout = float(a[2]) if len(a) > 2 and a[1] == "--timeout" else 1500
        start = last_hb = 0
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
        i = check_id(a[1])
        name = re.sub(r"[^A-Za-z0-9._ -]+", "_", a[2]).strip() or "sample.wav"
        DL.mkdir(parents=True, exist_ok=True)
        print(DL / name)
    elif cmd == "add-file":
        i, uuid, path = check_id(a[1]), a[2], Path(a[3])
        if not path.is_file() or path.stat().st_size == 0:
            sys.exit(f"missing or empty file: {path}")
        r = result(i)
        r["files"] = [f for f in r.get("files", []) if f.get("uuid") != uuid] + [{"uuid": uuid, "path": str(path)}]
        save(RES / f"{i}.json", r)
        print(json.dumps({"id": i, "files": len(r["files"])}))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
