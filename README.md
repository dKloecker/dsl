# dsl

Collection of low-level C++ components and utilities, made for learning purposes,
which I reuse across my C++ projects.

```
src/
├── dsl/            <-- Standard Libarary / Foundational Components
│   ├── dslu/       <-- Utilities
│   └── dslpmr/     <-- PMR Resources
├── dcl/            <-- Concurrency Library
│   ├── dclc/       <-- Concurrency Containers
│   └── dclu/       <-- Concurrency Utilities
└── dal/            <-- Application Library
    └── dall/       <-- Application Logger

tests/{dsl,dcl,dal}/        one GoogleTest executable per library
benchmarks/{dsl,dcl,dal}/   one Google Benchmark executable per library
```
## Building

```sh
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

Toggle with `-DDSL_BUILD_TESTING=OFF` / `-DDSL_BUILD_BENCHMARKS=OFF`.

Benchmarks are one executable per library.

```sh
./build/benchmarks/dcl_benchmarks --benchmark_filter=spsc
```
