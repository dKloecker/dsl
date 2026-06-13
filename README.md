# DSL

Collection of Low-Level Components and Utilities, made for learning purposes which I use across my CPP projects.

## Layout

```
dsl/
├── dsl/                    library headers
│   ├── core/
│   │   ├── concepts/       type concepts
│   │   ├── memory/         fixed-size and multi-size pmr pool resources
│   │   ├── spsc_queue/     lock-free SPSC ring buffer
│   │   └── utils/          small helpers
│   ├── logging/            async, lock-free logger
├── tests/                  GoogleTest suites
└── benchmarks/             Google Benchmark suites
```

## Building

Requires CMake 3.26+, a C++23 compiler, and vcpkg for dependencies
(`gtest`, `benchmark`, `boost-lockfree`).

```sh
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

Toggle tests/benchmarks with `-DDSL_BUILD_TESTING=OFF` / `-DDSL_BUILD_BENCHMARKS=OFF`.
