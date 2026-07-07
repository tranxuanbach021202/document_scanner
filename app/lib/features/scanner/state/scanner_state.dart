import 'package:camera/camera.dart';
import 'package:flutter/widgets.dart';

import '../../../core/constants.dart';

/// Trạng thái bất biến của màn quét (Model cho View qua ViewModel).
class ScannerState {
  const ScannerState({
    this.controller,
    this.isInitializing = true,
    this.errorMessage,
    this.quad,
    this.score = 0,
    this.imgW = 0,
    this.imgH = 0,
    this.sensorOrientation = 90,
    this.isCapturing = false,
    this.capturedPaths = const [],
  });

  /// Controller camera (null khi chưa khởi tạo xong).
  final CameraController? controller;
  final bool isInitializing;
  final String? errorMessage;

  /// 4 góc tài liệu trong hệ toạ độ khung hình nguồn (null nếu chưa dò thấy).
  final List<Offset>? quad;
  final double score;
  final int imgW;
  final int imgH;
  final int sensorOrientation;
  final bool isCapturing;

  /// Đường dẫn các ảnh đã quét trong phiên.
  final List<String> capturedPaths;

  /// Đã canh thẳng (đủ điểm) để hiển thị khung xanh + gợi ý chụp.
  bool get aligned => quad != null && score > ScannerConstants.alignedScoreThreshold;

  ScannerState copyWith({
    CameraController? controller,
    bool? isInitializing,
    String? errorMessage,
    bool clearError = false,
    List<Offset>? quad,
    bool clearQuad = false,
    double? score,
    int? imgW,
    int? imgH,
    int? sensorOrientation,
    bool? isCapturing,
    List<String>? capturedPaths,
  }) {
    return ScannerState(
      controller: controller ?? this.controller,
      isInitializing: isInitializing ?? this.isInitializing,
      errorMessage: clearError ? null : (errorMessage ?? this.errorMessage),
      quad: clearQuad ? null : (quad ?? this.quad),
      score: score ?? this.score,
      imgW: imgW ?? this.imgW,
      imgH: imgH ?? this.imgH,
      sensorOrientation: sensorOrientation ?? this.sensorOrientation,
      isCapturing: isCapturing ?? this.isCapturing,
      capturedPaths: capturedPaths ?? this.capturedPaths,
    );
  }
}
