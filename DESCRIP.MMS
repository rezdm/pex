! DESCRIP.MMS -- OpenVMS MMS build file for pex (TUI only)
!
! Usage:
!   $ MMS                      ! build PEXC.EXE (TUI process explorer)
!   $ MMS /MACRO=("CLEAN=1")   ! clean build artifacts
!   $ MMS /MACRO=("DEBUG=1")   ! build with debug info
!
! Prerequisites:
!   VSI C++ V10.1 or later (Clang 10.0.1 based)
!
! Directory layout on VMS (relative to [.PEX]):
!   [.SRC.CORE.MODEL]          - Core model headers
!   [.SRC.CORE.SERVICES]       - DataStore, NameResolver, SingleInstance
!   [.SRC.PLATFORM.INTERFACES] - Abstract interfaces
!   [.SRC.PLATFORM.OPENVMS]    - OpenVMS platform implementations
!   [.SRC.UI.COMMON.VIEWMODELS]- Shared ViewModel headers (header-only)
!   [.SRC.UI.TUI_VMS]          - SMG$-based TUI frontend
!
! Notes:
!   - Uses /CLANG=("-std=c++17") for C++17 language features
!   - Defines __NEW_STARLET=1 for modern typed VMS system service APIs
!   - Defines __VMS=1 for conditional compilation in platform-neutral code
!   - Links against SMGSHR for SMG$ keyboard input, SYS$SSISHR for SYS$QIOW
!   - name_resolver.cpp and single_instance.cpp are excluded (Unix-specific)
!   - format_utils.hpp requires std::format (C++20) -- needs VMS-specific shim
!

! ============================================================
! Compiler and linker flags
! ============================================================

! /NAMES=(AS_IS,SHORTENED) preserves mixed-case identifiers and handles
! long symbol names (C++ mangling can exceed VMS 31-char limit).
! /CLANG=("-std=c++17") enables C++17 via the underlying Clang compiler.
! /DEFINE=(__NEW_STARLET=1,__VMS=1) enables modern VMS API typedefs.

! IMPORTANT: All Clang options must be in a SINGLE /CLANG= qualifier.
! VMS CXX treats multiple /CLANG= as overrides (last one wins), so -std=c++17
! and -I paths must be combined into one qualifier value.
CXXFLAGS = /NAMES=(AS_IS,SHORTENED) -
           /DEFINE=(__NEW_STARLET=1,__VMS=1,PEX_PLATFORM_OPENVMS=1,_REENTRANT=1)

! Combined Clang flags: language standard + include paths
! -Wno-#warnings suppresses VMS system header <new> #warning about 32-bit allocator.
! Without it, CXX returns warning status (bit 0=0) which MMS treats as abort.
CXXINC = /CLANG=("-std=c++17","-Wno-#warnings", -
                  "-Isrc","-Isrc/core","-Isrc/core/model","-Isrc/core/services", -
                  "-Isrc/platform","-Isrc/platform/interfaces","-Isrc/platform/openvms", -
                  "-Isrc/ui/common/viewmodels","-Isrc/ui/tui_vms")

.IFDEF DEBUG
CXXDBG = /DEBUG /NOOPTIMIZE
LINKDBG = /DEBUG
.ELSE
CXXDBG =
LINKDBG =
.ENDIF

! Output directory for object files
OBJDIR = [.OBJ]

! ============================================================
! Source file lists
! ============================================================

! Core services (only files that compile on VMS)
! NOTE: name_resolver.cpp uses Unix sockets/DNS -- excluded
! NOTE: single_instance.cpp uses Unix domain sockets -- excluded
! NOTE: system_info.cpp is Linux-only (#ifdef PEX_PLATFORM_LINUX) -- excluded
CORE_OBJS = $(OBJDIR)DATA_STORE.OBJ

! Platform: OpenVMS implementations
PLATFORM_OBJS = $(OBJDIR)OPENVMS_PROCESS_DATA_PROVIDER.OBJ -
                $(OBJDIR)OPENVMS_PROCESS_DETAILS.OBJ -
                $(OBJDIR)OPENVMS_SYSTEM_DATA_PROVIDER.OBJ -
                $(OBJDIR)OPENVMS_PROCESS_KILLER.OBJ -
                $(OBJDIR)PLATFORM_FACTORY_OPENVMS.OBJ

! TUI: SMG$-based VMS frontend
TUI_OBJS = $(OBJDIR)SMG_APP.OBJ -
           $(OBJDIR)SMG_APP_DISPLAYS.OBJ -
           $(OBJDIR)SMG_COLORS.OBJ -
           $(OBJDIR)SMG_SYSTEM_PANEL.OBJ -
           $(OBJDIR)SMG_PROCESS_LIST.OBJ -
           $(OBJDIR)SMG_DETAILS_PANEL.OBJ -
           $(OBJDIR)SMG_DETAILS_TABS.OBJ -
           $(OBJDIR)SMG_INPUT.OBJ -
           $(OBJDIR)SMG_KILL_DIALOG.OBJ

! Main entry point
MAIN_OBJ = $(OBJDIR)MAIN_TUI_VMS.OBJ

ALL_OBJS = $(MAIN_OBJ) $(CORE_OBJS) $(PLATFORM_OBJS) $(TUI_OBJS)

! ============================================================
! Targets
! ============================================================

.IFDEF CLEAN
ALL :
	- DELETE/NOLOG $(OBJDIR)*.OBJ;*
	- DELETE/NOLOG PEXC.EXE;*
	- DELETE/NOLOG PEXC.OPT;*
	@ WRITE SYS$OUTPUT "Clean complete."
.ELSE
ALL : $(OBJDIR) PEXC.EXE
	@ WRITE SYS$OUTPUT ""
	@ WRITE SYS$OUTPUT "Build complete: PEXC.EXE"
.ENDIF

! Create object file directory if it does not exist
$(OBJDIR) :
	CREATE/DIRECTORY $(OBJDIR)

! Link the final executable
! Uses an options file to avoid DCL's 8-parameter limit on the LINK command.
! The .OPT file lists all object files and shared libraries, one per line.
PEXC.EXE : $(ALL_OBJS)
	@ OPEN/WRITE OPTF PEXC.OPT
	@ WRITE OPTF "[.OBJ]MAIN_TUI_VMS.OBJ"
	@ WRITE OPTF "[.OBJ]DATA_STORE.OBJ"
	@ WRITE OPTF "[.OBJ]OPENVMS_PROCESS_DATA_PROVIDER.OBJ"
	@ WRITE OPTF "[.OBJ]OPENVMS_PROCESS_DETAILS.OBJ"
	@ WRITE OPTF "[.OBJ]OPENVMS_SYSTEM_DATA_PROVIDER.OBJ"
	@ WRITE OPTF "[.OBJ]OPENVMS_PROCESS_KILLER.OBJ"
	@ WRITE OPTF "[.OBJ]PLATFORM_FACTORY_OPENVMS.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_APP.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_APP_DISPLAYS.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_COLORS.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_SYSTEM_PANEL.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_PROCESS_LIST.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_DETAILS_PANEL.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_DETAILS_TABS.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_INPUT.OBJ"
	@ WRITE OPTF "[.OBJ]SMG_KILL_DIALOG.OBJ"
	@ WRITE OPTF "SYS$SHARE:SMGSHR/SHAREABLE"
	@ WRITE OPTF "SYS$SHARE:SYS$SSISHR/SHAREABLE"
	@ WRITE OPTF "SYS$SHARE:PTHREAD$RTL/SHAREABLE"
	@ WRITE OPTF "SYS$SHARE:CMA$TIS_SHR/SHAREABLE"
	@ CLOSE OPTF
	LINK $(LINKDBG) /EXECUTABLE=PEXC.EXE PEXC.OPT/OPTIONS

! ============================================================
! Main entry point
! ============================================================

$(OBJDIR)MAIN_TUI_VMS.OBJ : [.SRC]MAIN_TUI_VMS.CXX -
                             [.SRC.PLATFORM]PLATFORM_FACTORY.HPP -
                             [.SRC.CORE.SERVICES]DATA_STORE.HPP -
                             [.SRC.UI.TUI_VMS]SMG_APP.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC]MAIN_TUI_VMS.CXX

! ============================================================
! Core services
! ============================================================

$(OBJDIR)DATA_STORE.OBJ : [.SRC.CORE.SERVICES]DATA_STORE.CXX -
                          [.SRC.CORE.SERVICES]DATA_STORE.HPP -
                          [.SRC.CORE.MODEL]PROCESS_INFO.HPP -
                          [.SRC.CORE.MODEL]SYSTEM_INFO.HPP -
                          [.SRC.PLATFORM.INTERFACES]I_PROCESS_DATA_PROVIDER.HPP -
                          [.SRC.PLATFORM.INTERFACES]I_SYSTEM_DATA_PROVIDER.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.CORE.SERVICES]DATA_STORE.CXX

! ============================================================
! Platform: OpenVMS implementations
! ============================================================

$(OBJDIR)OPENVMS_PROCESS_DATA_PROVIDER.OBJ : [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DATA_PROVIDER.CXX -
                                              [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DATA_PROVIDER.HPP -
                                              [.SRC.PLATFORM.INTERFACES]I_PROCESS_DATA_PROVIDER.HPP -
                                              [.SRC.CORE.MODEL]PROCESS_INFO.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DATA_PROVIDER.CXX

$(OBJDIR)OPENVMS_PROCESS_DETAILS.OBJ : [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DETAILS.CXX -
                                        [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DATA_PROVIDER.HPP -
                                        [.SRC.PLATFORM.INTERFACES]I_PROCESS_DATA_PROVIDER.HPP -
                                        [.SRC.CORE.MODEL]PROCESS_INFO.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DETAILS.CXX

$(OBJDIR)OPENVMS_SYSTEM_DATA_PROVIDER.OBJ : [.SRC.PLATFORM.OPENVMS]OPENVMS_SYSTEM_DATA_PROVIDER.CXX -
                                             [.SRC.PLATFORM.OPENVMS]OPENVMS_SYSTEM_DATA_PROVIDER.HPP -
                                             [.SRC.PLATFORM.INTERFACES]I_SYSTEM_DATA_PROVIDER.HPP -
                                             [.SRC.CORE.MODEL]SYSTEM_INFO.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.PLATFORM.OPENVMS]OPENVMS_SYSTEM_DATA_PROVIDER.CXX

$(OBJDIR)OPENVMS_PROCESS_KILLER.OBJ : [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_KILLER.CXX -
                                       [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_KILLER.HPP -
                                       [.SRC.PLATFORM.INTERFACES]I_PROCESS_KILLER.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_KILLER.CXX

$(OBJDIR)PLATFORM_FACTORY_OPENVMS.OBJ : [.SRC.PLATFORM.OPENVMS]PLATFORM_FACTORY_OPENVMS.CXX -
                                         [.SRC.PLATFORM]PLATFORM_FACTORY.HPP -
                                         [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_DATA_PROVIDER.HPP -
                                         [.SRC.PLATFORM.OPENVMS]OPENVMS_SYSTEM_DATA_PROVIDER.HPP -
                                         [.SRC.PLATFORM.OPENVMS]OPENVMS_PROCESS_KILLER.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.PLATFORM.OPENVMS]PLATFORM_FACTORY_OPENVMS.CXX

! ============================================================
! TUI: SMG$-based VMS frontend
! ============================================================

$(OBJDIR)SMG_APP.OBJ : [.SRC.UI.TUI_VMS]SMG_APP.CXX -
                        [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                        [.SRC.UI.TUI_VMS]SMG_COLORS.HPP -
                        [.SRC.CORE]FORMAT_UTILS.HPP -
                        [.SRC.CORE.SERVICES]DATA_STORE.HPP -
                        [.SRC.UI.COMMON.VIEWMODELS]APP_VIEW_MODEL.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_APP.CXX

$(OBJDIR)SMG_COLORS.OBJ : [.SRC.UI.TUI_VMS]SMG_COLORS.CXX -
                            [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_COLORS.CXX

$(OBJDIR)SMG_APP_DISPLAYS.OBJ : [.SRC.UI.TUI_VMS]SMG_APP_DISPLAYS.CXX -
                                  [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                  [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_APP_DISPLAYS.CXX

$(OBJDIR)SMG_SYSTEM_PANEL.OBJ : [.SRC.UI.TUI_VMS]SMG_SYSTEM_PANEL.CXX -
                                  [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                  [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_SYSTEM_PANEL.CXX

$(OBJDIR)SMG_PROCESS_LIST.OBJ : [.SRC.UI.TUI_VMS]SMG_PROCESS_LIST.CXX -
                                  [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                  [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_PROCESS_LIST.CXX

$(OBJDIR)SMG_DETAILS_PANEL.OBJ : [.SRC.UI.TUI_VMS]SMG_DETAILS_PANEL.CXX -
                                   [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                   [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_DETAILS_PANEL.CXX

$(OBJDIR)SMG_DETAILS_TABS.OBJ : [.SRC.UI.TUI_VMS]SMG_DETAILS_TABS.CXX -
                                  [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                  [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_DETAILS_TABS.CXX

$(OBJDIR)SMG_INPUT.OBJ : [.SRC.UI.TUI_VMS]SMG_INPUT.CXX -
                           [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                           [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_INPUT.CXX

$(OBJDIR)SMG_KILL_DIALOG.OBJ : [.SRC.UI.TUI_VMS]SMG_KILL_DIALOG.CXX -
                                 [.SRC.UI.TUI_VMS]SMG_APP.HPP -
                                 [.SRC.UI.TUI_VMS]SMG_COLORS.HPP
	CXX $(CXXFLAGS) $(CXXINC) $(CXXDBG) /OBJECT=$(MMS$TARGET) [.SRC.UI.TUI_VMS]SMG_KILL_DIALOG.CXX
