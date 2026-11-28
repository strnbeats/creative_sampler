#!/usr/bin/env python3
"""Focused tests for the local Human Listening Evaluation server."""

from __future__ import annotations

import csv
import json
import tempfile
import threading
import unittest
import wave
from http.client import HTTPConnection
from pathlib import Path

from review_server import ReviewError, ReviewHTTPServer, ReviewStore


class ReviewServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "pcm-primary").mkdir()
        (self.root / "runner-primary/dru-01").mkdir(parents=True)
        (self.root / "runner-primary/dru-02").mkdir(parents=True)
        self._write_csv(
            "curated_candidates.csv",
            ["id", "selection_role", "review_status"],
            [
                {"id": "dru-01", "selection_role": "primary", "review_status": "accepted"},
                {"id": "dru-02", "selection_role": "reserve", "review_status": "accepted"},
            ],
        )
        self._write_csv(
            "pcm-primary/manifest.pcm.local.csv",
            ["local_id", "relative_path", "category", "review_status"],
            [
                {"local_id": "dru-01", "relative_path": str(self.root / "pcm-primary/dru-01.wav"), "category": "drums", "review_status": "accepted"},
                {"local_id": "dru-02", "relative_path": str(self.root / "pcm-primary/dru-02.wav"), "category": "drums", "review_status": "accepted"},
            ],
        )
        self._write_wav(self.root / "pcm-primary/dru-01.wav")
        self._write_wav(self.root / "pcm-primary/dru-02.wav")
        result = {
            "sample": {"frames": 4, "sampleRateHz": 8000, "channels": 1},
            "analysis": {"beatGrid": {"reliability": "unreliable"}},
            "plans": {name: self._plan() for name in ("transient", "hybrid")},
        }
        (self.root / "runner-primary/dru-01/result.json").write_text(json.dumps(result), encoding="utf-8")
        original_fields = ["local_id", "category", "review_status", "transient_rating_1_to_5", "hybrid_rating_1_to_5", "comments", "reviewer", "custom_legacy"]
        original_rows = [
            {"local_id": "dru-01", "category": "drums", "review_status": "accepted", "comments": "keep this note", "reviewer": "human", "custom_legacy": "retain"},
            {"local_id": "dru-02", "category": "drums", "review_status": "accepted", "comments": "reserve note", "reviewer": "human", "custom_legacy": "reserve"},
        ]
        self._write_csv("runner-primary/human_review.csv", original_fields, original_rows)
        self.review_path = self.root / "runner-primary/human_review.csv"
        self.store = ReviewStore(self.root)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_csv(self, relative: str, fields: list[str], rows: list[dict[str, str]]) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)

    @staticmethod
    def _write_wav(path: Path) -> None:
        with wave.open(str(path), "wb") as stream:
            stream.setnchannels(1)
            stream.setsampwidth(2)
            stream.setframerate(8000)
            stream.writeframes(b"\x00\x00" * 4)

    @staticmethod
    def _plan() -> dict[str, object]:
        return {
            "valid": True,
            "coverageComplete": True,
            "fallback": "none",
            "gridDisposition": "transientFallback",
            "gridFallback": "unreliable",
            "boundaries": [{"frame": 0}, {"frame": 2}, {"frame": 4}],
            "slices": [
                {"startFrame": 0, "endFrame": 2},
                {"startFrame": 2, "endFrame": 4},
            ],
        }

    @staticmethod
    def _evaluation(rating: int = 4) -> dict[str, object]:
        values = {
            "boundary_quality": rating,
            "attack_preservation": 4,
            "tail_preservation": 3,
            "musical_usability": 4,
            "overall_quality": rating,
            "verdict": "Accept",
            "comments": "checked locally",
        }
        return {"id": "dru-01", "strategies": {"transient": values, "hybrid": values}, "preferredStrategy": "Equal"}

    def test_queue_contains_only_accepted_primary_and_reports_exact_frames(self) -> None:
        samples = self.store.list_samples()
        self.assertEqual([sample["id"] for sample in samples], ["dru-01"])
        self.assertEqual(samples[0]["plans"]["transient"]["boundaryFrames"], [0, 2, 4])
        self.assertEqual(samples[0]["plans"]["hybrid"]["slices"][1]["startFrame"], 2)
        self.assertTrue(samples[0]["comparison"]["identical"])
        with self.assertRaises((ReviewError, FileNotFoundError)):
            self.store.audio_path("dru-02")
        with self.assertRaises(ReviewError):
            self.store.audio_path("../../dru-01")

    def test_saving_extends_csv_without_losing_legacy_data_and_reloads(self) -> None:
        saved = self.store.save_review(self._evaluation())
        self.assertEqual(saved["status"], "reviewed")
        fields, rows = self._read_csv()
        self.assertIn("transient_boundary_quality_1_to_5", fields)
        self.assertIn("hybrid_comments", fields)
        self.assertEqual(rows["dru-01"]["comments"], "keep this note")
        self.assertEqual(rows["dru-01"]["custom_legacy"], "retain")
        self.assertEqual(rows["dru-02"]["custom_legacy"], "reserve")
        self.assertEqual(self.store.list_samples()[0]["review"]["strategies"]["transient"]["overall_quality"], "4")

    def test_invalid_rating_does_not_modify_existing_csv(self) -> None:
        before = self.review_path.read_bytes()
        payload = self._evaluation(rating=6)
        with self.assertRaises(ReviewError):
            self.store.save_review(payload)
        self.assertEqual(self.review_path.read_bytes(), before)

    def test_http_endpoints_serve_pcm_and_save_review(self) -> None:
        server = ReviewHTTPServer(("127.0.0.1", 0), self.store)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            connection.request("GET", "/api/samples")
            response = connection.getresponse()
            payload = json.loads(response.read())
            self.assertEqual(response.status, 200)
            self.assertEqual([sample["id"] for sample in payload["samples"]], ["dru-01"])
            connection.request("GET", "/api/audio/dru-01")
            audio = connection.getresponse()
            self.assertEqual(audio.status, 200)
            self.assertTrue(audio.read().startswith(b"RIFF"))
            body = json.dumps(self._evaluation()).encode("utf-8")
            connection.request(
                "POST",
                "/api/review",
                body=body,
                headers={"Content-Type": "application/json", "Origin": f"http://127.0.0.1:{server.server_port}"},
            )
            saved = connection.getresponse()
            self.assertEqual(saved.status, 200)
            self.assertTrue(json.loads(saved.read())["saved"])
            connection.request("GET", "/api/audio/../../dru-01")
            self.assertEqual(connection.getresponse().status, 422)
            connection.close()
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=3)

    def _read_csv(self) -> tuple[list[str], dict[str, dict[str, str]]]:
        with self.review_path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            return list(reader.fieldnames or []), {row["local_id"]: row for row in reader}


if __name__ == "__main__":
    unittest.main()
