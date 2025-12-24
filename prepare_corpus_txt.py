import json
from pathlib import Path

def main():
    out = open("corpus.txt", "w", encoding="utf-8")
    for path in sorted(Path("../corpus_aviation").glob("batch_*.jsonl")):
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except ValueError:
                    continue
                text = obj.get("text", "")
                if not text:
                    continue
                text = text.replace("\n", " ")
                out.write(text)
                out.write("\n")
    out.close()

if __name__ == "__main__":
    main()
