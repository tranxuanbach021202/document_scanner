import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

/// Detection mode mirrors the native DocdetMode enum.
enum DetectMode { photo, video }

/// Result of a document detection: 4 corners (clockwise from top-left) in the
/// coordinate space of the source frame, plus the detector confidence score.
class DocumentDetection {
  const DocumentDetection({required this.corners, required this.score});

  /// [x0,y0, x1,y1, x2,y2, x3,y3] — TL, TR, BR, BL.
  final List<double> corners;
  final double score;

  Offset cornerAt(int i) => Offset(corners[i * 2], corners[i * 2 + 1]);
}

/// Seed data for the manual corner-adjustment screen: the normalized image's
/// detected (or default) corners plus its pixel dimensions.
class CaptureSeed {
  const CaptureSeed({
    required this.corners,
    required this.score,
    required this.imgW,
    required this.imgH,
  });

  /// [x0,y0, x1,y1, x2,y2, x3,y3] in the normalized image's pixel space.
  final List<double> corners;
  final double score;
  final int imgW;
  final int imgH;
}

/// Lightweight point holder (avoids importing Flutter into the binding layer).
class Offset {
  const Offset(this.dx, this.dy);
  final double dx;
  final double dy;
}

// ---------------------------------------------------------------------------
// Native struct + typedefs
// ---------------------------------------------------------------------------

final class _Options extends Struct {
  @Int32()
  external int maxDetectSize;
  @Double()
  external double minAreaRatio;
  @Double()
  external double cannyLow;
  @Double()
  external double cannyHigh;
  @Int32()
  external int morphKernel;
  @Int32()
  external int useHoughFallback;
  @Int32()
  external int mode;
  @Double()
  external double earlyAcceptScore;
  @Int32()
  external int subpixelRefine;
}

typedef _DefaultOptionsNative = _Options Function();

typedef _DetectYuvNative =
    Int32 Function(
      Pointer<Uint8> y,
      Pointer<Uint8> u,
      Pointer<Uint8> v,
      Int32 width,
      Int32 height,
      Int32 yRowStride,
      Int32 uvRowStride,
      Int32 uvPixelStride,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );
typedef _DetectYuvDart =
    int Function(
      Pointer<Uint8> y,
      Pointer<Uint8> u,
      Pointer<Uint8> v,
      int width,
      int height,
      int yRowStride,
      int uvRowStride,
      int uvPixelStride,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _DetectRgbaNative =
    Int32 Function(
      Pointer<Uint8> data,
      Int32 width,
      Int32 height,
      Int32 rowStride,
      Int32 channels,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );
typedef _DetectRgbaDart =
    int Function(
      Pointer<Uint8> data,
      int width,
      int height,
      int rowStride,
      int channels,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _ProcessFileNative =
    Int32 Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      Int32 blackWhite,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );
typedef _ProcessFileDart =
    int Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      int blackWhite,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _PrepareCaptureNative =
    Int32 Function(
      Pointer<Char> inputPath,
      Pointer<Char> normalizedOutput,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
      Pointer<Int32> outWidth,
      Pointer<Int32> outHeight,
    );
typedef _PrepareCaptureDart =
    int Function(
      Pointer<Char> inputPath,
      Pointer<Char> normalizedOutput,
      Pointer<_Options> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
      Pointer<Int32> outWidth,
      Pointer<Int32> outHeight,
    );

typedef _WarpFileNative =
    Int32 Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      Pointer<Float> corners8,
      Int32 blackWhite,
    );
typedef _WarpFileDart =
    int Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      Pointer<Float> corners8,
      int blackWhite,
    );

typedef _LastErrorNative = Pointer<Char> Function();
typedef _LastErrorDart = Pointer<Char> Function();

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

class DocumentDetector {
  DocumentDetector({DynamicLibrary? library})
    : _lib = library ?? _open() {
    _defaultOptions = _lib
        .lookupFunction<_DefaultOptionsNative, _DefaultOptionsNative>(
          'docdet_default_options',
        );
    _detectYuv = _lib.lookupFunction<_DetectYuvNative, _DetectYuvDart>(
      'docdet_detect_yuv420',
    );
    _detectRgba = _lib.lookupFunction<_DetectRgbaNative, _DetectRgbaDart>(
      'docdet_detect_rgba',
    );
    _processFile = _lib.lookupFunction<_ProcessFileNative, _ProcessFileDart>(
      'docdet_process_file',
    );
    _prepareCapture = _lib
        .lookupFunction<_PrepareCaptureNative, _PrepareCaptureDart>(
          'docdet_prepare_capture',
        );
    _warpFile = _lib.lookupFunction<_WarpFileNative, _WarpFileDart>(
      'docdet_warp_file',
    );
    _lastError = _lib.lookupFunction<_LastErrorNative, _LastErrorDart>(
      'docdet_last_error',
    );
  }

  final DynamicLibrary _lib;
  late final _DefaultOptionsNative _defaultOptions;
  late final _DetectYuvDart _detectYuv;
  late final _DetectRgbaDart _detectRgba;
  late final _ProcessFileDart _processFile;
  late final _PrepareCaptureDart _prepareCapture;
  late final _WarpFileDart _warpFile;
  late final _LastErrorDart _lastError;

  Pointer<_Options> _makeOptions(DetectMode mode) {
    final ptr = calloc<_Options>();
    ptr.ref = _defaultOptions();
    ptr.ref.mode = mode == DetectMode.video ? 1 : 0;
    return ptr;
  }

  /// Detect on a YUV_420_888 camera frame (Android live preview).
  /// Returns null when no document is found.
  DocumentDetection? detectYuv420({
    required Uint8List yPlane,
    required Uint8List uPlane,
    required Uint8List vPlane,
    required int width,
    required int height,
    required int yRowStride,
    required int uvRowStride,
    required int uvPixelStride,
    DetectMode mode = DetectMode.video,
  }) {
    final y = calloc<Uint8>(yPlane.length);
    final u = calloc<Uint8>(uPlane.length);
    final v = calloc<Uint8>(vPlane.length);
    final corners = calloc<Float>(8);
    final score = calloc<Double>();
    final opts = _makeOptions(mode);
    try {
      y.asTypedList(yPlane.length).setAll(0, yPlane);
      u.asTypedList(uPlane.length).setAll(0, uPlane);
      v.asTypedList(vPlane.length).setAll(0, vPlane);
      final status = _detectYuv(
        y, u, v, width, height,
        yRowStride, uvRowStride, uvPixelStride,
        opts, corners, score,
      );
      if (status != 0) return null;
      return DocumentDetection(
        corners: List<double>.generate(8, (i) => corners[i]),
        score: score.value,
      );
    } finally {
      calloc.free(y);
      calloc.free(u);
      calloc.free(v);
      calloc.free(corners);
      calloc.free(score);
      calloc.free(opts);
    }
  }

  /// Detect on a packed RGBA/BGRA frame (iOS live preview).
  DocumentDetection? detectRgba({
    required Uint8List data,
    required int width,
    required int height,
    required int rowStride,
    int channels = 4,
    DetectMode mode = DetectMode.video,
  }) {
    final buf = calloc<Uint8>(data.length);
    final corners = calloc<Float>(8);
    final score = calloc<Double>();
    final opts = _makeOptions(mode);
    try {
      buf.asTypedList(data.length).setAll(0, data);
      final status = _detectRgba(
        buf, width, height, rowStride, channels, opts, corners, score,
      );
      if (status != 0) return null;
      return DocumentDetection(
        corners: List<double>.generate(8, (i) => corners[i]),
        score: score.value,
      );
    } finally {
      calloc.free(buf);
      calloc.free(corners);
      calloc.free(score);
      calloc.free(opts);
    }
  }

  /// Full capture pipeline: detect + crop/dewarp + enhance, writing to
  /// [outputPath]. Returns the detection (corners in the original photo space).
  DocumentDetection processFile({
    required String inputPath,
    required String outputPath,
    bool blackWhite = false,
    DetectMode mode = DetectMode.photo,
  }) {
    final input = inputPath.toNativeUtf8();
    final output = outputPath.toNativeUtf8();
    final corners = calloc<Float>(8);
    final score = calloc<Double>();
    final opts = _makeOptions(mode);
    try {
      final status = _processFile(
        input.cast<Char>(),
        output.cast<Char>(),
        blackWhite ? 1 : 0,
        opts,
        corners,
        score,
      );
      if (status != 0) {
        throw DocumentDetectorException(status, _errorMessage());
      }
      return DocumentDetection(
        corners: List<double>.generate(8, (i) => corners[i]),
        score: score.value,
      );
    } finally {
      calloc.free(input);
      calloc.free(output);
      calloc.free(corners);
      calloc.free(score);
      calloc.free(opts);
    }
  }

  /// Prepare a captured still for the manual corner-adjustment screen: writes an
  /// upright, EXIF-free copy to [normalizedPath] and returns the seed corners
  /// (detected, or a ~10% inset rectangle when detection fails) in that image's
  /// pixel space along with its dimensions.
  CaptureSeed prepareCapture({
    required String inputPath,
    required String normalizedPath,
    DetectMode mode = DetectMode.photo,
  }) {
    final input = inputPath.toNativeUtf8();
    final output = normalizedPath.toNativeUtf8();
    final corners = calloc<Float>(8);
    final score = calloc<Double>();
    final width = calloc<Int32>();
    final height = calloc<Int32>();
    final opts = _makeOptions(mode);
    try {
      final status = _prepareCapture(
        input.cast<Char>(),
        output.cast<Char>(),
        opts,
        corners,
        score,
        width,
        height,
      );
      if (status != 0) {
        throw DocumentDetectorException(status, _errorMessage());
      }
      return CaptureSeed(
        corners: List<double>.generate(8, (i) => corners[i]),
        score: score.value,
        imgW: width.value,
        imgH: height.value,
      );
    } finally {
      calloc.free(input);
      calloc.free(output);
      calloc.free(corners);
      calloc.free(score);
      calloc.free(width);
      calloc.free(height);
      calloc.free(opts);
    }
  }

  /// Warp + enhance [inputPath] using the caller-supplied [corners] (8 doubles,
  /// pixel space of [inputPath]) — no detection — writing the result to
  /// [outputPath].
  void warpFile({
    required String inputPath,
    required String outputPath,
    required List<double> corners,
    bool blackWhite = false,
  }) {
    assert(corners.length == 8);
    final input = inputPath.toNativeUtf8();
    final output = outputPath.toNativeUtf8();
    final corners8 = calloc<Float>(8);
    try {
      for (var i = 0; i < 8; i++) {
        corners8[i] = corners[i];
      }
      final status = _warpFile(
        input.cast<Char>(),
        output.cast<Char>(),
        corners8,
        blackWhite ? 1 : 0,
      );
      if (status != 0) {
        throw DocumentDetectorException(status, _errorMessage());
      }
    } finally {
      calloc.free(input);
      calloc.free(output);
      calloc.free(corners8);
    }
  }

  String _errorMessage() {
    final ptr = _lastError();
    return ptr == nullptr ? 'unknown native error' : ptr.cast<Utf8>().toDartString();
  }

  static DynamicLibrary _open() {
    if (Platform.isAndroid) {
      return DynamicLibrary.open('libdocument_detector.so');
    }
    if (Platform.isIOS || Platform.isMacOS) {
      return DynamicLibrary.process();
    }
    if (Platform.isLinux) {
      return DynamicLibrary.open('libdocument_detector.so');
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }
}

class DocumentDetectorException implements Exception {
  const DocumentDetectorException(this.status, this.message);
  final int status;
  final String message;
  @override
  String toString() => 'DocumentDetectorException($status): $message';
}
