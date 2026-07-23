# Third-party software notices

This project contains or builds against third-party software. Those components
remain under their own licenses. This file records their provenance and points
to the applicable license text.

## Matching Engine Algorithm Performance Challenge

- Upstream: https://github.com/flash1-dev/matching-engine-benchmark
- Use here: the C-ABI benchmark, workload, adapter, correctness, and audit
  infrastructure under `benchmark/` was adapted from this project.
- License: MIT License.
- Copyright: Copyright (c) 2026 Flash One Technologies LLC.
- License text: [`benchmark/LICENSE`](benchmark/LICENSE).

## Liquibook

- Upstream: https://github.com/enewhuis/liquibook
- Use here: vendored comparison implementation under `libs/liquibook/`.
- License: permissive three-clause-style terms stated by the upstream project,
  plus the notice covering its 2018 ease-of-use adjustments.
- Copyright: Copyright (c) 2012, 2013 Object Computing, Inc.; adjustments
  copyright 2018 Dendi Suhubdy.
- License text: [`libs/liquibook/LICENSE.txt`](libs/liquibook/LICENSE.txt).

## Rigtorp SPSCQueue

- Upstream: https://github.com/rigtorp/SPSCQueue
- Use here: report transport used by the benchmark harness.
- Distribution: downloaded by CMake `FetchContent` at configure time.
- License: MIT License.
- Copyright: Copyright (c) 2018 Erik Rigtorp.

The downloaded source contains its upstream `LICENSE` file.

## GoogleTest

- Upstream: https://github.com/google/googletest
- Use here: unit-test framework only.
- Distribution: downloaded by CMake `FetchContent` at configure time.
- License: BSD 3-Clause.
- Copyright: Copyright 2008, Google Inc.

The downloaded source contains its upstream `LICENSE` file.

## SHA-256

The benchmark uses the public-domain SHA-256 implementation in
`benchmark/third_party/sha256.c` and `benchmark/third_party/sha256.h`. Its
public-domain status is stated directly in both source headers and in
[`benchmark/LICENSE`](benchmark/LICENSE).

This notice is an attribution and distribution aid, not legal advice. Binary or
source releases must include any full license texts required by the applicable
third-party licenses.
