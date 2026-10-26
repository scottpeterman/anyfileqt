@echo off
REM scripts\bundle-windows.bat
REM
REM Packages anyfileqt as a self-contained folder using windeployqt.
REM
REM   scripts\bundle-windows.bat
REM   scripts\bundle-windows.bat --qt C:\Qt\6.10.3\msvc2022_64 --zip
REM
REM anyeditqt's bundle-windows.bat with the grammar corpus taken out; every
REM guard is here for the reason given in its comments.
REM
REM Run it from an "x64 Native Tools Command Prompt for VS 2022" so cl.exe is on
REM PATH and %VCToolsRedistDir% is set; the MSVC runtime is located through it.
REM
REM windeployqt is taken from the Qt prefix, NOT from PATH: it must come from
REM the Qt the application linked against.
REM
REM NOT VERIFIED ON WINDOWS for this project.

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
pushd "%ROOT%"

set "QTPREFIX="
set "MAKEZIP=0"
set "BUILD_DIR=build-win"
set "TARGET=anyfile"
set "STAGE=dist\anyfile"

REM --- options ---------------------------------------------------------------

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--zip" (set "MAKEZIP=1" & shift & goto parse)
if /i "%~1"=="--target" (
    if "%~2"=="" (echo error: --target needs a name 1>&2 & goto fail)
    set "TARGET=%~2" & shift & shift & goto parse
)
if /i "%~1"=="--qt" (
    if "%~2"=="" (echo error: --qt needs a path 1>&2 & goto fail)
    set "QTPREFIX=%~f2" & shift & shift & goto parse
)
if /i "%~1"=="--build-dir" (
    if "%~2"=="" (echo error: --build-dir needs a path 1>&2 & goto fail)
    set "BUILD_DIR=%~2" & shift & shift & goto parse
)
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo error: unknown option: %~1 1>&2
goto fail
:parsed

REM --- version ---------------------------------------------------------------
REM
REM Out of CMakeLists.txt, the single source the other two scripts use.

set "VERSION="
for /f "tokens=3" %%V in ('findstr /b /c:"project(anyfileqt VERSION" CMakeLists.txt') do set "VERSION=%%V"
if "%VERSION%"=="" (
    echo error: could not read the version out of CMakeLists.txt 1>&2
    goto fail
)

REM --- Qt --------------------------------------------------------------------

if "%QTPREFIX%"=="" if not "%CMAKE_PREFIX_PATH%"=="" set "QTPREFIX=%CMAKE_PREFIX_PATH%"

REM Qt6_DIR points at <prefix>\lib\cmake\Qt6, so walk back up three.
if "%QTPREFIX%"=="" if not "%Qt6_DIR%"=="" (
    for %%P in ("%Qt6_DIR%\..\..\..") do set "QTPREFIX=%%~fP"
)

REM Newest first, and NOT with dir /o-n. That sorts as TEXT, which puts 6.8.3
REM above 6.10.3 -- and the resulting mismatch is the exact thing this script is
REM supposed to prevent, because it deploys one Qt's DLLs beside a binary linked
REM against another. PowerShell's [version] cast compares the parts as numbers,
REM which is the only correct way to order these.
REM
REM The whole search is ONE PowerShell command on a plain command line, writing
REM to a temp file -- not inside for /f, and not inside an if-block. The old
REM form ran it inside for /f with ^| between stages, but ^ is an escape only
REM OUTSIDE double quotes: PowerShell received a literal "^|", failed, and
REM 2^>nul hid it, so the guess silently found nothing on every machine. Inside
REM double quotes on an ordinary line, | ( ) { } need no escaping at all.
REM
REM msvc dirs match ^msvc\d+_64$ exactly: the looser msvc*_64 also matches
REM msvc2022_arm64, which cannot link an x64 build.
if not "%QTPREFIX%"=="" goto :haveqt
if not exist "C:\Qt" goto :haveqt
powershell -NoProfile -Command "$r = Get-ChildItem 'C:\Qt' -Directory | Where-Object { $_.Name -match '^6\.\d+(\.\d+)*$' } | Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Get-ChildItem $_.FullName -Directory | Where-Object { $_.Name -match '^msvc\d+_64$' } | Sort-Object Name -Descending | Select-Object -First 1 } | Select-Object -First 1; if ($r) { $r.FullName }" > "%TEMP%\anyfile_qtguess.txt" 2>nul
set /p QTPREFIX=<"%TEMP%\anyfile_qtguess.txt"
del "%TEMP%\anyfile_qtguess.txt" >nul 2>nul
if "%QTPREFIX%"=="" goto :haveqt
echo ==^> guessed Qt at %QTPREFIX%
echo     (pass --qt or set CMAKE_PREFIX_PATH to choose a different one)
:haveqt

if "%QTPREFIX%"=="" (
    echo error: no Qt prefix. Either:
    echo         scripts\bundle-windows.bat --qt C:\Qt\6.10.3\msvc2022_64
    echo     or  set CMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
    echo     Nothing matching C:\Qt\6.*\msvc*_64 was found either.
    goto fail
)
if not exist "%QTPREFIX%\bin\windeployqt.exe" (
    echo error: no windeployqt.exe under %QTPREFIX%\bin 1>&2
    echo     That does not look like a Qt prefix. It should be the directory
    echo     holding bin\, lib\ and include\ -- e.g. C:\Qt\6.10.3\msvc2022_64,
    echo     not C:\Qt and not the lib\cmake\Qt6 inside it.
    goto fail
)

REM --- toolchain -------------------------------------------------------------

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo error: cl.exe not on PATH. 1>&2
    echo     Run this from an "x64 Native Tools Command Prompt for VS 2022",
    echo     or run vcvars64.bat in this shell first.
    goto fail
)

echo ==^> version    %VERSION%
echo ==^> Qt         %QTPREFIX%
echo ==^> target     %TARGET%
echo ==^> build dir  %BUILD_DIR%

REM --- build -----------------------------------------------------------------

REM A CMAKE CACHE RECORDS THE ABSOLUTE PATH IT WAS CREATED AT and cannot be
REM relocated. A build directory that travelled with the tree -- in a zip, or a
REM copy under a new name -- fails every cmake call with "is different than the
REM directory ... where CMakeCache.txt was created", naming somebody else's
REM folder, which reads as a path hardcoded in this script. There is none. Wipe
REM it. CMake writes forward slashes into the cache, so compare that way.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /b /c:"CMAKE_HOME_DIRECTORY:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt" > "%TEMP%\anyfile_home.txt" 2>nul
    set "CACHED_SRC="
    for /f "tokens=2 delims==" %%V in ('type "%TEMP%\anyfile_home.txt"') do set "CACHED_SRC=%%V"
    del "%TEMP%\anyfile_home.txt" >nul 2>nul
    set "HERE_SRC=%CD:\=/%"
    if not "!CACHED_SRC!"=="" if /i not "!CACHED_SRC!"=="!HERE_SRC!" (
        echo ==^> %BUILD_DIR% was configured under a different path:
        echo         cached: !CACHED_SRC!
        echo         here:   !HERE_SRC!
        echo     a CMake cache is not relocatable; wiping it.
        rmdir /s /q "%BUILD_DIR%"
    )
)

REM A CACHED Qt6_DIR BEATS -DCMAKE_PREFIX_PATH, so an existing build directory
REM configured against a different Qt keeps using it and says nothing: the
REM binary links one Qt while windeployqt below deploys another. Wiping is the
REM only reliable answer -- CMake offers no way to un-cache a find_package
REM result.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /c:"Qt6_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt" > "%TEMP%\anyfile_qtdir.txt" 2>nul
    set "CACHED="
    for /f "tokens=2 delims==" %%V in ('type "%TEMP%\anyfile_qtdir.txt"') do set "CACHED=%%V"
    del "%TEMP%\anyfile_qtdir.txt" >nul 2>nul
    if not "!CACHED!"=="" (
        echo !CACHED! | findstr /i /c:"%QTPREFIX:\=/%" >nul
        if errorlevel 1 (
            echo ==^> %BUILD_DIR% was configured against a different Qt:
            echo         cached: !CACHED!
            echo         wanted: %QTPREFIX%
            echo     wiping it, or the build and windeployqt would disagree.
            rmdir /s /q "%BUILD_DIR%"
        )
    )
)

echo ==^> building
REM Every option EXPLICITLY: option() honours an existing cache entry.
cmake -S . -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QTPREFIX%" ^
      -DCMAKE_BUILD_TYPE=Release -DANYFILE_BUILD_APP=ON -DANYFILE_BUILD_TESTS=OFF
if errorlevel 1 goto fail

cmake --build "%BUILD_DIR%" --config Release --target %TARGET%
if errorlevel 1 goto fail

REM Multi-config generators (MSBuild, the default here) put it under Release\;
REM single-config ones (Ninja) do not.
set "EXE=%BUILD_DIR%\app\Release\%TARGET%.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\app\%TARGET%.exe"
if not exist "%EXE%" (
    echo error: no %TARGET%.exe after the build 1>&2
    echo     Looked under %BUILD_DIR%\app\
    goto fail
)

REM --- stage -----------------------------------------------------------------

echo ==^> staging into %STAGE%
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
mkdir "%STAGE%\Resources"

copy /y "%EXE%" "%STAGE%\%TARGET%.exe" >nul
if errorlevel 1 goto fail

REM LICENCE TEXTS ARE A DISTRIBUTION OBLIGATION, not documentation. GPLv3 s4
REM wants a copy of the licence conveyed with the work, and LGPLv3 s4d wants
REM the Qt notices and both licence texts with any package that links it. A
REM package missing these is not merely undocumented, it is non-compliant --
REM and nothing about it looks wrong.
copy /y "LICENSE" "%STAGE%\Resources\LICENSE" >nul
if errorlevel 1 goto fail
copy /y "THIRD_PARTY_NOTICES.md" "%STAGE%\Resources\THIRD_PARTY_NOTICES.md" >nul
if errorlevel 1 goto fail
xcopy /e /i /y /q "licenses" "%STAGE%\Resources\licenses" >nul
if errorlevel 1 goto fail

REM --- windeployqt -----------------------------------------------------------

echo ==^> running windeployqt from %QTPREFIX%\bin
"%QTPREFIX%\bin\windeployqt.exe" --release --no-translations ^
    --no-system-d3d-compiler --compiler-runtime "%STAGE%\%TARGET%.exe"
if errorlevel 1 goto fail

REM --- the MSVC runtime ------------------------------------------------------
REM
REM WITHOUT THIS THE PACKAGE DIES ON A CLEAN BOX and runs fine here, because
REM this machine has the VS 2022 redistributable installed and the loader finds
REM it system-wide.
REM
REM --compiler-runtime above is asked for but not trusted. It locates the
REM redistributable through %VCINSTALLDIR%, which only a Developer Command
REM Prompt sets, and depending on the Qt version it may drop vc_redist.x64.exe
REM into the folder INSTEAD of the DLLs. An installer sitting in a directory is
REM not a deployed runtime, and the folder looks equally plausible either way.
REM
REM ucrtbase.dll is deliberately not in the list. The UCRT ships with Windows
REM itself from 10 onward, which is below anything this targets.

set "CRTOK=1"
for %%F in (VCRUNTIME140.dll VCRUNTIME140_1.dll MSVCP140.dll) do (
    if not exist "%STAGE%\%%F" set "CRTOK=0"
)

if "!CRTOK!"=="0" (
    if "%VCToolsRedistDir%"=="" (
        echo error: MSVC runtime DLLs missing from %STAGE%, and 1>&2
        echo         %%VCToolsRedistDir%% is unset so they cannot be located.
        echo     Run this from an "x64 Native Tools Command Prompt for VS 2022".
        goto fail
    )
    set "CRTDIR="
    for /f "delims=" %%D in ('dir /b /ad /o-n "%VCToolsRedistDir%x64\Microsoft.VC*.CRT" 2^>nul') do (
        if "!CRTDIR!"=="" set "CRTDIR=%VCToolsRedistDir%x64\%%D"
    )
    if "!CRTDIR!"=="" (
        REM !VAR! AND NOT %VAR%, and this is not style. Percent-expansion
        REM happens when cmd parses this whole if-block, before it runs a line
        REM of it -- so on a machine where VS lives under
        REM "C:\Program Files (x86)\..." the ")" in "(x86)" closes the block
        REM early and the rest of the path becomes a stray command. Delayed
        REM expansion happens at execution, after the parse that would break.
        echo error: no Microsoft.VC*.CRT directory under 1>&2
        echo         !VCToolsRedistDir!x64
        echo     Add "MSVC v143 - VS 2022 C++ x64/x86 Redistributable MSMs" in
        echo     the VS Installer.
        goto fail
    )
    echo ==^> copying the MSVC runtime from !CRTDIR!
    copy /y "!CRTDIR!\VCRUNTIME140.dll"   "%STAGE%\" >nul
    copy /y "!CRTDIR!\VCRUNTIME140_1.dll" "%STAGE%\" >nul
    copy /y "!CRTDIR!\MSVCP140.dll"       "%STAGE%\" >nul
)

REM Shipping an .exe that asks for elevation inside a folder meant to be
REM unzip-and-run is the wrong signal to whoever receives it.
if exist "%STAGE%\vc_redist.x64.exe" del /q "%STAGE%\vc_redist.x64.exe"

REM --- verify ----------------------------------------------------------------
REM
REM The platform plugin is the one whose absence is fatal and whose error
REM message does not name it usefully: without platforms\qwindows.dll the
REM application exits with "could not find or load the Qt platform plugin".

if not exist "%STAGE%\platforms\qwindows.dll" (
    echo error: platforms\qwindows.dll missing; the package would not start 1>&2
    goto fail
)
if not exist "%STAGE%\Qt6Widgets.dll" (
    echo error: Qt6Widgets.dll missing; windeployqt did not do its job 1>&2
    goto fail
)

for %%F in (LICENSE THIRD_PARTY_NOTICES.md licenses\GPL-3.0.txt licenses\LGPL-3.0.txt) do (
    if not exist "%STAGE%\Resources\%%F" (
        echo error: Resources\%%F missing. It is required to be distributed 1>&2
        echo        with the binary, not optional.
        goto fail
    )
)

for %%F in (VCRUNTIME140.dll VCRUNTIME140_1.dll MSVCP140.dll) do (
    if not exist "%STAGE%\%%F" (
        echo error: %%F missing; the package dies at launch on a machine 1>&2
        echo        without the VS 2022 redistributable, with a missing-DLL
        echo        dialog that says nothing about Qt or about anyfile.
        goto fail
    )
)

REM Everything above proves files are present; this proves they are the RIGHT
REM files. A Qt6Core.dll from a different Qt than the binary was linked against
REM is present, correctly named, and wrong -- and the package still runs here,
REM because the real Qt is on PATH on this machine.
for /f "tokens=2 delims==" %%V in ('findstr /c:"Qt6_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt"') do set "USEDQT=%%V"
echo !USEDQT! | findstr /i /c:"%QTPREFIX:\=/%" >nul
if errorlevel 1 (
    echo error: the build and windeployqt used different Qt installations. 1>&2
    echo         linked against: !USEDQT!
    echo         deployed from:  %QTPREFIX%
    echo     Delete %BUILD_DIR% and run this again.
    goto fail
)

REM anyfile --check: no window, no event loop, nothing written. It prints the
REM Qt version and the volumes it sees, and exits. There is no timeout(1) here,
REM which is why it must never enter the event loop.
echo ==^> smoke test: anyfile --check from the staged folder
"%STAGE%\%TARGET%.exe" --check > "%TEMP%\anyfile-check.txt" 2>&1
type "%TEMP%\anyfile-check.txt"
findstr /c:"volumes:" "%TEMP%\anyfile-check.txt" >nul
if errorlevel 1 (
    echo error: the staged folder ran but did not report its volumes; see above 1>&2
    goto fail
)
del "%TEMP%\anyfile-check.txt" >nul 2>nul

echo ==^> platform plugin, Qt DLLs and MSVC runtime present, one Qt throughout,
echo     and the staged folder starts

REM --- zip -------------------------------------------------------------------

REM NOT inside a parenthesised if-block, and each powershell command on ONE
REM line. Both rules are the same bug twice over:
REM
REM   ^ is a line continuation only OUTSIDE quotes. Inside a quoted string it is
REM   an ordinary character, so a "..." that spans lines ends at the first line
REM   break and cmd tries to run the remainder as a command. That is where
REM   "-Value was unexpected at this time" came from -- AFTER every verification
REM   had passed, which makes it read as a packaging failure rather than a typo.
REM
REM   And a ( ) block is parsed in full before any of it runs, so a stray
REM   parenthesis inside it -- of which a PowerShell expression has several --
REM   can close the block early. A goto costs nothing and removes the class.

if not "%MAKEZIP%"=="1" goto :skipzip

set "ZIP=dist\anyfile-%VERSION%-windows-x64.zip"
echo ==^> writing %ZIP%
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\anyfile' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 goto fail
if not exist "%ZIP%" (
    echo error: Compress-Archive reported success but wrote no %ZIP% 1>&2
    goto fail
)

REM Bare filename inside the .sha256, so the pair verifies wherever it is
REM downloaded to rather than only where it was built. Two spaces between hash
REM and name is sha256sum's format, so one file works on all three platforms.
REM Single quotes throughout: nesting double quotes inside a cmd "..." is its
REM own category of pain and is not needed here.
powershell -NoProfile -Command "$h=(Get-FileHash '%ZIP%' -Algorithm SHA256).Hash.ToLower(); $n=Split-Path '%ZIP%' -Leaf; Set-Content -Path '%ZIP%.sha256' -Value ($h+'  '+$n) -Encoding ASCII"
if errorlevel 1 goto fail
type "%ZIP%.sha256"

:skipzip

echo.
echo run it with:
echo       %STAGE%\%TARGET%.exe
popd
exit /b 0

:usage
echo Packages anyfileqt as a self-contained folder using windeployqt.
echo.
echo   scripts\bundle-windows.bat [--qt ^<prefix^>] [--zip] [--build-dir ^<dir^>]
echo.
echo Environment: CMAKE_PREFIX_PATH, Qt6_DIR
popd
exit /b 0

:fail
echo.
echo build failed
popd
exit /b 1