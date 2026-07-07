# Document detector C++ prototype

OpenCV-based document detection and perspective correction.

Build:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Run:

```sh
./build/detect_doc input.jpg output.jpg
./build/detect_doc input.jpg output_bw.jpg --bw
```

Generated files:

- `output.jpg` - cropped, perspective-corrected, enhanced document
- `output.jpg.debug.jpg` - original image with detected quadrilateral overlay

Core API:

- `docdet::detectDocument`
- `docdet::warpDocument`
- `docdet::enhanceDocument`
- `docdet_detect_file` / `docdet_process_file` for Flutter FFI

Source layout:

- `src/document_detector.cpp` - candidate generation, scoring, and detector orchestration
- `src/document_geometry.cpp` - corner ordering helpers
- `src/document_transform.cpp` - perspective warp/crop
- `src/document_enhance.cpp` - document color/BW enhancement
- `src/document_detector_ffi.cpp` - stable C ABI wrapper for Dart FFI
- `src/main.cpp` - command-line runner and debug overlay output

This is a clean-room OpenCV implementation inspired by the native pipeline shape found in the APK:
detect border -> crop/dewarp -> enhance. It is not a copy of the proprietary native code.

## Flutter FFI packaging

The Flutter-facing boundary is `include/document_detector_ffi.h`. It only exposes C types so
Dart can call it through `dart:ffi`.

Desktop smoke build:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

This produces:

- `build/libdocument_detector.dylib` on macOS
- `build/libdocument_detector.so` on Android/Linux
- `build/detect_doc` for command-line testing

### Android

Build the shared library with Android NDK CMake. Example:

```sh
cmake -S detect_c+ -B detect_c+/build-android-arm64 \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-23 \
  -DOpenCV_DIR=/path/to/opencv-android-sdk/sdk/native/jni

cmake --build detect_c+/build-android-arm64
```

Then place the output in the Flutter app:

```text
android/app/src/main/jniLibs/arm64-v8a/libdocument_detector.so
```

Repeat for other ABIs if needed:

- `armeabi-v7a`
- `x86_64`

### iOS

For iOS, build/link the same C++ sources plus OpenCV iOS as a static library or `.xcframework`.
The Dart side should use `DynamicLibrary.process()` when the native symbols are linked into the
app binary.

Recommended output shape:

```text
ios/Frameworks/DocumentDetector.xcframework
```

Expose these symbols from the iOS binary:

- `docdet_detect_file`
- `docdet_process_file`
- `docdet_last_error`

### Dart

See `flutter_ffi_example/document_detector_ffi.dart` for a minimal binding.

Add this dependency to the Flutter app:

```yaml
dependencies:
  ffi: ^2.1.0
```

Example usage:

```dart
final detector = DocumentDetectorFfi();
final result = detector.processFile(
  inputPath: inputImagePath,
  outputPath: outputImagePath,
  blackWhite: false,
);

print(result.corners); // [x0, y0, x1, y1, x2, y2, x3, y3]
print(result.score);
```
