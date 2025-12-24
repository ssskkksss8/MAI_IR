import json
from unittest.mock import patch, MagicMock
import pytest

import crawler


class FakeCollection:
    def __init__(self, initial_docs=None):
        self.docs = list(initial_docs or [])

    def create_index(self, *args, **kwargs):
        return "ok"

    def estimated_document_count(self):
        return len(self.docs)

    def update_one(self, filter_doc, update_doc, upsert=False):
        url = filter_doc.get("url")
        existing = None
        for d in self.docs:
            if d.get("url") == url:
                existing = d
                break

        if existing is None:
            if upsert:
                soi = update_doc.get("$setOnInsert", {})
                self.docs.append(dict(soi))

    def find_one(self, query, sort=None):
        now = int(crawler.time.time())

        def eligible(d):
            if d.get("status") == "new":
                return True
            if d.get("status") == "done" and d.get("next_crawl_at", 0) <= now:
                return True
            return False

        candidates = [d for d in self.docs if eligible(d)]
        if not candidates:
            return None

        if sort:
            key, direction = sort[0]
            candidates.sort(key=lambda x: x.get(key, 0))

        return dict(candidates[0])


def make_fake_mongo(fake_col):
    client = MagicMock()
    db = MagicMock()
    db.__getitem__.return_value = fake_col
    client.__getitem__.return_value = db
    return client


@pytest.fixture
def fake_pages_list_path(tmp_path):
    p = tmp_path / "pages_list.json"
    with open(str(p), "w", encoding="utf-8") as f:
        json.dump([{"title": "Test Page"}], f, ensure_ascii=False)
    return str(p)


@pytest.fixture
def fake_config(fake_pages_list_path):
    return {
        "logic": {"delay_sec": 0, "recrawl_interval_sec": 10, "max_documents": 10},
        "db": {"uri": "mongodb://fake", "name": "test", "collection": "docs"},
        "seeds": {
            "type": "wikipedia_pages_list",
            "pages_list_path": fake_pages_list_path,
            "base_url": "https://example.org/wiki/",
            "source_name": "test_source",
        },
    }


@pytest.fixture
def crawler_instance(fake_config):
    fake_col = FakeCollection()
    fake_client = make_fake_mongo(fake_col)

    with patch.object(crawler.Crawler, "_load_config", return_value=fake_config), \
         patch.object(crawler, "MongoClient", return_value=fake_client):
        c = crawler.Crawler("fake.yaml")
        return c


def test_ensure_seeds_loaded_imports(crawler_instance):
    c = crawler_instance
    c.ensure_seeds_loaded()
    assert c.col.estimated_document_count() == 1
    doc = c.col.docs[0]
    assert doc["status"] == "new"
    assert doc["url"].startswith("https://example.org/wiki/")


def test_ensure_seeds_loaded_skips_if_not_empty(fake_config):
    fake_col = FakeCollection(initial_docs=[{"url": "x", "status": "new"}])
    fake_client = make_fake_mongo(fake_col)

    with patch.object(crawler.Crawler, "_load_config", return_value=fake_config), \
         patch.object(crawler, "MongoClient", return_value=fake_client):
        c = crawler.Crawler("fake.yaml")
        c.ensure_seeds_loaded()
        assert c.col.estimated_document_count() == 1


def test_pick_next_document(crawler_instance):
    c = crawler_instance
    c.col.docs = [
        {"url": "a", "status": "done", "next_crawl_at": 9999999999},
        {"url": "b", "status": "new", "next_crawl_at": 0},
    ]
    doc = c._pick_next_document()
    assert doc is not None
    assert doc["url"] == "b"


def test_fetch_http_exception(crawler_instance):
    c = crawler_instance
    doc = {"url": "https://example.org", "etag": None, "last_modified": None}

    with patch.object(c.session, "get", side_effect=Exception("boom")):
        res = c._fetch_http(doc)
        assert res["status"] == "error"
        assert "boom" in res["error"]


def test_fetch_http_304(crawler_instance):
    c = crawler_instance
    doc = {"url": "https://example.org", "etag": "E", "last_modified": "L"}

    resp = MagicMock()
    resp.status_code = 304

    with patch.object(c.session, "get", return_value=resp):
        res = c._fetch_http(doc)
        assert res["status"] == "not_changed"


def test_fetch_http_non_200(crawler_instance):
    c = crawler_instance
    doc = {"url": "https://example.org", "etag": None, "last_modified": None}

    resp = MagicMock()
    resp.status_code = 500

    with patch.object(c.session, "get", return_value=resp):
        res = c._fetch_http(doc)
        assert res["status"] == "error"
        assert "HTTP 500" in res["error"]


def test_fetch_http_ok(crawler_instance):
    c = crawler_instance
    doc = {"url": "https://example.org", "etag": None, "last_modified": None}

    resp = MagicMock()
    resp.status_code = 200
    resp.text = "<html>ok</html>"
    resp.headers = {"ETag": "E2", "Last-Modified": "L2"}

    with patch.object(c.session, "get", return_value=resp):
        res = c._fetch_http(doc)
        assert res["status"] == "ok"
        assert res["html"] == "<html>ok</html>"
        assert res["etag"] == "E2"
        assert res["last_modified"] == "L2"


def test_run_processes_one_document(fake_config):
    fake_col = FakeCollection(initial_docs=[
        {
            "url": "https://example.org/wiki/Test_Page",
            "source": "test_source",
            "html": None,
            "fetched_at": None,
            "etag": None,
            "last_modified": None,
            "content_hash": None,
            "next_crawl_at": 0,
            "status": "new",
            "error": None,
            "_id": "id1",
        }
    ])
    fake_client = make_fake_mongo(fake_col)

    with patch.object(crawler.Crawler, "_load_config", return_value=fake_config), \
         patch.object(crawler, "MongoClient", return_value=fake_client), \
         patch.object(crawler.time, "sleep", return_value=None):

        c = crawler.Crawler("fake.yaml")

        resp = MagicMock()
        resp.status_code = 200
        resp.text = "<html>content</html>"
        resp.headers = {}

        with patch.object(c.session, "get", return_value=resp):
            c.run()

        assert fake_col.estimated_document_count() == 1
        assert fake_col.docs[0]["status"] == "done"
        assert fake_col.docs[0]["html"] == "<html>content</html>"
        assert fake_col.docs[0]["content_hash"] is not None
