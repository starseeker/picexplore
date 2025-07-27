# picojpeg - Tiny JPEG decoder

## Source

This is a forked copy of picojpeg from the original repository:
- **Original Author**: Rich Geldreich <richgel99@gmail.com>
- **Original Repository**: https://github.com/richgel999/picojpeg
- **Version**: Latest master branch as of integration date
- **Original Description**: Public domain JPEG decompressor for 8/16-bit microcontrollers

## License

picojpeg is **public domain** software. The original author has dedicated this work to the public domain.

From the original README:
> picojpeg is a public domain JPEG decompressor written in plain C

The library is also dual-licensed under the MIT license where public domain is not acceptable.

## Files

- `picojpeg.c` - Main JPEG decoder implementation (60KB, single source file)
- `picojpeg.h` - Header file with API declarations and constants
- `jpg2tga.c` - Example application demonstrating picojpeg usage
- `README.md` - This documentation file

## Key Features

- **Minimal memory consumption**: Uses only ~2.3KB of work memory
- **Embedded-friendly**: Written for ROM-abundant, RAM-constrained environments
- **Pure C implementation**: No C runtime dependencies, no dynamic allocation
- **Simple API**: Initialize with `pjpeg_decode_init()`, decode MCUs with `pjpeg_decode_mcu()`
- **8-bit optimized**: Most operations use 8-bit integers for efficiency
- **Winograd IDCT**: Minimizes multiplications (5 per 1D IDCT, up to 80 total)

## Supported Formats

- Baseline sequential grayscale JPEG
- YCbCr color JPEG with chroma sampling factors: H1V1, H1V2, H2V1, H2V2
- **Not supported**: Progressive JPEG

## Intended Future Modifications

This copy of picojpeg has been integrated into the picexplore project with plans for future enhancements:

1. **Partial decode support**: Add functionality to decode only portions of JPEG images
2. **Scaled decode support**: Implement efficient decoding at reduced resolutions (beyond the existing 1/8th mode)
3. **Integration with picexplore**: Optimize for use in the image thumbnail and gallery generation pipeline
4. **Memory optimizations**: Further reduce memory usage for large-scale image processing

## Current Status

- **UNMODIFIED**: The picojpeg source code is currently unmodified from the original
- **Integration**: Basic integration into CMake build system (planned)
- **Testing**: No additional tests added yet (original jpg2tga example available)

## Usage

See the original `jpg2tga.c` for a complete example of how to use the picojpeg API. The basic workflow is:

1. Initialize decoder with `pjpeg_decode_init()`
2. Provide a callback function to supply JPEG data
3. Call `pjpeg_decode_mcu()` repeatedly to decode image MCUs
4. Process the decoded pixel data from the MCU buffers

## Build Integration

This directory is intended to be integrated into the picexplore CMake build system without requiring any external dependencies beyond what picexplore already uses.

---

**Note**: This is a forked copy for use within picexplore. For the latest updates and issues with the original picojpeg library, please refer to the original repository at https://github.com/richgel999/picojpeg.