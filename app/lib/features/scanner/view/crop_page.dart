import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/constants.dart';
import '../state/providers.dart';
import '../state/scanner_view_model.dart';
import '../widgets/magnifier_painter.dart';

/// Màn chỉnh góc (như CamScanner): hiển thị ảnh chụp với 4 góc kéo-thả và
/// kính lúp phóng to khi kéo, rồi warp/crop ra ảnh chỉ-tài-liệu khi xác nhận.
///
/// View mỏng theo MVVM: mọi state/logic nằm ở [cropViewModelProvider]; các
/// nhánh rebuild được thu hẹp bằng Consumer + select để kéo góc không rebuild
/// ảnh nền full-res.
class CropPage extends ConsumerWidget {
  const CropPage({super.key, required this.request});

  /// Dữ liệu lần chụp (ảnh nguồn, góc seed, kích thước, nơi ghi kết quả).
  final CaptureRequest request;

  // Vị trí ảnh khi đặt theo BoxFit.contain trong khung [box].
  ({Offset offset, double scale}) _fit(Size box) {
    final iw = request.imgW.toDouble();
    final ih = request.imgH.toDouble();
    if (iw <= 0 || ih <= 0) return (offset: Offset.zero, scale: 1);
    final scale = math.min(box.width / iw, box.height / ih);
    final off = Offset((box.width - iw * scale) / 2, (box.height - ih * scale) / 2);
    return (offset: off, scale: scale);
  }

  Future<void> _confirm(BuildContext context, WidgetRef ref) async {
    final outPath =
        await ref.read(cropViewModelProvider(request).notifier).confirm();
    if (outPath != null && context.mounted) {
      Navigator.of(context).pop(outPath);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final provider = cropViewModelProvider(request);

    // Lỗi warp → SnackBar (một lần, rồi clear).
    ref.listen(provider.select((s) => s.errorMessage), (_, err) {
      if (err != null) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err)));
        ref.read(provider.notifier).clearError();
      }
    });

    const hit = ScannerConstants.cropHandleHitSize;
    const r = ScannerConstants.cropHandleRadius;
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
        title: const Text('Chỉnh góc tài liệu'),
      ),
      body: Column(
        children: [
          Expanded(
            child: LayoutBuilder(
              builder: (context, constraints) {
                final box = Size(constraints.maxWidth, constraints.maxHeight);
                final fit = _fit(box);
                return Stack(
                  children: [
                    // Ảnh nền full-res: nằm ngoài mọi Consumer nên KHÔNG bị
                    // rebuild trong lúc kéo góc.
                    Positioned.fill(
                      child:
                          Image.file(File(request.imagePath), fit: BoxFit.contain),
                    ),
                    // Nhánh quad + 4 handle: chỉ rebuild khi corners đổi.
                    Positioned.fill(
                      child: Consumer(
                        builder: (context, ref, _) {
                          final corners =
                              ref.watch(provider.select((s) => s.corners));
                          final vm = ref.read(provider.notifier);
                          final wpts = [
                            for (final p in corners)
                              Offset(
                                fit.offset.dx + p.dx * fit.scale,
                                fit.offset.dy + p.dy * fit.scale,
                              ),
                          ];
                          return Stack(
                            children: [
                              Positioned.fill(
                                child: CustomPaint(painter: _CropPainter(wpts)),
                              ),
                              for (int i = 0; i < 4; i++)
                                Positioned(
                                  left: wpts[i].dx - hit / 2,
                                  top: wpts[i].dy - hit / 2,
                                  child: GestureDetector(
                                    onPanStart: (_) => vm.startDrag(i),
                                    onPanUpdate: (d) =>
                                        vm.moveCorner(d, fit.scale),
                                    onPanEnd: (_) => vm.endDrag(),
                                    onPanCancel: vm.endDrag,
                                    child: Container(
                                      width: hit,
                                      height: hit,
                                      color: Colors.transparent,
                                      child: Center(
                                        child: Container(
                                          width: r * 2,
                                          height: r * 2,
                                          decoration: BoxDecoration(
                                            shape: BoxShape.circle,
                                            color: Colors.greenAccent
                                                .withValues(alpha: 0.5),
                                            border: Border.all(
                                                color: Colors.white, width: 2),
                                          ),
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                            ],
                          );
                        },
                      ),
                    ),
                    // Kính lúp: chỉ hiện khi đang kéo và ảnh đã decode xong.
                    _MagnifierOverlay(
                      request: request,
                      fitOffset: fit.offset,
                      fitScale: fit.scale,
                      viewport: box,
                    ),
                  ],
                );
              },
            ),
          ),
          Container(
            color: Colors.black,
            padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 24),
            child: Consumer(
              builder: (context, ref, _) {
                final saving = ref.watch(provider.select((s) => s.saving));
                return Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    TextButton(
                      onPressed:
                          saving ? null : () => Navigator.of(context).pop(),
                      child: const Text('Chụp lại',
                          style: TextStyle(color: Colors.white)),
                    ),
                    ElevatedButton.icon(
                      onPressed: saving ? null : () => _confirm(context, ref),
                      icon: saving
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.check),
                      label: const Text('Xác nhận'),
                    ),
                  ],
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}

/// Lớp phủ kính lúp: theo dõi góc đang kéo và vẽ vòng tròn phóng to phía trên
/// ngón tay. Tách widget riêng + RepaintBoundary để repaint liên tục khi kéo
/// không lan sang phần còn lại của màn hình.
class _MagnifierOverlay extends ConsumerWidget {
  const _MagnifierOverlay({
    required this.request,
    required this.fitOffset,
    required this.fitScale,
    required this.viewport,
  });

  final CaptureRequest request;
  final Offset fitOffset;
  final double fitScale;
  final Size viewport;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final provider = cropViewModelProvider(request);
    final dragIndex = ref.watch(provider.select((s) => s.dragIndex));
    if (dragIndex == null) return const SizedBox.shrink();

    // Ảnh decode sẵn; frame đầu tiên sau khi bắt đầu kéo có thể chưa xong —
    // khi đó tạm ẩn lúp (decode chạy nền, xong sẽ tự hiện).
    final ui.Image? image =
        ref.watch(decodedImageProvider(request.imagePath)).value;
    if (image == null) return const SizedBox.shrink();

    final corners = ref.watch(provider.select((s) => s.corners));
    final focal = corners[dragIndex];

    const d = ScannerConstants.magnifierDiameter;
    // Tâm lúp: phía trên điểm góc, kẹp trong khung hiển thị để không tràn mép.
    final screenPt = Offset(
      fitOffset.dx + focal.dx * fitScale,
      fitOffset.dy + focal.dy * fitScale,
    );
    final center = Offset(
      screenPt.dx.clamp(d / 2, math.max(d / 2, viewport.width - d / 2)),
      (screenPt.dy - ScannerConstants.magnifierOffsetY)
          .clamp(d / 2, math.max(d / 2, viewport.height - d / 2)),
    );

    return Positioned(
      left: center.dx - d / 2,
      top: center.dy - d / 2,
      child: IgnorePointer(
        child: RepaintBoundary(
          child: CustomPaint(
            size: const Size.square(d),
            painter: MagnifierPainter(
              image: image,
              focal: focal,
              corners: corners,
              magnification: ScannerConstants.magnifierZoom,
            ),
          ),
        ),
      ),
    );
  }
}

class _CropPainter extends CustomPainter {
  _CropPainter(this.pts);
  final List<Offset> pts;

  @override
  void paint(Canvas canvas, Size size) {
    final path = Path()..moveTo(pts[0].dx, pts[0].dy);
    for (int i = 1; i < pts.length; i++) {
      path.lineTo(pts[i].dx, pts[i].dy);
    }
    path.close();
    final fill = Paint()
      ..style = PaintingStyle.fill
      ..color = Colors.greenAccent.withValues(alpha: 0.12);
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = Colors.greenAccent;
    canvas.drawPath(path, fill);
    canvas.drawPath(path, stroke);
  }

  @override
  bool shouldRepaint(covariant _CropPainter old) => old.pts != pts;
}
