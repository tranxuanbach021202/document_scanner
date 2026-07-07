import 'package:flutter/gestures.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'crop_state.dart';
import 'providers.dart';
import 'scanner_view_model.dart';

/// ViewModel màn chỉnh góc: giữ vị trí 4 góc, trạng thái kéo (cho kính lúp)
/// và thao tác warp khi xác nhận. Nhận [CaptureRequest] qua family arg.
class CropViewModel extends Notifier<CropState> {
  CropViewModel(this.arg);

  /// Dữ liệu lần chụp: ảnh nguồn, góc seed, kích thước ảnh, nơi ghi kết quả.
  final CaptureRequest arg;

  @override
  CropState build() {
    return CropState(
      corners: [
        for (int i = 0; i < 4; i++)
          Offset(arg.corners[i * 2], arg.corners[i * 2 + 1]),
      ],
    );
  }

  /// Bắt đầu kéo góc [i] — hiện kính lúp tại góc đó.
  void startDrag(int i) {
    state = state.copyWith(dragIndex: i);
  }

  /// Di chuyển góc đang kéo theo delta màn hình; [fitScale] là tỉ lệ
  /// BoxFit.contain để quy đổi về hệ pixel ảnh. Kẹp trong biên ảnh.
  void moveCorner(DragUpdateDetails d, double fitScale) {
    final i = state.dragIndex;
    if (i == null || fitScale <= 0) return;
    final cur = state.corners;
    final next = List<Offset>.of(cur);
    next[i] = Offset(
      (cur[i].dx + d.delta.dx / fitScale).clamp(0.0, arg.imgW - 1.0),
      (cur[i].dy + d.delta.dy / fitScale).clamp(0.0, arg.imgH - 1.0),
    );
    state = state.copyWith(corners: next);
  }

  /// Kết thúc/huỷ kéo — ẩn kính lúp.
  void endDrag() {
    state = state.copyWith(clearDrag: true);
  }

  /// Warp/crop theo 4 góc hiện tại. Trả về đường dẫn ảnh kết quả, hoặc null
  /// nếu lỗi (lỗi được đưa vào [CropState.errorMessage]).
  Future<String?> confirm() async {
    state = state.copyWith(saving: true, clearError: true);
    try {
      final corners8 = <double>[
        for (final p in state.corners) ...[p.dx, p.dy],
      ];
      ref.read(documentDetectorProvider).warpFile(
            inputPath: arg.imagePath,
            outputPath: arg.outputPath,
            corners: corners8,
          );
      return arg.outputPath;
    } catch (e) {
      state = state.copyWith(saving: false, errorMessage: 'Crop lỗi: $e');
      return null;
    }
  }

  /// Đã hiển thị lỗi xong — xoá để không hiện lại.
  void clearError() {
    state = state.copyWith(clearError: true);
  }
}
