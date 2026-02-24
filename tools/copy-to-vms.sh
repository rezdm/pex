#!/bin/bash
# copy-to-vms.sh -- Copy PEX VMS source files to OpenVMS host via SCP
#
# Usage:
#   ./tools/copy-to-vms.sh user@vms-host [remote-dir]
#
# Example:
#   ./tools/copy-to-vms.sh rezdm@192.168.56.10
#   ./tools/copy-to-vms.sh rezdm@192.168.56.10 /SYS\$SYSDEVICE/REZDM/PEX
#
# Default remote dir: ~/PEX (home directory)
#
# This copies all source files needed for the VMS build and renames
# .cpp -> .CXX (VMS CXX compiler convention). Headers keep .HPP extension.
# The DESCRIP.MMS build file is also copied.

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 user@vms-host [remote-dir]"
    echo "Example: $0 rezdm@192.168.56.10"
    exit 1
fi

VMS_HOST="$1"
REMOTE_DIR="${2:-PEX}"
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== PEX VMS Source Transfer ==="
echo "Host: $VMS_HOST"
echo "Remote dir: $REMOTE_DIR"
echo "Source dir: $SCRIPT_DIR"
echo ""

# Create directory structure on VMS
echo "Creating directories..."
ssh "$VMS_HOST" "
    CREATE/DIR ${REMOTE_DIR}/
    CREATE/DIR ${REMOTE_DIR}/OBJ/
    CREATE/DIR ${REMOTE_DIR}/SRC/
    CREATE/DIR ${REMOTE_DIR}/SRC/CORE/
    CREATE/DIR ${REMOTE_DIR}/SRC/CORE/MODEL/
    CREATE/DIR ${REMOTE_DIR}/SRC/CORE/SERVICES/
    CREATE/DIR ${REMOTE_DIR}/SRC/PLATFORM/
    CREATE/DIR ${REMOTE_DIR}/SRC/PLATFORM/INTERFACES/
    CREATE/DIR ${REMOTE_DIR}/SRC/PLATFORM/OPENVMS/
    CREATE/DIR ${REMOTE_DIR}/SRC/UI/
    CREATE/DIR ${REMOTE_DIR}/SRC/UI/COMMON/
    CREATE/DIR ${REMOTE_DIR}/SRC/UI/COMMON/VIEWMODELS/
    CREATE/DIR ${REMOTE_DIR}/SRC/UI/TUI_VMS/
" 2>/dev/null || true

# Function to copy a file, renaming .cpp -> .CXX
copy_file() {
    local src="$1"
    local dest_dir="$2"
    local basename=$(basename "$src")

    # Rename .cpp -> .CXX for VMS CXX compiler
    local dest_name="${basename/.cpp/.CXX}"

    echo "  $basename -> $dest_dir/$dest_name"
    scp -q "$SCRIPT_DIR/$src" "$VMS_HOST:${REMOTE_DIR}/${dest_dir}/${dest_name}"
}

# Copy DESCRIP.MMS (build file)
echo "Copying build file..."
scp -q "$SCRIPT_DIR/DESCRIP.MMS" "$VMS_HOST:${REMOTE_DIR}/DESCRIP.MMS"
echo "  DESCRIP.MMS"

# Main entry point
echo "Copying main entry point..."
copy_file "src/main_tui_vms.cpp" "SRC"

# Core model headers
echo "Copying core model headers..."
copy_file "src/core/model/process_info.hpp" "SRC/CORE/MODEL"
copy_file "src/core/model/system_info.hpp" "SRC/CORE/MODEL"
copy_file "src/core/model/errors.hpp" "SRC/CORE/MODEL"
copy_file "src/core/format_utils.hpp" "SRC/CORE"

# Core services
echo "Copying core services..."
copy_file "src/core/services/data_store.cpp" "SRC/CORE/SERVICES"
copy_file "src/core/services/data_store.hpp" "SRC/CORE/SERVICES"

# Platform interfaces
echo "Copying platform interfaces..."
copy_file "src/platform/platform_factory.hpp" "SRC/PLATFORM"
copy_file "src/platform/interfaces/i_process_data_provider.hpp" "SRC/PLATFORM/INTERFACES"
copy_file "src/platform/interfaces/i_system_data_provider.hpp" "SRC/PLATFORM/INTERFACES"
copy_file "src/platform/interfaces/i_process_killer.hpp" "SRC/PLATFORM/INTERFACES"

# OpenVMS platform implementations
echo "Copying OpenVMS platform..."
copy_file "src/platform/openvms/openvms_process_data_provider.cpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_process_data_provider.hpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_process_details.cpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_system_data_provider.cpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_system_data_provider.hpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_process_killer.cpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/openvms_process_killer.hpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/platform_factory_openvms.cpp" "SRC/PLATFORM/OPENVMS"
copy_file "src/platform/openvms/vms_mutex.hpp" "SRC/PLATFORM/OPENVMS"

# TUI VMS frontend
echo "Copying SMG$ TUI frontend..."
copy_file "src/ui/tui_vms/smg_app.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_app.hpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_app_displays.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_colors.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_colors.hpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_system_panel.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_process_list.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_details_panel.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_details_tabs.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_input.cpp" "SRC/UI/TUI_VMS"
copy_file "src/ui/tui_vms/smg_kill_dialog.cpp" "SRC/UI/TUI_VMS"

# Shared viewmodels (header-only)
echo "Copying shared viewmodels..."
copy_file "src/ui/common/viewmodels/app_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"
copy_file "src/ui/common/viewmodels/details_panel_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"
copy_file "src/ui/common/viewmodels/kill_dialog_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"
copy_file "src/ui/common/viewmodels/process_list_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"
copy_file "src/ui/common/viewmodels/process_popup_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"
copy_file "src/ui/common/viewmodels/system_panel_view_model.hpp" "SRC/UI/COMMON/VIEWMODELS"

echo ""
echo "=== Transfer complete (37 files) ==="
echo ""
echo "On VMS, build with:"
echo '  $ SET DEFAULT [.PEX]   ! or wherever REMOTE_DIR points'
echo '  $ MMS'
echo '  $ DEFINE/USER SYS$ERROR SYS$OUTPUT'
echo '  $ RUN PEXC.EXE'
