#!/usr/bin/env python3
import os
import json
import time
import requests
from collections import deque

SAVE_DIR = r"C:\Users\user\YandexDisk\corpus_aviation"

MAX_PAGES = 1_000_000
BATCH_SIZE = 20_000

API_DELAY = 0.2
PAGE_DELAY = 0.05

MIN_TEXT_CHARS = 300

PAGES_LIST_PATH = os.path.join(SAVE_DIR, "pages_list.json")

SOURCES = [
    {
        "source": "wikipedia_ru",
        "api_url": "https://ru.wikipedia.org/w/api.php",
        "lang": "ru",
        "start_categories": [
            "Категория:Транспорт",
            "Категория:Авиация",
            "Категория:Авиация по странам",
            "Категория:Виды авиации",
            "Категория:Аэропорты",
            "Категория:Порты",
            "Категория:Транспортная инфраструктура",
        ],
    },
    {
        "source": "wikipedia_en",
        "api_url": "https://en.wikipedia.org/w/api.php",
        "lang": "en",
        "start_categories": [
            "Category:Transport",
            "Category:Aviation",
            "Category:Airports",
            "Category:Air traffic control",
            "Category:Aircraft",
            "Category:Airlines",
            "Category:Air safety",
        ],
    },
    {
        "source": "skybrary",
        "api_url": "https://www.skybrary.aero/api.php",
        "lang": "en",
        "start_categories": [
            "Category:Air Operations",
            "Category:Safety Regulations",
            "Category:Enhancing Safety",
            "Category:Operational Issues",
        ],
    },
]

session = requests.Session()
session.headers.update({
    "User-Agent": "MAI-IR-TransportCorpus/1.0 (student project; contact: example@example.com)"
})

def fetch_category_members(api_url, cat, cmcontinue=None):
    params = {
        "action": "query",
        "list": "categorymembers",
        "cmtitle": cat,
        "cmtype": "page|subcat",
        "cmlimit": "max",
        "format": "json",
    }
    if cmcontinue:
        params["cmcontinue"] = cmcontinue
    r = session.get(api_url, params=params, timeout=30)
    r.raise_for_status()
    data = r.json()
    members = data.get("query", {}).get("categorymembers", [])
    next_token = data.get("continue", {}).get("cmcontinue")
    return members, next_token

def get_plain_text(api_url, pageid):
    params = {
        "action": "query",
        "pageids": pageid,
        "prop": "extracts",
        "explaintext": True,
        "format": "json",
    }
    r = session.get(api_url, params=params, timeout=40)
    r.raise_for_status()
    pages = r.json().get("query", {}).get("pages", {})
    p = pages.get(str(pageid), {})
    return p.get("title", ""), p.get("extract", "")

def collect_pages_multi_source():
    pages = {}
    visited = set()
    q = deque()

    for s in SOURCES:
        for c in s["start_categories"]:
            q.append((s["source"], s["api_url"], s["lang"], c))

    while q and len(pages) < MAX_PAGES:
        source, api_url, lang, cat = q.popleft()
        key = (source, cat)
        if key in visited:
            continue
        visited.add(key)

        cmc = None
        while True:
            try:
                members, cmc = fetch_category_members(api_url, cat, cmc)
            except Exception:
                break

            for m in members:
                ns = m.get("ns")
                title = m.get("title", "")
                pageid = m.get("pageid")

                if ns == 14 and title:
                    q.append((source, api_url, lang, title))
                elif ns == 0 and pageid is not None:
                    doc_key = (source, int(pageid))
                    if doc_key not in pages:
                        pages[doc_key] = {"pageid": int(pageid), "title": title, "lang": lang, "source": source}
                        if len(pages) >= MAX_PAGES:
                            break

            if len(pages) >= MAX_PAGES:
                break
            if not cmc:
                break
            time.sleep(API_DELAY)

    return list(pages.values())

def save_pages_list(pages_list):
    with open(PAGES_LIST_PATH, "w", encoding="utf-8") as f:
        json.dump(pages_list, f, ensure_ascii=False, indent=2)

def load_pages_list():
    with open(PAGES_LIST_PATH, "r", encoding="utf-8") as f:
        return json.load(f)

def download_texts(pages_list):
    os.makedirs(SAVE_DIR, exist_ok=True)

    existing_batches = [
        name for name in os.listdir(SAVE_DIR)
        if name.startswith("batch_") and name.endswith(".jsonl")
    ]

    if existing_batches:
        max_batch = max(int(name[6:10]) for name in existing_batches)
        batch_id = max_batch + 1
    else:
        batch_id = 1

    processed = 0
    for name in existing_batches:
        path = os.path.join(SAVE_DIR, name)
        try:
            with open(path, "r", encoding="utf-8") as f:
                for _ in f:
                    processed += 1
        except Exception:
            pass

    current_batch_path = os.path.join(SAVE_DIR, f"batch_{batch_id:04}.jsonl")
    f = open(current_batch_path, "a", encoding="utf-8")
    counter_in_batch = processed % BATCH_SIZE

    api_url_by_source = {s["source"]: s["api_url"] for s in SOURCES}

    total_pages = len(pages_list)
    for idx, page in enumerate(pages_list):
        if idx < processed:
            continue

        pageid = page["pageid"]
        source = page.get("source", "wikipedia_ru")
        lang = page.get("lang", "ru")
        api_url = api_url_by_source.get(source)

        if not api_url:
            continue

        try:
            title, text = get_plain_text(api_url, pageid)
        except Exception:
            continue

        if not text or len(text) < MIN_TEXT_CHARS:
            continue

        doc = {"pageid": pageid, "title": title, "lang": lang, "source": source, "text": text}
        f.write(json.dumps(doc, ensure_ascii=False) + "\n")
        counter_in_batch += 1

        if counter_in_batch >= BATCH_SIZE:
            f.close()
            batch_id += 1
            current_batch_path = os.path.join(SAVE_DIR, f"batch_{batch_id:04}.jsonl")
            f = open(current_batch_path, "a", encoding="utf-8")
            counter_in_batch = 0

        if (idx + 1) % 200 == 0:
            pass

        time.sleep(PAGE_DELAY)

    f.close()

def main():
    os.makedirs(SAVE_DIR, exist_ok=True)

    need_collect = True
    if os.path.exists(PAGES_LIST_PATH):
        try:
            tmp = load_pages_list()
            if isinstance(tmp, list) and len(tmp) > 0:
                need_collect = False
        except Exception:
            need_collect = True

    if need_collect:
        pages_list = collect_pages_multi_source()
        save_pages_list(pages_list)

    pages_list = load_pages_list()
    if len(pages_list) == 0:
        return

    download_texts(pages_list)

if __name__ == "__main__":
    main()
