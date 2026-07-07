import 'package:camera/camera.dart';

/// Các tham số có thể tinh chỉnh của luồng quét tài liệu.
///
/// Gom về một nơi để dễ chỉnh mà không phải lần tìm trong code UI/ViewModel.
class ScannerConstants {
  ScannerConstants._();

  // ── Nhận diện ở chế độ xem trực tiếp (preview) ──────────────────────────────

  /// Ngưỡng điểm để báo "đã canh thẳng" (khung overlay chuyển sang xanh).
  /// Tài liệu nhận đúng thường đạt ~1.6–2.1; đặt dưới dải đó một chút.
  static const double alignedScoreThreshold = 1.5;

  /// Khoảng cách tối thiểu giữa 2 lần dò trên preview (mili-giây).
  /// Throttle này giữ preview mượt khi camera chạy ở độ phân giải cao (4K).
  static const int detectThrottleMs = 150;

  /// Hệ số làm mượt EMA cho 4 góc giữa các khung hình (0..1).
  /// Càng nhỏ càng mượt/ì; càng lớn càng bám sát khung mới nhưng dễ rung.
  static const double quadSmoothing = 0.5;

  // ── Camera ──────────────────────────────────────────────────────────────────

  /// Độ phân giải chụp (ưu tiên từ cao xuống thấp; máy không hỗ trợ thì lùi dần).
  /// Chụp nét để ảnh crop rõ — phần dò vẫn tự downscale về [maxDetectSize] bên
  /// native nên không ảnh hưởng tốc độ preview.
  static const List<ResolutionPreset> capturePresets = [
    ResolutionPreset.ultraHigh, // ~4K (8MP)
    ResolutionPreset.veryHigh, // 1080p
    ResolutionPreset.high, // 720p
  ];

  // ── Màn hình chỉnh góc (crop) ───────────────────────────────────────────────

  /// Bán kính chấm tròn của 1 góc kéo-thả (logic pixel).
  static const double cropHandleRadius = 11;

  /// Kích thước vùng chạm (bắt sự kiện) của mỗi góc — to hơn chấm để dễ kéo.
  static const double cropHandleHitSize = 44;

  // ── Kính lúp khi kéo góc (magnifier) ────────────────────────────────────────

  /// Đường kính vòng tròn kính lúp hiện khi kéo 1 góc (logic pixel).
  static const double magnifierDiameter = 120;

  /// Hệ số phóng đại ảnh gốc bên trong kính lúp.
  static const double magnifierZoom = 2.5;

  /// Khoảng lệch trục Y giữa tâm kính lúp và điểm góc đang kéo — đặt kính lúp
  /// phía trên ngón tay để ngón tay không che khuất vùng đang xem.
  static const double magnifierOffsetY = 90;

  // ── Màn hình kết quả ────────────────────────────────────────────────────────

  /// Mức phóng to tối đa khi pinch-zoom ảnh kết quả.
  static const double resultMaxZoom = 5;
}
