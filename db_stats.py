#!/usr/bin/env python3
import argparse
from pymongo import MongoClient


def human(n):
    if n is None:
        return "N/A"
    n = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024.0:
            return f"{n:0.1f} {unit}"
        n /= 1024.0
    return f"{n:0.1f} PB"


def print_db_stats(client, dbname):
    db = client[dbname]
    stats = db.command("dbstats")
    print(f"Database: {dbname}")
    print(f"  collections: {stats.get('collections')}, objects: {stats.get('objects')}")
    print(f"  dataSize:    {human(stats.get('dataSize'))} ({stats.get('dataSize')})")
    print(f"  storageSize: {human(stats.get('storageSize'))} ({stats.get('storageSize')})")
    print(f"  indexSize:   {human(stats.get('indexSize'))} ({stats.get('indexSize')})")
    print()

    print("Collection statistics:")
    for coll_name in sorted(db.list_collection_names()):
        cs = db.command("collstats", coll_name)
        print(
            f"  {coll_name}: count={cs.get('count')}, size={human(cs.get('size'))}, "
            f"storageSize={human(cs.get('storageSize'))}, nindexes={cs.get('nindexes')}"
        )
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uri", default="mongodb://localhost:27017", help="MongoDB URI")
    ap.add_argument("--db", required=True, help="Database name to inspect")
    args = ap.parse_args()

    client = MongoClient(args.uri)
    try:
        client.admin.command("ping")
    except Exception as e:
        print("Cannot connect to MongoDB:", e)
        return

    print_db_stats(client, args.db)


if __name__ == "__main__":
    main()
