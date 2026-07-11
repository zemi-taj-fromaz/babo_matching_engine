# Third-party software notices

This project contains or builds against third-party software. Those components
remain under their own licenses; the project's own license does not replace
them. The list below records the provenance of the code used by the benchmark.

## Matching Engine Algorithm Performance Challenge

- Upstream: https://github.com/flash1-dev/matching-engine-benchmark
- Use here: the C-ABI benchmark, workload, adapter, correctness, and audit
  infrastructure under `benchmark/` was adapted from this project.
- License: MIT License.
- Copyright: Copyright (c) 2026 Flash One Technologies LLC.
- Local license text: [`benchmark/LICENSE`](benchmark/LICENSE).

The upstream copyright and permission notice must remain with copies or
substantial portions of the adapted software.

## Liquibook

- Upstream: https://github.com/enewhuis/liquibook
- Use here: vendored comparison implementation under `libs/liquibook/`.
- License: permissive three-clause-style terms stated by the upstream project,
  plus the notice covering its 2018 ease-of-use adjustments.
- Copyright: Copyright (c) 2012, 2013 Object Computing, Inc.; adjustments
  copyright 2018 Dendi Suhubdy.
- Local license text: [`libs/liquibook/LICENSE.txt`](libs/liquibook/LICENSE.txt).

Source redistributions must retain the notices, conditions, and disclaimer.
Binary redistributions must reproduce them in accompanying documentation or
materials. The copyright holders' and contributors' names may not be used to
endorse this project without prior written permission.

## Rigtorp SPSCQueue

- Upstream: https://github.com/rigtorp/SPSCQueue
- Use here: report transport used by the benchmark harness; downloaded by CMake
  `FetchContent` at configure time.
- License: MIT License.
- Copyright: Copyright (c) 2018 Erik Rigtorp.
- Local license text: [`third_party/licenses/SPSCQueue-LICENSE.txt`](third_party/licenses/SPSCQueue-LICENSE.txt).

The upstream copyright and permission notice must remain with copies or
substantial portions of the software.

## Other dependencies

GoogleTest is downloaded for the test binaries through CMake `FetchContent` and
is governed by its own upstream license. The public-domain SHA-256 implementation
used by the harness is identified in its source header and in
[`benchmark/LICENSE`](benchmark/LICENSE).

This notice is an attribution and distribution aid, not legal advice. When a
release includes source or binaries containing third-party code, ship the
applicable full license texts with that release as well.
