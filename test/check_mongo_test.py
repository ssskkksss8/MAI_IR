from pymongo import MongoClient
from datetime import datetime

client = MongoClient("mongodb://localhost:27017")
db = client["transport_corpus_test"]
col = db["documents"]

count = col.count_documents({})
print("Всего документов в тестовой коллекции:", count)

for doc in col.find({}, {"url": 1, "source": 1, "fetched_at": 1, "html": 1}).limit(5):
    fetched_at = doc.get("fetched_at")
    fetched_at_dt = datetime.fromtimestamp(fetched_at) if fetched_at else None
    html_len = len(doc.get("html") or "")
    print("URL:", doc["url"])
    print("  source:", doc.get("source"))
    print("  fetched_at:", fetched_at, "=>", fetched_at_dt)
    print("  html length:", html_len)
    print("-" * 40)
