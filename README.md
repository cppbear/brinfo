# BrInfo

[中文说明](./README.zh.md)

BrInfo is a C++/Clang-based toolkit to correlate static control-flow conditions with runtime behavior in tests. It provides:

- Static analysis/instrumentation to normalize and hash conditions (IF/CASE/DEFAULT/LOOP/TRY)
- A lightweight runtime that emits NDJSON events: test, assertion, invocation, and condition
- Google Test integration (listener + optional macro auto-wrap)
- An AST rewriter to auto-wrap function calls in test bodies with `BRINFO_CALL(...)`

End goal: extract per-assertion triples <test prefix, test oracle, condition chain> from your test runs.

## Prerequisites

To use this tool, the project to be analyzed must have a compilation database similar to `compile_commands.json`.

If the project being analyzed is managed by CMake, you can generate this file by adding `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` when configuring CMake.

If the project is not managed by CMake, you can consider using [Bear](https://github.com/rizsotto/Bear) to generate the compilation database.

## CLI: Static analyzer (brinfo)

```
USAGE: brinfo [options] <source>

OPTIONS:

Generic Options:

  --help                      - Display available options (--help-hidden for more)
  --help-list                 - Display list of available options (--help-list-hidden for more)
  --version                   - Display the version of this program

brinfo options:

  -c <string>                 - Specify the class of the function
  --cfg                       - Dump CFG to .dot file
  -f <string>                 - Specify the function to analyze
  -p <string>                 - Build path
  --project=<string>          - Specify the project path

-p <build-path> is used to read a compile command database.

        For example, it can be a CMake build directory in which a file named
        compile_commands.json exists (use -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        CMake option to get this output). When no build path is specified,
        a search for compile_commands.json will be attempted through all
        parent paths of the first input file . See:
        https://clang.llvm.org/docs/HowToSetupToolingForLLVM.html for an
        example of setting up Clang Tooling on a source tree.

<source> specify the path of source file. The path is
        looked up in the compile command database. If the path of a file is
        absolute, it needs to point into CMake's source tree. If the path is
        relative, the current working directory needs to be in the CMake
        source tree and the file must be in a subdirectory of the current
        working directory. "./" prefixes in the relative files will be
        automatically removed, but the rest of a relative path must be a
        suffix of a path in the compile command database.
```

Here is a breakdown of the options:

- `--project` specifies the path to the project.
- `-p` specifies the path to the folder containing `compile_commands.json`.
- `-f` (optional) specifies the function/method to be analyzed.
- `-c` (optional) specifies the class to which the function/method belongs.

If no function/method is specified, all functions/methods in the `<source>` file will be analyzed.

- `--cfg` saves the CFG (Control Flow Graph) of the analyzed function/method as a dot file in the project directory.

### Examples

1. Analyze all functions/methods in a source file of a project.

   ```shell
   brinfo --project path/to/project -p path/to/build/directory path/to/source/file
   ```

2. Analyze a specified function in a source file of a project.

   ```shell
   brinfo --project path/to/project -p path/to/build/directory path/to/source_file -f function_name
   ```

3. Analyze a specified method in a source file of a project, where the method belongs to a specific class.

   ```shell
   brinfo --project path/to/project -p path/to/build/directory path/to/source_file -c class_name -f function_name
   ```

The condition chains analyzed by the tool will be saved as JSON files with the filename format `*_req.json`. These files will be located in the `llm_reqs` folder within the analyzed project's directory.

## Runtime and GTest integration

- Include `brinfo/GTestSupport.h` in your tests to register the listener and to use `BRINFO_CALL(...)` around call sites you want to track.
- To auto-wrap assertion macros so invocations inside assertions are marked `in_oracle`, enable auto-wrap by defining `BRINFO_AUTO_WRAP_GTEST` and including `brinfo/GTestAutoWrap.h` before `<gtest/gtest.h>`.
- Alternatively, use the AST rewriter `brinfo_callwrap` to rewrite your test sources and insert `BRINFO_CALL(...)` automatically (with optional macro-argument wrapping).

See docs/GTestIntegration.md and docs/CallWrapTool.md for details.

## AST rewriter (brinfo_callwrap)

This tool finds calls inside gtest TestBody and wraps them with `BRINFO_CALL(...)`. Flags:
- `--only-tests` (default on): limit to TestBody
- `--allow` regex: filter fully qualified callee names
- `--wrap-macro-args`: also wrap calls inside assertion macro arguments (spelling in main file)
- `--print-structure`, `--structure-all`, `--print-calls`: diagnostics/outline

It also injects, once per modified main file, the header block:

```
#define BRINFO_AUTO_WRAP_GTEST
#include "brinfo/GTestAutoWrap.h"
#include "brinfo/GTestSupport.h"
```

## Build

1. Install the dependencies, including:

   1. C/C++ compilation tools such as `gcc` or `clang`
   2. `CMake`
   3. `Ninja`
   
2. Install development packages:

   ```shell
   sudo apt install -y llvm-17-dev libclang-17-dev clang-tools-17 clang-17 pkg-config xxhash libxxhash-dev
   ```

3. Config CMake

   ```shell
   cmake -S . -B build -G Ninja -DLLVM_DIR=/usr/lib/llvm-17/cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ```

4. Build & Install

   ```
   cmake --build build
   cmake --install build
   ```

   ## Documentation

   - docs/GTestIntegration.md (How to attach Google Test context)
   - docs/Runtime.md (Event model, field semantics, threading/context)
   - docs/Instrumentation.md (Condition normalization, cond_kind, hashing)
   - docs/CallWrapTool.md (AST rewriter flags and internals)
   - docs/ReportExtractor.md (Offline rules to extract <prefix, oracle, cond_chain>)

   Chinese versions are available with `.zh.md` suffix alongside the English docs.
