! DESCRIP.MMS — OpenVMS MMS build file for pex
!
! Usage:
!   $ MMS                  ! build hello world test
!   $ MMS /MACRO=CLEAN     ! clean build artifacts
!
! Prerequisites:
!   VSI C++ V10.1 or later
!

CXXFLAGS = /STANDARD=LATEST /NAMES=(AS_IS,SHORTENED)

.IFDEF CLEAN
ALL :
	DELETE/NOLOG *.OBJ;*,*.EXE;*
	@ WRITE SYS$OUTPUT "Clean complete."
.ELSE
ALL : HELLO.EXE
	@ WRITE SYS$OUTPUT "Build complete."
.ENDIF

HELLO.EXE : HELLO.OBJ
	LINK HELLO.OBJ /EXECUTABLE=HELLO.EXE

HELLO.OBJ : HELLO.CXX
	CXX $(CXXFLAGS) HELLO.CXX /OBJECT=HELLO.OBJ
