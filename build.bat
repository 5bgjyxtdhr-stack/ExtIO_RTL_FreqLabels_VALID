
@echo off
setlocal
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86

if not exist build mkdir build

cl /nologo /EHsc /MD /O2 ^
  /DWIN32 /D_WINDOWS ^
  /Fe:build\ExtIO_RTL_FreqLabels.obj ^
  /c src\ExtIO_RTL_FreqLabels.cpp

link /nologo /DLL ^
  /DEF:res\ExtIO_RTL_FreqLabels.def ^
  /OUT:build\ExtIO_RTL_FreqLabels.dll ^
  build\ExtIO_RTL_FreqLabels.obj

echo Done: build\ExtIO_RTL_FreqLabels.dll
endlocal
