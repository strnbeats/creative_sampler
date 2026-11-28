#!/usr/bin/env python3
"""Local-only listening review player for Auto Slicing Runner results."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RESULTS_ROOT = (
    REPO_ROOT / "tests/fixtures/auto_slicing/results/corpus-curation"
).resolve()
UI_FILE = Path(__file__).with_name("review_player.html")
SAMPLE_ID = re.compile(r"^(?:dru|mel|voc)-\d{2}$")
RATING_FIELDS = (
    "boundary_quality",
    "attack_preservation",
    "tail_preservation",
    "musical_usability",
    "overall_quality",
)
STRATEGIES = ("transient", "hybrid")
VERDICTS = ("", "Accept", "Needs Improvement", "Reject")
PREFERENCES = ("", "Transient", "Hybrid", "Equal", "Neither")
MAX_REQUEST_BYTES = 64 * 1024


class ReviewError(Exception):
    """A user-correctable validation or local corpus error."""


def _csv_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    if not path.is_file():
        raise ReviewError(f"Required local file is missing: {path.name}")
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ReviewError(f"CSV has no header: {path.name}")
        return list(reader.fieldnames), list(reader)


def _safe_id(value: str) -> str:
    if not SAMPLE_ID.fullmatch(value):
        raise ReviewError("Unknown sample ID.")
    return value


def _validate_plan(plan: dict[str, Any], frames: int) -> dict[str, Any]:
    slices = plan.get("slices")
    if not plan.get("valid") or not plan.get("coverageComplete") or not isinstance(slices, list) or not slices:
        raise ReviewError("The plan is missing, invalid, or does not cover the source.")
    checked: list[dict[str, Any]] = []
    next_frame = 0
    for number, item in enumerate(slices, 1):
        try:
            start = int(item["startFrame"])
            end = int(item["endFrame"])
        except (KeyError, TypeError, ValueError) as error:
            raise ReviewError("A slice has invalid frame boundaries.") from error
        if start != next_frame or start < 0 or end <= start or end > frames:
            raise ReviewError("Slice boundaries contain a gap, overlap, or out-of-range frame.")
        checked.append({"number": number, "startFrame": start, "endFrame": end, "durationFrames": end - start})
        next_frame = end
    if next_frame != frames:
        raise ReviewError("Slice boundaries do not end at the source frame count.")

    boundaries = plan.get("boundaries", [])
    boundary_frames: list[int] = []
    for boundary in boundaries:
        frame = boundary.get("frame") if isinstance(boundary, dict) else boundary
        if not isinstance(frame, int) or frame < 0 or frame > frames:
            raise ReviewError("The plan contains an invalid boundary position.")
        boundary_frames.append(frame)
    expected_boundaries = [0] + [item["endFrame"] for item in checked]
    if boundary_frames != expected_boundaries:
        raise ReviewError("Plan boundary frames do not match the slice intervals.")
    return {
        "valid": True,
        "sliceCount": len(checked),
        "slices": checked,
        "boundaryFrames": boundary_frames,
        "fallback": str(plan.get("fallback", "none")),
        "gridDisposition": str(plan.get("gridDisposition", "unknown")),
        "gridFallback": str(plan.get("gridFallback", "none")),
        "gridSupportedEvents": int(plan.get("gridSupportedEvents", 0)),
        "retainedCandidates": int(plan.get("retainedCandidates", 0)),
        "diagnostics": plan.get("diagnostics") if isinstance(plan.get("diagnostics"), dict) else {},
    }


def _evaluation_state(row: dict[str, str]) -> str:
    any_data = bool(row.get("preferred_strategy", "").strip())
    complete_strategies = 0
    for strategy in STRATEGIES:
        prefix = f"{strategy}_"
        filled_ratings = [row.get(prefix + field + "_1_to_5", "").strip() for field in RATING_FIELDS]
        if not filled_ratings[-1]:
            filled_ratings[-1] = row.get(f"{strategy}_rating_1_to_5", "").strip()
        filled = any(filled_ratings)
        filled = filled or bool(row.get(prefix + "verdict", "").strip())
        filled = filled or bool(row.get(prefix + "comments", "").strip())
        any_data = any_data or filled
        if row.get(prefix + "overall_quality_1_to_5", "").strip() and row.get(prefix + "verdict", "").strip():
            complete_strategies += 1
    if complete_strategies == 2 and row.get("preferred_strategy", "").strip():
        return "reviewed"
    return "in_progress" if any_data else "unreviewed"


class ReviewStore:
    """Reads only known files below one results directory and saves reviews atomically."""

    REVIEW_COLUMNS = [
        "transient_boundary_quality_1_to_5",
        "transient_attack_preservation_1_to_5",
        "transient_tail_preservation_1_to_5",
        "transient_musical_usability_1_to_5",
        "transient_overall_quality_1_to_5",
        "transient_verdict",
        "transient_comments",
        "hybrid_boundary_quality_1_to_5",
        "hybrid_attack_preservation_1_to_5",
        "hybrid_tail_preservation_1_to_5",
        "hybrid_musical_usability_1_to_5",
        "hybrid_overall_quality_1_to_5",
        "hybrid_verdict",
        "hybrid_comments",
        "preferred_strategy",
        "evaluation_status",
    ]

    def __init__(self, results_root: Path = DEFAULT_RESULTS_ROOT):
        self.root = results_root.resolve()
        self.manifest_path = self.root / "pcm-primary/manifest.pcm.local.csv"
        self.candidates_path = self.root / "curated_candidates.csv"
        self.review_path = self.root / "runner-primary/human_review.csv"
        self._lock = threading.Lock()

    def _queue(self) -> list[dict[str, str]]:
        manifest_fields, manifest_rows = _csv_rows(self.manifest_path)
        candidate_fields, candidate_rows = _csv_rows(self.candidates_path)
        del manifest_fields, candidate_fields
        accepted = {
            row["id"]: row
            for row in candidate_rows
            if row.get("selection_role") == "primary" and row.get("review_status") == "accepted"
        }
        queue: list[dict[str, str]] = []
        seen: set[str] = set()
        for row in manifest_rows:
            sample_id = _safe_id(row.get("local_id", ""))
            candidate = accepted.get(sample_id)
            if candidate is None or sample_id in seen:
                continue
            if row.get("review_status") != "accepted":
                continue
            expected = (self.root / "pcm-primary" / f"{sample_id}.wav").resolve()
            supplied = Path(row.get("relative_path", "")).resolve()
            if supplied != expected or not expected.is_file():
                raise ReviewError(f"PCM source for {sample_id} is missing or outside the expected folder.")
            seen.add(sample_id)
            queue.append({
                "id": sample_id,
                "category": row.get("category", ""),
                "filename": candidate.get("filename") or expected.name,
                "sourceStatus": row.get("review_status", "accepted"),
                "path": str(expected),
            })
        return queue

    def _review_rows(self) -> tuple[list[str], dict[str, dict[str, str]]]:
        fields, rows = _csv_rows(self.review_path)
        by_id: dict[str, dict[str, str]] = {}
        for row in rows:
            sample_id = row.get("local_id", "")
            if sample_id:
                if sample_id in by_id:
                    raise ReviewError(f"Duplicate review CSV row for {sample_id}.")
                by_id[sample_id] = row
        return fields, by_id

    def _load_case(self, queue_row: dict[str, str], reviews: dict[str, dict[str, str]]) -> dict[str, Any]:
        sample_id = queue_row["id"]
        wav_path = Path(queue_row["path"])
        try:
            with wave_open_read(wav_path) as wav:
                frames = wav.getnframes()
                sample_rate = wav.getframerate()
                channels = wav.getnchannels()
            result_path = self.root / "runner-primary" / sample_id / "result.json"
            if not result_path.is_file():
                raise ReviewError("Runner result JSON is missing.")
            data = json.loads(result_path.read_text(encoding="utf-8"))
            sample_meta = data.get("sample", {})
            if (
                int(sample_meta.get("frames", -1)) != frames
                or int(sample_meta.get("sampleRateHz", -1)) != sample_rate
                or int(sample_meta.get("channels", -1)) != channels
            ):
                raise ReviewError("WAV geometry does not match the Runner JSON.")
            plans_raw = data.get("plans", {})
            plans = {
                strategy: _validate_plan(plans_raw.get(strategy, {}), frames)
                for strategy in STRATEGIES
            }
            transient = set(plans["transient"]["boundaryFrames"][1:-1])
            hybrid = set(plans["hybrid"]["boundaryFrames"][1:-1])
            matches = len(transient & hybrid)
            differences = len(transient ^ hybrid)
            return {
                "id": sample_id,
                "category": queue_row["category"],
                "filename": queue_row["filename"],
                "pcmFilename": wav_path.name,
                "sampleRate": sample_rate,
                "channels": channels,
                "frames": frames,
                "durationSeconds": frames / sample_rate,
                "plans": plans,
                "comparison": {
                    "matchingInternalBoundaries": matches,
                    "differentInternalBoundaries": differences,
                    "identical": differences == 0,
                },
                "analysis": data.get("analysis", {}),
                "review": self._review_payload(reviews.get(sample_id, {})),
                "error": "",
            }
        except Exception as error:  # keep one bad local asset from hiding the rest
            return {
                "id": sample_id,
                "category": queue_row["category"],
                "filename": wav_path.name,
                "pcmFilename": wav_path.name,
                "error": str(error),
                "review": self._review_payload(reviews.get(sample_id, {})),
            }

    @staticmethod
    def _review_payload(row: dict[str, str]) -> dict[str, Any]:
        strategy_values: dict[str, dict[str, Any]] = {}
        for strategy in STRATEGIES:
            values: dict[str, Any] = {}
            for field in RATING_FIELDS:
                key = f"{strategy}_{field}_1_to_5"
                value = row.get(key, "").strip()
                if not value and field == "overall_quality":
                    value = row.get(f"{strategy}_rating_1_to_5", "").strip()
                values[field] = value
            values["verdict"] = row.get(f"{strategy}_verdict", "").strip()
            values["comments"] = row.get(f"{strategy}_comments", "").strip()
            strategy_values[strategy] = values
        return {
            "strategies": strategy_values,
            "preferredStrategy": row.get("preferred_strategy", "").strip(),
            "status": _evaluation_state(row),
            "legacyComments": row.get("comments", "").strip(),
        }

    def list_samples(self) -> list[dict[str, Any]]:
        _, reviews = self._review_rows()
        return [self._load_case(row, reviews) for row in self._queue()]

    def audio_path(self, sample_id: str) -> Path:
        sample_id = _safe_id(sample_id)
        queue_ids = {row["id"] for row in self._queue()}
        if sample_id not in queue_ids:
            raise FileNotFoundError("Sample is not in the accepted primary queue.")
        path = (self.root / "pcm-primary" / f"{sample_id}.wav").resolve()
        if path.parent != (self.root / "pcm-primary").resolve() or not path.is_file():
            raise FileNotFoundError("PCM file is not available.")
        return path

    @staticmethod
    def _clean_rating(value: Any, label: str) -> str:
        if value in (None, ""):
            return ""
        if isinstance(value, bool) or not isinstance(value, (int, str)):
            raise ReviewError(f"{label} must be an integer from 1 to 5 or blank.")
        try:
            number = int(value)
        except (TypeError, ValueError) as error:
            raise ReviewError(f"{label} must be an integer from 1 to 5 or blank.") from error
        if str(number) != str(value) or number < 1 or number > 5:
            raise ReviewError(f"{label} must be an integer from 1 to 5 or blank.")
        return str(number)

    @staticmethod
    def _clean_verdict(value: Any) -> str:
        if value not in VERDICTS:
            raise ReviewError("Verdict must be Accept, Needs Improvement, Reject, or blank.")
        return value

    @staticmethod
    def _clean_comments(value: Any) -> str:
        if not isinstance(value, str) or len(value) > 5000:
            raise ReviewError("Comments must be text up to 5,000 characters.")
        return value

    def save_review(self, payload: dict[str, Any]) -> dict[str, Any]:
        sample_id = _safe_id(str(payload.get("id", "")))
        if sample_id not in {row["id"] for row in self._queue()}:
            raise ReviewError("Only accepted primary candidates can be reviewed here.")
        ratings = payload.get("strategies")
        if not isinstance(ratings, dict) or any(key not in STRATEGIES for key in ratings):
            raise ReviewError("Review must include Transient and/or Hybrid fields.")
        preference = payload.get("preferredStrategy", "")
        if preference not in PREFERENCES:
            raise ReviewError("Preference must be Transient, Hybrid, Equal, Neither, or blank.")

        clean: dict[str, dict[str, str]] = {}
        for strategy, values in ratings.items():
            if not isinstance(values, dict):
                raise ReviewError(f"{strategy} review must be an object.")
            if any(key not in (*RATING_FIELDS, "verdict", "comments") for key in values):
                raise ReviewError("Review contains an unknown field.")
            clean[strategy] = {
                **{
                    field: self._clean_rating(values.get(field, ""), field)
                    for field in RATING_FIELDS
                },
                "verdict": self._clean_verdict(values.get("verdict", "")),
                "comments": self._clean_comments(values.get("comments", "")),
            }

        with self._lock:
            fields, rows = _csv_rows(self.review_path)
            if "local_id" not in fields:
                raise ReviewError("Review CSV is missing the local_id column.")
            columns = list(fields)
            for column in ("preferred_strategy", "evaluation_status", *self.REVIEW_COLUMNS):
                if column not in columns:
                    columns.append(column)
            target: dict[str, str] | None = None
            ids_seen: set[str] = set()
            normalized: list[dict[str, str]] = []
            for old in rows:
                row = {column: old.get(column, "") or "" for column in columns}
                row_id = row.get("local_id", "")
                if row_id == sample_id:
                    if target is not None:
                        raise ReviewError(f"Duplicate review CSV row for {sample_id}.")
                    target = row
                if row_id:
                    if row_id in ids_seen:
                        raise ReviewError(f"Duplicate review CSV row for {row_id}.")
                    ids_seen.add(row_id)
                normalized.append(row)
            if target is None:
                raise ReviewError(f"Review CSV has no row for {sample_id}; no data was changed.")

            for strategy, values in clean.items():
                for field in RATING_FIELDS:
                    target[f"{strategy}_{field}_1_to_5"] = values[field]
                target[f"{strategy}_verdict"] = values["verdict"]
                target[f"{strategy}_comments"] = values["comments"]
            if "preferredStrategy" in payload:
                target["preferred_strategy"] = preference
            target["evaluation_status"] = _evaluation_state(target)
            self._atomic_write(columns, normalized)
            return self._review_payload(target)

    def _atomic_write(self, columns: list[str], rows: list[dict[str, str]]) -> None:
        self.review_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_name = ""
        try:
            with tempfile.NamedTemporaryFile(
                "w",
                newline="",
                encoding="utf-8",
                dir=self.review_path.parent,
                prefix=f".{self.review_path.name}.",
                suffix=".tmp",
                delete=False,
            ) as stream:
                temporary_name = stream.name
                writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
                writer.writeheader()
                writer.writerows(rows)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary_name, self.review_path)
        finally:
            if temporary_name and os.path.exists(temporary_name):
                os.unlink(temporary_name)


def wave_open_read(path: Path):
    import wave

    return wave.open(str(path), "rb")


class ReviewHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], store: ReviewStore):
        self.store = store
        super().__init__(address, make_handler(store))


def make_handler(store: ReviewStore):
    class Handler(BaseHTTPRequestHandler):
        server_version = "AutoSlicingReview/1.0"

        def log_message(self, fmt: str, *args: Any) -> None:
            print(f"[review-player] {self.address_string()} {fmt % args}")

        def _host_ok(self) -> bool:
            return self.headers.get("Host", "").split(":", 1)[0] == "127.0.0.1"

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'self'; connect-src 'self'; media-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'none'")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, status: int, data: Any) -> None:
            encoded = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self._send(status, encoded, "application/json; charset=utf-8")

        def _error(self, status: int, message: str) -> None:
            self._json(status, {"error": message})

        def do_GET(self) -> None:
            if not self._host_ok():
                self._error(403, "Only loopback requests are permitted.")
                return
            path = urlsplit(self.path).path
            try:
                if path == "/":
                    self._send(200, UI_FILE.read_bytes(), "text/html; charset=utf-8")
                elif path == "/api/samples":
                    self._json(200, {"samples": store.list_samples()})
                elif path.startswith("/api/audio/"):
                    sample_id = path[len("/api/audio/"):]
                    audio_path = store.audio_path(sample_id)
                    self._send(200, audio_path.read_bytes(), "audio/wav")
                else:
                    self._error(404, "Not found.")
            except FileNotFoundError as error:
                self._error(404, str(error))
            except ReviewError as error:
                self._error(422, str(error))
            except (OSError, json.JSONDecodeError) as error:
                self._error(500, f"Could not load this local result: {error}")

        def do_POST(self) -> None:
            if not self._host_ok():
                self._error(403, "Only loopback requests are permitted.")
                return
            expected_origin = f"http://127.0.0.1:{self.server.server_port}"
            origin = self.headers.get("Origin")
            if origin and origin != expected_origin:
                self._error(403, "Cross-origin requests are not accepted.")
                return
            if urlsplit(self.path).path != "/api/review":
                self._error(404, "Not found.")
                return
            try:
                length = int(self.headers.get("Content-Length", "-1"))
                if length < 0 or length > MAX_REQUEST_BYTES:
                    raise ReviewError("Request body is missing or too large.")
                if self.headers.get_content_type() != "application/json":
                    raise ReviewError("Review requests must use application/json.")
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
                if not isinstance(payload, dict):
                    raise ReviewError("Review body must be a JSON object.")
                review = store.save_review(payload)
                self._json(200, {"saved": True, "review": review})
            except ReviewError as error:
                self._error(400, str(error))
            except (ValueError, UnicodeDecodeError, json.JSONDecodeError):
                self._error(400, "Request body is not valid JSON.")
            except OSError as error:
                self._error(500, f"Could not save the local review: {error}")

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 0 < args.port < 65536:
        parser.error("--port must be between 1 and 65535")
    store = ReviewStore()
    # Fail early with a clear message when results are not prepared.
    queue = store._queue()
    if not queue:
        raise SystemExit("No accepted primary PCM manifest rows were found under results/corpus-curation.")
    server = ReviewHTTPServer(("127.0.0.1", args.port), store)
    print(f"Human Listening Evaluation Player: http://127.0.0.1:{server.server_port}")
    print(f"Loaded {len(queue)} accepted primary candidates. Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping local review server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
