# Third-party licenses

This directory collects the license and attribution notices for dependencies
used by this repository.

| Component | How it is used | Notice |
|---|---|---|
| GoogleTest | Fetched by CMake for the test targets | [GoogleTest-LICENSE.txt](GoogleTest-LICENSE.txt) |
| Rigtorp SPSCQueue | Fetched by CMake for the benchmark report transport | [SPSCQueue-LICENSE.txt](SPSCQueue-LICENSE.txt) |
| Liquibook | Vendored reference matching engine under `libs/liquibook/` | [Liquibook-LICENSE.txt](Liquibook-LICENSE.txt) |
| Flash One benchmark harness | Vendored benchmark code under `benchmark/` | [Benchmark-Harness-LICENSE.txt](Benchmark-Harness-LICENSE.txt) |
| SHA-256 implementation | Vendored public-domain implementation under `benchmark/third_party/` | [SHA256-NOTICE.txt](SHA256-NOTICE.txt) |

Canonical license files are also retained beside vendored source where provided:

- `libs/liquibook/LICENSE.txt`
- `benchmark/LICENSE`

The copies in this directory make all third-party notices easy to find in one
place. They do not replace or modify the original license terms.
