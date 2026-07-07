import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

class DocumentDetection {
  const DocumentDetection({required this.corners, required this.score});

  final List<double> corners;
  final double score;
}

class DocumentDetectorFfi {
  DocumentDetectorFfi({DynamicLibrary? library})
    : _library = library ?? _openLibrary() {
    _processFile = _library
        .lookupFunction<_ProcessFileNative, _ProcessFileDart>(
          'docdet_process_file',
        );
    _detectFile = _library.lookupFunction<_DetectFileNative, _DetectFileDart>(
      'docdet_detect_file',
    );
    _lastError = _library.lookupFunction<_LastErrorNative, _LastErrorDart>(
      'docdet_last_error',
    );
  }

  final DynamicLibrary _library;
  late final _ProcessFileDart _processFile;
  late final _DetectFileDart _detectFile;
  late final _LastErrorDart _lastError;

  DocumentDetection detectFile(String inputPath) {
    final input = inputPath.toNativeUtf8();
    final corners = calloc<Float>(8);
    final score = calloc<Double>();

    try {
      final status = _detectFile(input.cast<Char>(), nullptr, corners, score);
      _throwIfFailed(status);
      return DocumentDetection(
        corners: List<double>.generate(8, (index) => corners[index]),
        score: score.value,
      );
    } finally {
      calloc.free(input);
      calloc.free(corners);
      calloc.free(score);
    }
  }

  DocumentDetection processFile({
    required String inputPath,
    required String outputPath,
    bool blackWhite = false,
  }) {
    final input = inputPath.toNativeUtf8();
    final output = outputPath.toNativeUtf8();
    final corners = calloc<Float>(8);
    final score = calloc<Double>();

    try {
      final status = _processFile(
        input.cast<Char>(),
        output.cast<Char>(),
        blackWhite ? 1 : 0,
        nullptr,
        corners,
        score,
      );
      _throwIfFailed(status);
      return DocumentDetection(
        corners: List<double>.generate(8, (index) => corners[index]),
        score: score.value,
      );
    } finally {
      calloc.free(input);
      calloc.free(output);
      calloc.free(corners);
      calloc.free(score);
    }
  }

  void _throwIfFailed(int status) {
    if (status == 0) {
      return;
    }

    final messagePtr = _lastError();
    final message = messagePtr == nullptr
        ? 'Unknown native error'
        : messagePtr.cast<Utf8>().toDartString();
    throw DocumentDetectorException(status, message);
  }

  static DynamicLibrary _openLibrary() {
    if (Platform.isAndroid) {
      return DynamicLibrary.open('libdocument_detector.so');
    }
    if (Platform.isIOS) {
      return DynamicLibrary.process();
    }
    if (Platform.isMacOS) {
      return DynamicLibrary.open('libdocument_detector.dylib');
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

typedef _ProcessFileNative =
    Int32 Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      Int32 blackWhite,
      Pointer<Void> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _ProcessFileDart =
    int Function(
      Pointer<Char> inputPath,
      Pointer<Char> outputPath,
      int blackWhite,
      Pointer<Void> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _DetectFileNative =
    Int32 Function(
      Pointer<Char> inputPath,
      Pointer<Void> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _DetectFileDart =
    int Function(
      Pointer<Char> inputPath,
      Pointer<Void> options,
      Pointer<Float> outCorners8,
      Pointer<Double> outScore,
    );

typedef _LastErrorNative = Pointer<Char> Function();
typedef _LastErrorDart = Pointer<Char> Function();
