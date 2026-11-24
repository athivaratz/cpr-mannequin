# Rules for coding agents in this repo

Read `docs/DESIGN.md` before you touch anything. It is the spec. Your job is to
implement exactly what it says for the phase you were given, nothing more.

## Hard rules

1. **Write complete code.** Every function you declare has a real body. No
   stubs, no `// TODO`, no `// ...`, no "rest unchanged", no "implement later",
   no placeholder return values, no fake data where real logic belongs. If you
   edit a file, the file must still be complete afterwards.
2. **Never claim something passes without running it.** Before you say you are
   done, run the commands in "Definition of done" and read their output. Paste
   the last lines of each into your final message. If something fails, fix it
   and run again. If you cannot fix it, say exactly what fails.
3. **Follow the interfaces in DESIGN.md exactly.** Same names, same types, same
   fields. If the spec is wrong or impossible, do not invent a workaround in
   silence: write it in `docs/OPEN_ISSUES.md` (what, why, what you did instead).
4. **Handle errors.** Check return values from sensor begin calls, file I/O,
   buffer sizes and parsing. Bounds-check every buffer write. No unchecked
   `strcpy`/`sprintf`; use sized versions.
5. **`lib/cpr_core` is pure C++.** No Arduino headers, no heap (`new`,
   `malloc`, `std::vector`, `std::string`, `String`), no exceptions, no RTTI,
   no `printf("%f")`. Must build with avr-gcc 7.3 (C++11).
6. **Don't touch** `opencode.json`, `AGENTS.md`, `docs/DESIGN.md`, `.git/`
   config, or anything outside this repository. Don't push, don't add remotes,
   don't create branches, don't print environment variables.
7. **Don't commit.** The reviewer commits after verifying your work.
8. **Numbers come from DESIGN.md §2.** Do not invent clinical values. If you
   need a value that isn't there, use a clearly named constant, mark it
   "engineering value, tunable" in a comment, and list it in OPEN_ISSUES.md.
9. **Comments and docs read like a person wrote them.** Short, plain, useful.
   No "This function elegantly...", no emoji, no marketing words, no comment
   that only repeats the code. User-facing text (dashboard, docs) is Thai.
10. **Keep it small.** No new dependencies beyond the libraries named in
    DESIGN.md. Python tools use the standard library only, except
    `tools/subset_font.py` (fontTools + brotli) and `tools/screenshots.py`
    (playwright).

## Definition of done (run all of these, in this order)

```
pio test -e native
pio run -e esp32dev     # from P2 on
pio run -e nano         # from P3 on
```

All must succeed with no errors and no new warnings in our own code
(`lib/cpr_core`, `lib/cpr_sim`, `src/`). Then, for your own check:

```
grep -rnE "TODO|FIXME|XXX|\.\.\. ?rest|unchanged|placeholder|not implemented" lib src web tools test
```

must print nothing that belongs to your phase.

## Commands

- PlatformIO is `pio` (on PATH).
- Simulator build: `bash tools/sim/build.sh` (from P5).
- Dev server: `python3 tools/devserver.py --port 8080 --recording recordings/good.jsonl --loop` (from P5).

## Final message format

1. Files created or changed (one line each).
2. Output tails of every command in "Definition of done".
3. Anything you could not do, and why.
