#!/bin/sh
#
# Install the camera control service unit for the current user.
#
# Usage:
#   ./setup_camera_video_service.sh
#
# After running this script, start the service with:
#   systemctl --user start camera_video.service
# (or simply reboot, if `loginctl enable-linger` has been set up).
#
# Re-run this script after editing camera_video.service to refresh the
# installed copy.

set -e

SERVICE_NAME="camera_video"
SERVICE_FILE="${SERVICE_NAME}.service"

# Optional: on Kylin / Galaxy Kylin the kernel "softmode" allows non-root
# processes to bind privileged ports. Uncomment the line below if you are
# deploying on Kylin and need to allow non-root users to open low ports.
# Most setups do not need this.
# sudo setstatus softmode -p

# Install the unit to the user-level systemd directory.
# This is preferred over /etc/systemd/system/ because it does not require
# root and does not need an enable-linger policy beyond the normal one.
DEST_DIR="$HOME/.config/systemd/user"
mkdir -p "$DEST_DIR"
install -m 0644 "$SERVICE_FILE" "$DEST_DIR/$SERVICE_FILE"
echo "Installed $SERVICE_FILE to $DEST_DIR/"

# Reload the user manager so the new unit is visible.
systemctl --user daemon-reload

# Enable (auto-start on subsequent boots) — does NOT start now.
# To start immediately:
#   systemctl --user start $SERVICE_NAME.service
# To enable + start in one go:
#   systemctl --user enable --now $SERVICE_NAME.service
systemctl --user enable "$SERVICE_NAME.service"
echo "Enabled $SERVICE_NAME.service (will auto-start on next login / reboot)."

# Reminder: for the service to stay alive after the user logs out (e.g.
# on a headless IPC where the session is closed), enable lingering once:
#   loginctl enable-linger "$USER"
# This is NOT done automatically by this script because it is a system-
# wide policy change.

echo
echo "Next steps:"
echo "  1. Edit $DEST_DIR/$SERVICE_FILE and replace <INSTALL_DIR> / <YOUR_LOCAL_IP>"
echo "  2. loginctl enable-linger $USER   (only on headless hosts)"
echo "  3. systemctl --user start $SERVICE_NAME.service"
