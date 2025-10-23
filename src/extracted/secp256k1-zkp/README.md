# Extracted secp256k1-zkp Library

This directory contains the extracted secp256k1-zkp library integrated from the original upstream repository.

## Integration Details

- **Original Source**: https://github.com/BlockstreamResearch/secp256k1-zkp
- **Version**: Integrated from upstream commit (to be specified during extraction)
- **License**: MIT
- **Integration Date**: 2025-10-22
- **Integration Method**: Source code fusion with full attribution

## Directory Structure

- `include/` - Header files with integrated attribution
- `src/` - Source files with integrated attribution
- `build/` - Build configuration and generated files
- `attribution_headers/` - Attribution reference files

## Attribution

All source files have been modified to include proper attribution headers with:
- SPDX license identifier
- Copyright notices
- Origin references (@origin and @sot_ref tags)
- SHA-256 integrity verification

## Build Integration

This library is integrated into the main project build system via CMake. No external dependencies are required.

## Modifications

Only namespace adaptations have been made to prevent conflicts. All core functionality remains identical to the original upstream version.