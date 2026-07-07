import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';

import '../../../core/constants.dart';
import '../../../data/document_detector.dart' as nd;
import 'providers.dart';
import 'scanner_state.dart';

/// Dữ liệu cần thiết để mở màn chỉnh góc sau khi chụp.
class CaptureRequest {
  const CaptureRequest({
    required this.imagePath,
    required this.corners,
    required this.imgW,
    required this.imgH,
    required this.outputPath,
  });

  /// Ảnh chụp đã chuẩn hoá (upright, full-res) dùng để hiển thị + warp.
  final String imagePath;

  /// 4 góc seed (8 giá trị) trong hệ pixel của [imagePath].
  final List<double> corners;
  final int imgW;
  final int imgH;

  /// Nơi ghi ảnh kết quả sau khi crop/warp.
  final String outputPath;

  // Equality theo cặp đường dẫn (đủ định danh 1 lần chụp) để dùng làm
  // tham số family của cropViewModelProvider mà không tạo provider trùng.
  @override
  bool operator ==(Object other) =>
      other is CaptureRequest &&
      other.imagePath == imagePath &&
      other.outputPath == outputPath;

  @override
  int get hashCode => Object.hash(imagePath, outputPath);
}

/// ViewModel màn quét: quản lý vòng đời camera, dò biên ở preview và chụp.
class ScannerViewModel extends Notifier<ScannerState> {
  CameraController? _controller;

  nd.DocumentDetector get _detector => ref.read(documentDetectorProvider);

  @override
  ScannerState build() {
    ref.onDispose(() {
      _controller?.dispose();
      _controller = null;
    });
    _init();
    return const ScannerState();
  }

  Future<void> _init() async {
    final cameras = await ref.read(camerasProvider.future);
    if (cameras.isEmpty) {
      state = state.copyWith(
        isInitializing: false,
        errorMessage: 'Không tìm thấy camera',
      );
      return;
    }
    final back = cameras.firstWhere(
      (c) => c.lensDirection == CameraLensDirection.back,
      orElse: () => cameras.first,
    );
    final imageFormat = Platform.isAndroid
        ? ImageFormatGroup.yuv420
        : ImageFormatGroup.bgra8888;

    // Thử lần lượt các preset (cao -> thấp) để chụp nét nhưng vẫn mở được camera.
    CameraController? controller;
    for (final preset in ScannerConstants.capturePresets) {
      final c = CameraController(
        back,
        preset,
        enableAudio: false,
        imageFormatGroup: imageFormat,
      );
      try {
        await c.initialize();
        controller = c;
        break;
      } catch (_) {
        await c.dispose();
      }
    }
    if (controller == null) {
      state = state.copyWith(
        isInitializing: false,
        errorMessage: 'Không mở được camera',
      );
      return;
    }

    // Flow kiểu CamScanner: KHÔNG dò biên khi đang quay preview (không stream
    // ảnh) — preview mượt tối đa. Chỉ khi nhấn chụp mới detect trên ảnh chụp
    // rồi mở màn chỉnh góc.
    _controller = controller;
    state = state.copyWith(
      controller: controller,
      isInitializing: false,
      sensorOrientation: back.sensorOrientation,
    );
  }

  /// Chụp ảnh: lấy ảnh full-res, chuẩn hoá + dò góc seed (nhẹ).
  /// Trả về dữ liệu để View mở màn chỉnh góc, hoặc null nếu lỗi.
  Future<CaptureRequest?> capture() async {
    final controller = _controller;
    if (controller == null || state.isCapturing) return null;
    state = state.copyWith(isCapturing: true, clearError: true);
    try {
      final shot = await controller.takePicture();
      return await _prepareFromFile(shot.path);
    } catch (e) {
      state = state.copyWith(errorMessage: 'Không xử lý được ảnh: $e');
      return null;
    }
  }

  /// Import ảnh có sẵn từ thư viện: chuẩn hoá + dò góc seed như khi chụp.
  /// Trả về null nếu người dùng huỷ chọn hoặc gặp lỗi.
  Future<CaptureRequest?> importFromGallery() async {
    if (state.isCapturing) return null;
    final XFile? picked;
    try {
      picked = await ImagePicker().pickImage(source: ImageSource.gallery);
    } catch (e) {
      state = state.copyWith(errorMessage: 'Không chọn được ảnh: $e');
      return null;
    }
    if (picked == null) return null; // người dùng huỷ
    state = state.copyWith(isCapturing: true, clearError: true);
    try {
      return await _prepareFromFile(picked.path);
    } catch (e) {
      state = state.copyWith(errorMessage: 'Không xử lý được ảnh: $e');
      return null;
    }
  }

  /// Chuẩn hoá ảnh [inputPath] (upright, full-res) + dò góc seed (nhẹ, VIDEO)
  /// rồi đóng gói thành [CaptureRequest] để mở màn chỉnh góc.
  Future<CaptureRequest> _prepareFromFile(String inputPath) async {
    final dir = await getTemporaryDirectory();
    final ts = DateTime.now().millisecondsSinceEpoch;
    final normalized = '${dir.path}/norm_$ts.jpg';
    // Seed chỉ khởi tạo cho màn chỉnh góc nên dùng detect VIDEO (nhẹ).
    final seed = _detector.prepareCapture(
      inputPath: inputPath,
      normalizedPath: normalized,
      mode: nd.DetectMode.video,
    );
    return CaptureRequest(
      imagePath: normalized,
      corners: seed.corners,
      imgW: seed.imgW,
      imgH: seed.imgH,
      outputPath: '${dir.path}/scan_$ts.jpg',
    );
  }

  /// Mở lại trạng thái chụp sau khi rời màn chỉnh góc (preview tự chạy tiếp).
  Future<void> resumePreview() async {
    state = state.copyWith(isCapturing: false);
  }

  /// Thêm 1 ảnh đã quét vào danh sách.
  void addCaptured(String path) {
    state = state.copyWith(capturedPaths: [...state.capturedPaths, path]);
  }
}
