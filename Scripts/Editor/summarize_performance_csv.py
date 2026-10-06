"""Summarize a stable trailing CSV window; timings are ms, not CPU utilization %."""
import argparse
import csv
import json
import statistics
from pathlib import Path

csv.field_size_limit(100_000_000)
parser = argparse.ArgumentParser()
parser.add_argument("paths", nargs="+")
parser.add_argument("--frames", type=int, default=900)
parser.add_argument("--skip-last", type=int, default=10,
                    help="Exclude final capture/shutdown frames consistently from every run")
args = parser.parse_args()
if args.frames <= 0 or args.skip_last < 0:
    parser.error("frames must be positive and skip-last must be non-negative")

def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, int((len(ordered)-1)*fraction))]

for name in args.paths:
    path = Path(name)
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.reader(handle)
        rows = list(reader)
        # UE appends new columns as stats first appear, then writes the full
        # header again at the end. Earlier frames omit these trailing zeros.
        headers = next(row for row in reversed(rows) if row and row[0] == "EVENTS")
        records = [row + ["0"] * (len(headers)-len(row)) for row in rows
                   if row and row[0] != "EVENTS" and not row[0].startswith("[")]
    wanted = [i for i,h in enumerate(headers) if h in
              ("FrameTime", "GameThreadTime", "RenderThreadTime", "GPU FrameTime", "GPUFrameTime", "GPUTime", "RHIThreadTime", "Memory/PhysicalUsedMB", "GPUMem/LocalUsedMB")
              or (h.startswith("GPU/") and not h.startswith("COUNTS/"))
              or h.startswith("Slate/") or h.startswith("H5UIBindings/")
              or h in ("View/PosX", "View/PosY", "View/PosZ", "DrawPrimitiveCalls")]
    end = len(records) - args.skip_last
    window = records[max(0, end-args.frames):max(0, end)]
    result = {"file": str(path), "total_records": len(records), "trailing_frames": args.frames,
              "skip_last": args.skip_last, "window_start": max(0, end-args.frames),
              "window_end_exclusive": max(0, end), "metrics": {}}
    for index in wanted:
        values = []
        for row in window:
            try:
                values.append(float(row[index]))
            except ValueError:
                pass
        if values:
            result["metrics"][headers[index]] = {"mean": round(statistics.mean(values), 5),
                "p50": round(statistics.median(values), 5), "p95": round(percentile(values, .95), 5),
                "samples": len(values)}
    frame = result["metrics"].get("FrameTime", {}).get("mean", 0)
    if frame > 0:
        result["fps_from_mean_frame_time"] = round(1000/frame, 2)
    output = path.with_suffix(".summary.json")
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
