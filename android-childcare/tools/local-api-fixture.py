"""Disposable local integration service; no production configuration or credentials."""
from pathlib import Path
import sys
import tempfile
import uvicorn

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "backend"))
from app.records.api import create_record_app

directory = Path(tempfile.mkdtemp(prefix="api-fixture-", dir=root / "android-childcare/.local"))

class FixtureProvider:
    model = "local-test-fixture"
    def generate(self, kind, prompt):
        return f"LOCAL TEST ONLY {kind}: {prompt}"

app = create_record_app(directory / "personal.db", directory / "media",
                        admin_token="fixture-owner", care_token="fixture-care",
                        provider=FixtureProvider(), start_workers=True)

from fastapi import Depends, HTTPException
@app.post("/fixture/reset")
def reset(who=Depends(app.state.identity)):
    if not who.owner:
        raise HTTPException(403)
    with app.state.store.write() as db:
        for table in ("ai_jobs", "receipts", "changes", "records"):
            db.execute("DELETE FROM " + table)
        db.execute("DELETE FROM sqlite_sequence WHERE name='changes'")
        db.execute("UPDATE profiles SET generation=1")
    return {"reset": True}

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8912, access_log=False)
