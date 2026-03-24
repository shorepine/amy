# AMY on Windows

A minimal example that plays a 440Hz sine wave using AMY on Windows.

## Prerequisites

Install **Visual Studio Build Tools 2022** (free) with the C++ workload:

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
```

Or download from https://visualstudio.microsoft.com/visual-cpp-build-tools/ and select **Desktop development with C++**.

## Build

Open a **Developer Command Prompt for VS 2022** (search for it in the Start menu), then:

```cmd
cd path\to\amy\windows
build.bat
```

This produces `amy_sine.exe`.

## Run

```cmd
amy_sine.exe
```

You should hear a 440Hz sine wave. Press Enter to quit.

## CMake (alternative)

If you have CMake installed, you can also build from a Developer Command Prompt:

```cmd
cd windows
mkdir build && cd build
cmake ..
cmake --build . --config Release
```
