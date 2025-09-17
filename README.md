# BrInfo

The tool is capable of analyzing the condition chains within specified C++ source files.

## Prerequisites

To use this tool, the project to be analyzed must have a compilation database similar to `compile_commands.json`.

If the project being analyzed is managed by CMake, you can generate this file by adding `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` when configuring CMake.

If the project is not managed by CMake, you can consider using [Bear](https://github.com/rizsotto/Bear) to generate the compilation database.

## Usage

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

## Build

1. Install the dependencies, including:

   1. C/C++ compilation tools such as `gcc` or `clang`
   2. `CMake`
   3. `Ninja`
   
2. Install development packages:

   ```shell
   sudo apt install -y llvm-17-dev libclang-17-dev clang-tools-17 clang-17 pkg-config
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
