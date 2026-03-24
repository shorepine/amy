@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo Failed to set up MSVC environment
    exit /b 1
)
cd /d "%~dp0"
cl /O2 /W3 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DAMY_WAVETABLE /I..\src ^
    amy_sine.c ^
    ..\src\algorithms.c ^
    ..\src\amy.c ^
    ..\src\envelope.c ^
    ..\src\examples.c ^
    ..\src\parse.c ^
    ..\src\filters.c ^
    ..\src\oscillators.c ^
    ..\src\pcm.c ^
    ..\src\interp_partials.c ^
    ..\src\custom.c ^
    ..\src\delay.c ^
    ..\src\log2_exp2.c ^
    ..\src\patches.c ^
    ..\src\transfer.c ^
    ..\src\sequencer.c ^
    ..\src\libminiaudio-audio.c ^
    ..\src\instrument.c ^
    ..\src\amy_midi.c ^
    ..\src\api.c ^
    ..\src\midi_mappings.c ^
    /Fe:amy_sine.exe
