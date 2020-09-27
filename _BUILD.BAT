@echo off

REM *** Assumes DOS Turbo C compiler in C:\TC ***
REM *** Place Turbo Assembler (TASM) into %TC%\BIN if it's not there ***

set TC=C:\TC

if exist 8format.exe del 8format.exe
if exist 8tsr.exe del 8tsr.exe
if exist testread.com del testread.com
del *.obj >nul

%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -G -ms -K MAIN.C 8FLOPPY.C 8FORMAT.C ISADMA.C FAT.C
%TC%\BIN\TLINK /L%TC%\LIB /x /Tde C0FS MAIN.OBJ 8FLOPPY.OBJ 8FORMAT.OBJ ISADMA.OBJ FAT.OBJ,8FORMAT.EXE,,CS

%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -G -mt -K TESTREAD.C
%TC%\BIN\TLINK /L%TC%\LIB /x /Tdc C0FT TESTREAD.OBJ,TESTREAD.COM,,CS

%TC%\BIN\TASM 8TSR.ASM
ren 8TSR.OBJ ASM.OBJ
%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -G -ms -K 8TSR.C
%TC%\BIN\TLINK /L%TC%\LIB /x /Tde C0FS 8TSR.OBJ ASM.OBJ,8TSR.EXE,,CS
del *.obj >nul
del *.dsk >nul
del *.map >nul