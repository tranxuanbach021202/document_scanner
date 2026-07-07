import 'package:flutter/widgets.dart';

/// Trạng thái bất biến của màn chỉnh góc (Model cho View qua ViewModel).
class CropState {
  const CropState({
    this.corners = const [],
    this.dragIndex,
    this.saving = false,
    this.errorMessage,
  });

  /// 4 góc tài liệu trong hệ pixel của ảnh gốc.
  final List<Offset> corners;

  /// Chỉ số góc đang được kéo (null = không kéo → ẩn kính lúp).
  final int? dragIndex;

  /// Đang warp/ghi ảnh kết quả (disable nút bấm).
  final bool saving;

  /// Lỗi khi warp (hiển thị SnackBar rồi clear).
  final String? errorMessage;

  CropState copyWith({
    List<Offset>? corners,
    int? dragIndex,
    bool clearDrag = false,
    bool? saving,
    String? errorMessage,
    bool clearError = false,
  }) {
    return CropState(
      corners: corners ?? this.corners,
      dragIndex: clearDrag ? null : (dragIndex ?? this.dragIndex),
      saving: saving ?? this.saving,
      errorMessage: clearError ? null : (errorMessage ?? this.errorMessage),
    );
  }
}
