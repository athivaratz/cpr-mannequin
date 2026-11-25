#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

BIN=.simbin/cpr_sim
if [ ! -x "$BIN" ]; then
  bash tools/sim/build.sh
fi

mkdir -p recordings web/demo

run() {
  "$BIN" "$@"
}

# Nine waveform scenarios at the HX711 high-speed rate (80 SPS).
run --scenario good      --seconds 40 --sps 80 --mode free  --out recordings/good.jsonl
run --scenario shallow   --seconds 40 --sps 80 --mode free  --out recordings/shallow.jsonl
run --scenario deep      --seconds 40 --sps 80 --mode free  --out recordings/deep.jsonl
run --scenario fast      --seconds 40 --sps 80 --mode free  --out recordings/fast.jsonl
run --scenario slow      --seconds 40 --sps 80 --mode free  --out recordings/slow.jsonl
run --scenario norecoil  --seconds 40 --sps 80 --mode free  --out recordings/norecoil.jsonl
run --scenario wronghand --seconds 40 --sps 80 --mode free  --out recordings/wronghand.jsonl
run --scenario noisy     --seconds 40 --sps 80 --mode free  --out recordings/noisy.jsonl
run --scenario cycle     --seconds 90 --sps 80 --mode cycle --out recordings/cycle.jsonl

# Extras used by the dashboard and the dev server.
run --scenario good --seconds 125 --sps 80 --mode test --out recordings/test2min.jsonl
run --scenario good --seconds 40  --sps 10 --no-tof --esp32  --out recordings/kgonly_good.jsonl
run --scenario good --seconds 40  --sps 10                   --out recordings/lowsps_good.jsonl

rm -f web/demo/*.jsonl
cp recordings/good.jsonl recordings/shallow.jsonl recordings/cycle.jsonl \
   recordings/test2min.jsonl recordings/kgonly_good.jsonl web/demo/

python3 - <<'PY'
import json
import math
import os

ROOT = os.getcwd()
REC = os.path.join(ROOT, "recordings")
WEB = os.path.join(ROOT, "web", "demo")

TITLES = {
    "good": "กดถูกต้อง",
    "shallow": "กดตื้นไป",
    "deep": "กดลึกเกินไป",
    "fast": "จังหวะเร็วเกินไป",
    "slow": "จังหวะช้าเกินไป",
    "norecoil": "ปล่อยมือไม่สุด",
    "wronghand": "วางมือผิดตำแหน่ง",
    "noisy": "สัญญาณมีเสียงรบกวน",
    "cycle": "รอบ 30:2",
    "test2min": "ทดสอบ 2 นาที",
    "kgonly_good": "กดถูกต้อง (เครื่องชั่ง)",
    "lowsps_good": "กดถูกต้อง (10 SPS)",
}

ORDER = ["good", "shallow", "deep", "fast", "slow", "norecoil",
         "wronghand", "noisy", "cycle", "test2min", "kgonly_good",
         "lowsps_good"]

DEMO = ["good", "shallow", "cycle", "test2min", "kgonly_good"]

entries = [{"id": name, "title": TITLES[name], "file": name + ".jsonl"}
           for name in ORDER]
with open(os.path.join(REC, "manifest.json"), "w", encoding="utf-8") as handle:
    json.dump(entries, handle, ensure_ascii=False, indent=2)
    handle.write("\n")

with open(os.path.join(WEB, "manifest.json"), "w", encoding="utf-8") as handle:
    json.dump([entry for entry in entries if entry["id"] in DEMO], handle,
              ensure_ascii=False, indent=2)
    handle.write("\n")

sums = {}
last_sps = {}
for name in ORDER:
    path = os.path.join(REC, name + ".jsonl")
    with open(path, "r", encoding="utf-8") as handle:
        lines = [line for line in handle.read().splitlines() if line]
    if not lines or json.loads(lines[0]).get("ty") != "cfg":
        raise SystemExit("FAIL %s: first line is not cfg" % name)
    if json.loads(lines[-1]).get("ty") != "sum":
        raise SystemExit("FAIL %s: last line is not sum" % name)
    sums[name] = json.loads(lines[-1])
    per_sum = sum(1 for line in lines if json.loads(line).get("ty") == "sum")
    if per_sum != 1:
        raise SystemExit("FAIL %s: %d sum lines" % (name, per_sum))
    for line in reversed(lines):
        obj = json.loads(line)
        if obj.get("ty") == "f":
            last_sps[name] = obj.get("sps", 0)
            break

def ratio(part, whole):
    return (float(part) / whole) if whole else 0.0

checks = []

def check(name, ok, detail):
    checks.append((name, ok, detail))

s = sums["good"]
check("good", ratio(s["dg"], s["n"]) >= 0.80, "dg/n %.2f" % ratio(s["dg"], s["n"]))
s = sums["shallow"]
check("shallow", ratio(s["dsh"], s["n"]) >= 0.80, "dsh/n %.2f" % ratio(s["dsh"], s["n"]))
s = sums["deep"]
check("deep", ratio(s["dd"], s["n"]) >= 0.80, "dd/n %.2f" % ratio(s["dd"], s["n"]))
s = sums["fast"]
check("fast", ratio(s["rf"], s["rr"]) >= 0.50, "rf/rr %.2f" % ratio(s["rf"], s["rr"]))
s = sums["slow"]
check("slow", ratio(s["rs"], s["rr"]) >= 0.50, "rs/rr %.2f" % ratio(s["rs"], s["rr"]))
s = sums["norecoil"]
check("norecoil", ratio(s["rc"], s["n"]) <= 0.10, "rc/n %.2f" % ratio(s["rc"], s["n"]))
s = sums["wronghand"]
check("wronghand", 0.15 <= ratio(s["hd"], s["n"]) <= 0.45,
      "hd/n %.2f" % ratio(s["hd"], s["n"]))
s = sums["noisy"]
expected = int(math.floor(40 * 1000 * 110 / 60000.0))
check("noisy", abs(s["n"] - expected) <= 2,
      "n %d expected %d" % (s["n"], expected))
s = sums["cycle"]
check("cycle", s["cy"] >= 4, "cy %d" % s["cy"])
s = sums["test2min"]
check("test2min", s["mode"] == 2 and 118 <= s["dur"] <= 121 and s["n"] > 0,
      "mode %d dur %d n %d" % (s["mode"], s["dur"], s["n"]))
s = sums["kgonly_good"]
# Recorded at 10 SPS with the ESP32 settings (no median, no smoothing), like
# the real board with a stock HX711.
check("kgonly_good", ratio(s["dg"], s["n"]) >= 0.80 and ratio(s["rc"], s["n"]) >= 0.80,
      "dg/n %.2f rc/n %.2f" % (ratio(s["dg"], s["n"]), ratio(s["rc"], s["n"])))
s = sums["lowsps_good"]
check("lowsps_good", last_sps["lowsps_good"] <= 15 and s["n"] > 0,
      "last sps %d n %d" % (last_sps["lowsps_good"], s["n"]))

print("%-12s %4s %4s %4s %4s %4s %4s %4s %5s" %
      ("recording", "n", "dg", "dsh", "dd", "rg", "rc", "hd", "score"))
for name in ORDER:
    s = sums[name]
    print("%-12s %4d %4d %4d %4d %4d %4d %4d %5d" %
          (name, s["n"], s["dg"], s["dsh"], s["dd"], s["rg"], s["rc"],
           s["hd"], s["score"]))

failed = [item for item in checks if not item[1]]
if failed:
    for name, _, detail in failed:
        print("FAIL %-12s %s" % (name, detail))
    raise SystemExit(1)
PY
