#!/bin/bash
# Create static release builds locally for testing

set -e

BUILD_DIR="build-release"
PROJECT_NAME="cachyos-installer"

echo "Creating static release build..."

# Clean previous build
rm -rf "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_DEVENV=OFF \
    -DENABLE_STATIC_BUILD=ON

# Build
echo "Building..."
cmake --build "$BUILD_DIR" --config Release

# Strip binary
echo "Stripping binary..."
strip "$BUILD_DIR/$PROJECT_NAME"

# Show binary info
echo "Binary information:"
file "$BUILD_DIR/$PROJECT_NAME"
ldd "$BUILD_DIR/$PROJECT_NAME" || echo "Static binary (no dynamic dependencies)"
ls -lh "$BUILD_DIR/$PROJECT_NAME"

echo "Static release binary created at: $BUILD_DIR/$PROJECT_NAME"