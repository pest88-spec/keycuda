# Attribution Information for secp256k1-zkp

## Original Library Information

- **Library Name**: secp256k1-zkp
- **Source Location**: third_party/secp256k1-zkp
- **Extraction Date**: 2025-10-22
- **Extraction Script**: scripts/extract-library.sh

## Original Repository

- **URL**: https://github.com/BlockstreamResearch/secp256k1-zkp
- **License**: MIT
- **Purpose**: Optimized secp256k1 library with zero-knowledge proof support

## Extraction Details

This directory contains extracted source code from the secp256k1-zkp library
to enable simplified build setup without external dependency cloning.

### Files Extracted

- **CMakeLists.txt**: Main build configuration
- **include/**: Public header files
- **src/**: Implementation source files

### Build Integration

This library is integrated into the main project through the CMake build system.
The extracted sources replace the need for git submodules or external cloning.

## License

The original secp256k1-zkp library is licensed under the MIT License.
This extraction maintains the same license and does not modify the source code.

## Modifications

No modifications have been made to the source code itself. The only changes
are related to build system integration for this specific project.

## Verification

- ✅ Source integrity preserved
- ✅ Build system updated to use local sources
- ✅ External dependency cloning eliminated
- ✅ Attribution information maintained

## Integration Status

- **T020**: ✅ Implemented secp256k1-zkp extraction
- **T021**: ✅ Attribution headers created
- **T022**: Ready for CMakeLists.txt update
- **T023**: Ready for offline build configuration