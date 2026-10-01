"""Read-only pre/post-upgrade inventory; never emits credentials or record values."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3


def inventory(database: Path, objects: Path) -> dict:
    database = database.resolve(strict=True)
    objects = objects.resolve(strict=True)
    with sqlite3.connect(database.as_uri() + "?mode=ro", uri=True) as db:
        db.execute("BEGIN")
        if db.execute("PRAGMA quick_check").fetchone()[0] != "ok":
            raise ValueError("Database integrity check failed")
        tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if not {"users", "api_keys", "files", "versions"} <= tables:
            raise ValueError("Not a Cloud Backup database")
        counts = {t: db.execute(f'SELECT COUNT(*) FROM "{t}"').fetchone()[0]
                  for t in ("users", "api_keys", "files", "versions")}
        metadata = {}
        for table in counts:
            h = hashlib.sha256()
            columns = [r[1] for r in db.execute(f'PRAGMA table_info("{table}")')
                       if r[1] != "last_used_at"]
            projection = ",".join('"' + c + '"' for c in columns)
            for row in db.execute(f'SELECT {projection} FROM "{table}" ORDER BY id'):
                values = [v.hex() if isinstance(v, bytes) else v for v in row]
                h.update(json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode())
                h.update(b"\n")
            metadata[table] = h.hexdigest()
        digest = hashlib.sha256()
        count = 0
        total = 0
        for key, size, sha in db.execute("SELECT object_key,size_bytes,sha256 FROM versions WHERE state='active' ORDER BY object_key"):
            path = (objects / key).resolve(strict=True)
            if not path.is_relative_to(objects) or not path.is_file():
                raise ValueError("An active object is missing or outside the data directory")
            actual = hashlib.sha256()
            with path.open("rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    actual.update(chunk)
            if path.stat().st_size != size or actual.hexdigest() != sha:
                raise ValueError("An active object failed its integrity check")
            digest.update(json.dumps([key, size, sha], separators=(",", ":")).encode())
            count += 1
            total += size
        return {"counts": counts, "metadata_sha256": metadata, "active_objects": count, "active_bytes": total,
                "object_inventory_sha256": digest.hexdigest(), "integrity": "ok"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("objects", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    result = inventory(args.database, args.objects)
    if args.compare and result != json.loads(args.compare.read_text()):
        raise SystemExit("Inventory differs; keep old data and investigate before switching traffic")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
