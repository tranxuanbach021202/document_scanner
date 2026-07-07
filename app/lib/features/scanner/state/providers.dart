import 'dart:io';
import 'dart:ui' as ui;

import 'package:camera/camera.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../data/document_detector.dart';
import 'crop_state.dart';
import 'crop_view_model.dart';
import 'scanner_state.dart';
import 'scanner_view_model.dart';

/// Danh sách camera của thiết bị (load 1 lần).
final camerasProvider = FutureProvider<List<CameraDescription>>((ref) async {
  try {
    return await availableCameras();
  } catch (_) {
    return const <CameraDescription>[];
  }
});

/// Lớp data: cầu nối FFI tới thư viện native (dò biên + warp + enhance).
final documentDetectorProvider = Provider<DocumentDetector>((ref) {
  return DocumentDetector();
});

/// ViewModel của màn quét. autoDispose để khi rời màn scan (về Home) thì
/// controller camera được giải phóng, không chạy nền tốn pin.
final scannerViewModelProvider =
    NotifierProvider.autoDispose<ScannerViewModel, ScannerState>(
  ScannerViewModel.new,
);

/// Ảnh gốc decode đúng 1 lần thành [ui.Image] cho kính lúp khi kéo góc —
/// tránh re-decode file mỗi frame kéo. autoDispose giải phóng khi rời màn.
final decodedImageProvider =
    FutureProvider.autoDispose.family<ui.Image, String>((ref, path) async {
  final bytes = await File(path).readAsBytes();
  final codec = await ui.instantiateImageCodec(bytes);
  final frame = await codec.getNextFrame();
  ref.onDispose(frame.image.dispose);
  return frame.image;
});

/// ViewModel màn chỉnh góc; family theo [CaptureRequest] của lần chụp,
/// autoDispose để dọn state khi rời màn.
final cropViewModelProvider = NotifierProvider.autoDispose
    .family<CropViewModel, CropState, CaptureRequest>(CropViewModel.new);
