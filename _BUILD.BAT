@echo off

REM *** Assumes DOS Turbo C compiler in C:\TC ***
REM *** Place Turbo Assembler (TASM) into %TCBIN% if it's not there ***
REM *** For Turbo C 2.01, set TCBIN=%TC% ***

set TC=C:\TC
set TCBIN=%TC%\BIN
set PATH=%TCBIN%;%PATH%

if exist 8format.exe del 8format.exe
if exist 8tsr.exe del 8tsr.exe
if exist testread.com del testread.com
del *.obj >nul

TCC -I%TC%\INCLUDE -c -f- -1- -G -ms -K MAIN.C 8FLOPPY.C 8FORMAT.C ISADMA.C FAT.C
TLINK /x %TC%\LIB\C0S.OBJ MAIN.OBJ 8FLOPPY.OBJ 8FORMAT.OBJ ISADMA.OBJ FAT.OBJ,8FORMAT.EXE,,%TC%\LIB\CS.LIB

TCC -I%TC%\INCLUDE -c -f- -1- -G -mt -K TESTREAD.C
TLINK /x /t %TC%\LIB\C0T.OBJ TESTREAD.OBJ,TESTREAD.COM,,%TC%\LIB\CS.LIB

TASM 8TSR_A.ASM
TCC -I%TC%\INCLUDE -c -f- -1- -G -ms -K 8TSR.C
TLINK /x %TC%\LIB\C0S.OBJ 8TSR.OBJ 8TSR_A.OBJ,8TSR.EXE,,%TC%\LIB\CS.LIB

del *.obj >nul
del *.dsk >nul
del *.map >nul