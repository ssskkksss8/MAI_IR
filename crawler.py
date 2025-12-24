#!/usr/bin/env python3
import sys
import time
import json
import hashlib
from typing import Dict, Any, Optional
import requests
import yaml
from pymongo import MongoClient, ASCENDING
from urllib.parse import urlparse, urlunparse, quote

def normalize_url(url: str) -> str:
    parsed = urlparse(url)
    scheme = parsed.scheme.lower() or "http"
    netloc = parsed.netloc.lower()
    path = parsed.path or "/"

    if path.endswith("/") and path != "/":
        path = path[:-1]

    return urlunparse((scheme, netloc, path, "", "", ""))

def now_ts() -> int:
    return int(time.time())

def sha256_hex(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", errors="ignore")).hexdigest()

class Crawler:
    def __init__(self, config_path: str):
        self.config = self._load_config(config_path)

        logic = self.config["logic"]
        self.delay = float(logic["delay_sec"])
        self.recrawl_interval = int(logic["recrawl_interval_sec"])
        self.max_documents = int(logic.get("max_documents", 10**9))

        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": "MAI-IR-Crawler/1.0 (student project; contact: example@example.com)"
        })

        db_cfg = self.config["db"]
        client = MongoClient(db_cfg["uri"])
        db = client[db_cfg["name"]]
        self.col = db[db_cfg["collection"]]

        self.col.create_index([("url", ASCENDING)], unique=True)
        self.col.create_index([("status", ASCENDING), ("next_crawl_at", ASCENDING)])

        print("[INIT] Crawler initialized")

    @staticmethod
    def _load_config(path: str) -> Dict[str, Any]:
        with open(path, "r", encoding="utf-8") as f:
            return yaml.safe_load(f)

    def ensure_seeds_loaded(self) -> None:
        if self.col.estimated_document_count() > 0:
            print("[SEEDS] Collection is not empty, skipping seeds import")
            return

        seeds_cfg = self.config["seeds"]
        if seeds_cfg["type"] != "wikipedia_pages_list":
            raise ValueError("Unsupported seeds.type in config")

        pages_list_path = seeds_cfg["pages_list_path"]
        base_url = seeds_cfg["base_url"]
        source_name = seeds_cfg["source_name"]

        print(f"[SEEDS] Loading pages list from {pages_list_path}")
        with open(pages_list_path, "r", encoding="utf-8") as f:
            pages = json.load(f)

        inserted = 0
        for i, p in enumerate(pages):
            title = p["title"].replace(" ", "_")
            url = base_url + quote(title)
            norm_url = normalize_url(url)

            doc = {
                "url": norm_url,
                "source": source_name,
                "html": None,
                "fetched_at": None,
                "etag": None,
                "last_modified": None,
                "content_hash": None,
                "next_crawl_at": 0,
                "status": "new",
                "error": None,
            }

            self.col.update_one(
                {"url": norm_url},
                {"$setOnInsert": doc},
                upsert=True
            )

            inserted += 1

        total = self.col.estimated_document_count()
        print(f"[SEEDS] Seeds imported (processed={inserted}), documents in DB: {total}")

    def _pick_next_document(self) -> Optional[Dict[str, Any]]:
        now = now_ts()
        query = {
            "$or": [
                {"status": "new"},
                {"status": "done", "next_crawl_at": {"$lte": now}},
            ]
        }
        doc = self.col.find_one(query, sort=[("next_crawl_at", ASCENDING)])
        return doc

    def _fetch_http(self, doc: Dict[str, Any]) -> Dict[str, Any]:
        headers = {}
        if doc.get("etag"):
            headers["If-None-Match"] = doc["etag"]
        if doc.get("last_modified"):
            headers["If-Modified-Since"] = doc["last_modified"]

        url = doc["url"]
        try:
            resp = self.session.get(url, headers=headers, timeout=20)
        except Exception as e:
            return {"status": "error", "error": str(e)}

        if resp.status_code == 304:
            return {"status": "not_changed"}

        if resp.status_code != 200:
            return {"status": "error", "error": "HTTP {}".format(resp.status_code)}

        html = resp.text
        etag = resp.headers.get("ETag")
        last_mod = resp.headers.get("Last-Modified")

        return {
            "status": "ok",
            "html": html,
            "etag": etag,
            "last_modified": last_mod,
        }

    def run(self) -> None:
        self.ensure_seeds_loaded()

        processed = 0
        max_docs = self.max_documents

        while processed < max_docs:
            doc = self._pick_next_document()
            if not doc:
                print("[MAIN] Queue is empty, nothing to crawl.")
                break

            url = doc["url"]
            print(f"[MAIN] Crawling: {url}")
            time.sleep(self.delay)

            http_res = self._fetch_http(doc)
            now = now_ts()
            next_crawl_at = now + self.recrawl_interval

            if http_res["status"] == "error":
                self.col.update_one(
                    {"_id": doc["_id"]},
                    {
                        "$set": {
                            "fetched_at": now,
                            "next_crawl_at": next_crawl_at,
                            "status": "error",
                            "error": http_res["error"],
                        }
                    }
                )
                print(f"[MAIN] Error: {http_res['error']}")
                processed += 1
                continue

            if http_res["status"] == "not_changed":
                self.col.update_one(
                    {"_id": doc["_id"]},
                    {
                        "$set": {
                            "fetched_at": now,
                            "next_crawl_at": next_crawl_at,
                            "status": "done",
                            "error": None,
                        }
                    }
                )
                print("[MAIN] Not changed (HTTP 304)")
                processed += 1
                continue

            new_html = http_res["html"]
            new_hash = sha256_hex(new_html)

            old_hash = doc.get("content_hash")
            changed = (old_hash is None) or (old_hash != new_hash)

            if not changed:
                self.col.update_one(
                    {"_id": doc["_id"]},
                    {
                        "$set": {
                            "fetched_at": now,
                            "next_crawl_at": next_crawl_at,
                            "status": "done",
                            "error": None,
                            "etag": http_res["etag"],
                            "last_modified": http_res["last_modified"],
                        }
                    }
                )
                print("[MAIN] Not changed (by content hash)")
            else:
                self.col.update_one(
                    {"_id": doc["_id"]},
                    {
                        "$set": {
                            "html": new_html,
                            "fetched_at": now,
                            "next_crawl_at": next_crawl_at,
                            "status": "done",
                            "error": None,
                            "etag": http_res["etag"],
                            "last_modified": http_res["last_modified"],
                            "content_hash": new_hash,
                        }
                    }
                )
                size_kb = len(new_html) / 1024.0
                print(f"[MAIN] Downloaded, size ≈ {size_kb:.1f} KB, changed={changed}")

            processed += 1

        print(f"[MAIN] Finished. Processed {processed} documents (limit={max_docs}).")

def main() -> None:
    if len(sys.argv) != 2:
        print("Usage: python crawler.py path/to/config.yaml")
        sys.exit(1)

    config_path = sys.argv[1]
    crawler = Crawler(config_path)
    crawler.run()

if __name__ == "__main__":
    main()