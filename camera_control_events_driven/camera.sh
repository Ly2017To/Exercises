#!/bin/bash
# Specify the path to your LocoNumber executable
CAMERA_DIR="/a_path_to/camera/bin/"
CAMERA_PATH="/a_path_to/camera/bin/camera_control"

# Add execute permissions to camera_control
chmod -R +rx "$CAMERA_DIR"

# Start camera_control in the background
cd "$CAMERA_DIR"

# Start the second shell script in the background
"$CAMERA_PATH"

