# iiGeneralDocument: Windows 빌드 및 실행

PDF·XML·HTML·Word 계열 문서 편집과 변환을 제공하는 Qt 비의존 C++23 SDK이다.

검증 기록 갱신: `2026-10-20T02:45:37+09:00`. 변경 전 소스 기준 commit: `f9b7f7419cd9152696be61d65e7f7065249cf0bb`. 아래 결과는 당시의 기록이며, 현재 작업 트리의 검증을 뜻하지 않는다.

## 현재 검증 상태

2026-10-20 기록에서는 Release 빌드·실행 검사·설치와 등록 검사 30개를 통과했다. 이후 변경에 대한 Windows 실행은 별도로 검증해야 한다.

## 이 프로젝트의 의존성과 실행 범위

선행 workspace SDK는 다음과 같다. 각각의 Windows 지침에 따라 실행 검증과 설치를 완료한 뒤 진행한다.

- [iiFileProvider](../../../SDK/iiFileProvider/docs/WindowsBuildAndRun.md)의 Qt 없는 Native 구성요소
- [iiXml](../../../SDK/iiXml/README.md)의 Qt 없는 `iiXmlMarkup` 구성요소

- iiFileProvider Native, iiXmlMarkup, 시스템 libzip 및 zlib을 사용한다. PDF 전용 내부 의존성은 프로젝트가 고정한 버전으로 준비한다.
- legacy DOC 검증에는 `Tools/LibreOffice/SourceDir/LibreOffice/program/soffice.com`이 필요하다. 개발 환경은 LibreOffice 경로를 PATH의 뒤쪽에 둔다.

프로젝트별 Windows 조건은 다음과 같다.

- MSVC JPEG 정적 산출물은 `jpeg-static.lib`이다. 현재 CMake 소스는 이 이름을 처리한다.
- 새로운 LibreOffice 격리 프로필의 legacy DOC 테스트는 변환마다 최대 120초를 허용한다. 공개 API의 기본 conversionTimeout과는 별도이다.
- 설치 소비자와 SDK의 Release 구성을 일치시킨다.

## 환경 준비

PowerShell에서 이 프로젝트 루트로 이동한 뒤 순서대로 실행한다. workspace 배치는 `Workspace/SDK/<이름>`을 전제로 한다.

2026-10-20 당시 도구 기록은 MSVC 19.51 x64, Windows SDK 10.0.28000, CMake 4.4.4, Ninja 1.13.2이다. 개발 shell은 빌드 재현을 위해 Python 3.14.8을 선택하며 시스템 기본 Python 3.15.0과 구분한다.

```powershell
$ErrorActionPreference = 'Stop'
$project = (Get-Location).Path
$workspace = (Resolve-Path -LiteralPath '../..').Path
$tools = Join-Path $workspace 'Tools'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ Build Tools를 찾을 수 없다.' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Set-Location -LiteralPath $project
$cmakeBin = Get-ChildItem (Join-Path $tools 'CMake') -Directory | Sort-Object Name -Descending | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName 'bin' }
$pythonBin = Join-Path $tools 'Python314/tools'
$vcpkg = Join-Path $tools 'vcpkg'
$sdkRoot = Join-Path $env:USERPROFILE '.local/SDK'
$env:PATH = (@($cmakeBin, (Join-Path $tools 'Ninja'), $pythonBin, (Join-Path $vcpkg 'installed/x64-windows/bin'), (Join-Path $sdkRoot 'iiFileProvider/bin')) + @($env:PATH)) -join ';'
$env:CMAKE_PREFIX_PATH = (@((Join-Path $sdkRoot 'iiFileProvider'), (Join-Path $sdkRoot 'iiXml'), (Join-Path $vcpkg 'installed/x64-windows')) -join ';').Replace('\', '/')
$officeBin = Join-Path $tools 'LibreOffice/SourceDir/LibreOffice/program'
if (Test-Path -LiteralPath $officeBin) { $env:PATH += ';' + $officeBin }
$env:CL = '/utf-8 /Zc:__cplusplus /bigobj /DNOMINMAX'
$env:VSLANG = '1033'
$python = (Join-Path $pythonBin 'python.exe').Replace('\', '/')
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source.Replace('\', '/')
$toolchain = (Join-Path $vcpkg 'scripts/buildsystems/vcpkg.cmake').Replace('\', '/')
$msvcInclude = (Join-Path $workspace 'SDK/Windows/WindowsMsvcPrefix.cmake').Replace('\', '/')
$prefix = (Join-Path $sdkRoot 'iiGeneralDocument').Replace('\', '/')
```

이 절차는 공통 Qt 개발 환경 스크립트 대신 MSVC 개발 셸과 필요한 SDK·vcpkg 경로만 준비한다. Qt 키트는 필요하지 않다. `CMAKE_PREFIX_PATH`의 디렉터리 존재만으로 의존성 검증을 대신하지 않는다.

## 구성과 빌드

```powershell
$configure = @(
    '-S', '.', '-B', 'build', '-G', 'Ninja'
    '-DCMAKE_BUILD_TYPE=Release'
    "-DCMAKE_C_COMPILER=$compiler"
    "-DCMAKE_CXX_COMPILER=$compiler"
    "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    '-DVCPKG_TARGET_TRIPLET=x64-windows'
    "-DCMAKE_PROJECT_INCLUDE=$msvcInclude"
    "-DPython3_EXECUTABLE=$python"
    "-DPython_EXECUTABLE=$python"
    '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE'
    '-DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE'
    '-DCMAKE_DISABLE_FIND_PACKAGE_Qt=TRUE'
    '-DBUILD_TESTING=ON'
    '-DIIGENERALDOCUMENT_USE_SYSTEM_LIBZIP=ON'
    '-DIIGENERALDOCUMENT_PDF_ONLY=OFF'
)
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake 구성 실패' }
$cache = Get-Content -LiteralPath 'build/CMakeCache.txt' -Raw
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$') {
    throw 'Release 구성이 유실되었음. 같은 구성 인수에 --fresh를 추가하여 다시 구성할 것'
}
& $python (Join-Path $workspace 'SDK/Windows/Configure-WindowsNinja.py') (Join-Path $project 'build')
if ($LASTEXITCODE -ne 0) { throw 'Ninja MSVC 의존성 설정 실패' }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw '빌드 실패' }
```

모든 생성 파일은 이 프로젝트의 `build/` 아래에 둔다. 컴파일러는 위에서 확인한 절대 경로를 사용한다. 기존 캐시가 다른 compiler 또는 generator를 사용한다면 같은 구성 인수에 `--fresh`를 추가하여 다시 구성한다. MSVC 키트와 MinGW 또는 Debug/Release 라이브러리를 혼합하지 않는다.

`WindowsMsvcPrefix.cmake`와 `Configure-WindowsNinja.py`는 MSVC include 출력 언어를 Ninja에 맞추고 UTF-8 실행 manifest를 적용한다. 빌드 병렬도는 4로 제한하며 실행 검사는 직렬로 수행한다.

## 실행과 확인

PDF 편집·왕복 저장, Word/XML/HTML, 실제 LibreOffice DOC 변환 및 설치 소비자를 검증한다.

```powershell
$dllDirectories = @(Get-ChildItem -LiteralPath build -Recurse -Filter '*.dll' -File |
    Select-Object -ExpandProperty DirectoryName | Sort-Object -Unique)
$env:PATH = (@($dllDirectories) + @($env:PATH)) -join ';'
ctest --test-dir build -C Release --output-on-failure --parallel 1 --no-tests=error
if ($LASTEXITCODE -ne 0) { throw '전체 실행 검사 실패' }
```

현재 빌드에 실제 등록된 실행 경로의 예는 `iiGeneralDocument.PdfObjects`, `iiGeneralDocument.PdfObjects.inspection`, `iiGeneralDocument.PdfObjects.selection`이다. 전체 목록은 `ctest --test-dir build -N`으로 확인한다.

## 설치와 배포 범위

전체 테스트가 통과한 구성만 설치한다. skip된 선택 기능은 지원·실행 범위를 별도 기록한다.

```powershell
cmake --install build --config Release --prefix "$prefix"
if ($LASTEXITCODE -ne 0) { throw '설치 실패' }
```

설치 파일은 `build/install_manifest.txt`와 실제 prefix의 파일을 대조한다. 소비자 실행 시 prefix의 `bin`과 필요한 vcpkg DLL을 검색할 수 있어야 한다.

## 검증 근거

- 빌드 정의: [CMakeLists.txt](../CMakeLists.txt)와 프로젝트 테스트 등록이다.
- 이번 실행 결과: [Windows SDK 전체 검증 기록](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/Verification-20261020.md)이다.
- 프로젝트별 실행 로그와 JUnit은 같은 검증 디렉터리에 보관한다. 로그의 종료 코드, 실패·skip 목록과 실제 실행 범위를 함께 확인한다.

전체 SDK의 실행·설치와 커밋 조건은 [공통 Windows 검증 도구](https://github.com/iisacc-Justmoong/SDK/blob/main/Windows/README.md)에서 재현한다. 원본 JSON·JUnit·상세 로그는 로컬 `Workspace/build/maintenance/20261019-221920-sdk-recovery/final/`에 보관하며 Git에는 포함하지 않는다. 이 프로젝트의 성공은 제품·다른 플랫폼·별도 계정 및 물리 장치의 실연동을 대신하지 않는다.
