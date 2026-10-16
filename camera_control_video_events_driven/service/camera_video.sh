#!/bin/sh
# Wrapper for the camera control service.
#
# This script is a thin launcher that sets up the run-time environment and
# execs the binary. systemd's Type=simple means this script runs in the
# foreground, so the service's lifecycle == the binary's lifecycle.
#
# Edit INSTALL_DIR below to point at the absolute path where this project
# lives, e.g. /opt/camera_video_control.

INSTALL_DIR="<INSTALL_DIR>"

CAMERA_VIDEO_DIR="$INSTALL_DIR/bin"
CAMERA_VIDEO_PATH="$INSTALL_DIR/bin/camera_video_control"

# Ensure the binary is executable (idempotent; no-op if already +x).
chmod -R +rx "$INSTALL_DIR/bin" 2>/dev/null || true

# cd to the install root so any relative paths in the config (notably
# "videoOutDirs": "videos") resolve to <INSTALL_DIR>/videos.
cd "$INSTALL_DIR" || {
    echo "[camera_video] FATAL: cannot cd to $INSTALL_DIR" 1>&2
    exit 1
}

# Run the binary in the foreground (systemd tracks this process).
exec "$CAMERA_VIDEO_PATH" "$@"
