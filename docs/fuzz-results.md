# Fuzzing results

The ITCH parser is the only component that touches untrusted bytes, so it is
fuzzed directly.

## Harness

`bench/fuzz_itch.cpp` is a libFuzzer target whose body is a single
`itch::parse(span)` over the mutator-supplied bytes, built with
`-fsanitize=fuzzer,address,undefined`. The corpus is seeded from coherent synth
messages via `seed_fuzz_corpus`, giving the mutator valid structures to start
from.

```sh
cmake -S . -B build-fuzz -DLAZERBOOK_BUILD_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz -j
mkdir -p corpus
./build-fuzz/bench/seed_fuzz_corpus corpus 2048
./build-fuzz/bench/fuzz_itch corpus
```

## Run of record (reference platform)

Recorded on the development reference platform (Linux, clang-18, which ships the
libFuzzer runtime — AppleClang does not, so the libFuzzer target builds but cannot
be linked/run on macOS):

| Metric | Value |
|---|---|
| Platform | clang-18 + libFuzzer |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer |
| Duration | 121 s |
| Executions | 385,000,000 |
| Crashes | 0 |
| Sanitizer hits | 0 |

## Local cross-check (any platform)

Where libFuzzer is unavailable, the parser's totality is cross-checked with a
plain random-input loop under ASan + UBSan: 20,000,000 random byte spans of
length 0–64 fed to `itch::parse`. Result: ~941k valid parses, ~19.06M clean
rejects, **0 crashes, 0 sanitizer hits**.

The parser is total: every input either returns a `Message` or a `ParseError`
(`BufferTooShort` / `UnknownMessageType`), never a crash, out-of-bounds read, or
undefined-behaviour trap. Length is validated against `expected_length()` before
any field is decoded, and recognised-but-shallow types stash a bounded copy of the
body into the `Unknown` arm.

## Reproduce

Run for at least 60 s (acceptance criterion) with the seeded corpus; CI runs a
short smoke. Any future finding should be added to the corpus as a regression
seed.
