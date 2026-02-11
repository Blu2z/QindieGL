##############################################################################
##
##  Makefile for Detours.
##
##  Microsoft Research Detours Package
##
##  Copyright (c) Microsoft Corporation.  All rights reserved.
##

ROOT = .
!include "$(ROOT)\system.mak"

all:
    cd "$(MAKEDIR)"
    @if exist "$(MAKEDIR)\core\Makefile" cd "$(MAKEDIR)\core" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS)
    cd "$(MAKEDIR)\src"
    @$(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS)
    @if exist "$(MAKEDIR)\bugs\Makefile" cd "$(MAKEDIR)\bugs" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS)
    cd "$(MAKEDIR)"

clean:
    cd "$(MAKEDIR)"
    @if exist "$(MAKEDIR)\core\Makefile" cd "$(MAKEDIR)\core" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) clean
    cd "$(MAKEDIR)\src"
    @$(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) clean
    @if exist "$(MAKEDIR)\bugs\Makefile" cd "$(MAKEDIR)\bugs" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) clean
    cd "$(MAKEDIR)"

realclean: clean
    cd "$(MAKEDIR)"
    @if exist "$(MAKEDIR)\core\Makefile" cd "$(MAKEDIR)\core" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) realclean
    cd "$(MAKEDIR)\src"
    @$(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) realclean
    @if exist "$(MAKEDIR)\bugs\Makefile" cd "$(MAKEDIR)\bugs" && $(MAKE) /F Makefile /NOLOGO /$(MAKEFLAGS) realclean
    cd "$(MAKEDIR)"
    -rmdir /q /s $(INCDS) 2> nul
    -rmdir /q /s $(LIBDS) 2> nul
    -rmdir /q /s $(BINDS) 2> nul
    -rmdir /q /s dist 2> nul
    -del docsrc\detours.chm 2> nul
    -del /q *.msi 2>nul
    -del /q /f /s *~ 2>nul

test:
    cd "$(MAKEDIR)"

################################################################# End of File.
