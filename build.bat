@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x86 || call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x86
setlocal ENABLEDELAYEDEXPANSION

rem Build 32-bit DLL with explicit .def exports and machine x86
cl /nologo /EHsc /MD /O2 /DWIN32 /D_WINDOWS /D_USRDLL /D_WINDLL /DWINRAD_EXTIO /I. ^
  ExtIO_RTL_FreqLabels.cpp ^
  /link /DLL /OUT:ExtIO_RTL_FreqLabels.dll /DEF:ExtIO_RTL_FreqLabels.def /MACHINE:X86

if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

mkdir artifacts 2>nul
copy ExtIO_RTL_FreqLabels.dll artifacts\
if exist freq_labels.csv copy freq_labels.csv artifacts\
