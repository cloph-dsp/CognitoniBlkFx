#!/bin/bash
# BlkFx macOS DMG builder
set -e

APP_VERSION="0.2.0"
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/Builds"
DMG_NAME="BlkFx_macOS.dmg"
DMG_PATH="$BUILD_DIR/$DMG_NAME"
STAGING_DIR=$(mktemp -d)

# Copy VST3 bundle
cp -R "$PROJECT_ROOT/build/BlkFx_artefacts/Release/VST3/BlkFx.vst3" "$STAGING_DIR/"

# Create alias to VST3 install folder
ln -s "/Library/Audio/Plug-Ins/VST3" "$STAGING_DIR/"

# Copy readme
cp "$(dirname "$0")/README.txt" "$STAGING_DIR/"

# Create DMG
mkdir -p "$BUILD_DIR"
hdiutil create -volname "BlkFx $APP_VERSION" \
  -srcfolder "$STAGING_DIR" \
  -ov -format UDZO \
  "$DMG_PATH"

rm -rf "$STAGING_DIR"
echo "Created: $DMG_PATH"
