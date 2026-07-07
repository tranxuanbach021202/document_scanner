import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// Vẽ kính lúp tròn phóng to vùng ảnh quanh góc đang kéo.
///
/// Lấy mẫu trực tiếp từ [image] (ảnh gốc full-res đã decode sẵn) bằng
/// drawImageRect nên vùng phóng to nét tối đa và không tốn decode mỗi frame.
/// Vẽ kèm các cạnh quad đi qua vùng nhìn + dấu chữ thập tại đúng vị trí góc.
class MagnifierPainter extends CustomPainter {
  MagnifierPainter({
    required this.image,
    required this.focal,
    required this.corners,
    required this.magnification,
  });

  /// Ảnh gốc đã decode (hệ pixel ảnh).
  final ui.Image image;

  /// Điểm góc đang kéo, hệ pixel ảnh.
  final Offset focal;

  /// 4 góc hiện tại (hệ pixel ảnh) — để vẽ viền quad bên trong kính lúp.
  final List<Offset> corners;

  /// Hệ số phóng đại ảnh gốc bên trong kính lúp.
  final double magnification;

  @override
  void paint(Canvas canvas, Size size) {
    final diameter = size.shortestSide;
    final radius = diameter / 2;
    final dstCenter = Offset(radius, radius);
    final dstRect = Rect.fromCircle(center: dstCenter, radius: radius);

    canvas.save();
    canvas.clipPath(Path()..addOval(dstRect));

    // Nền tối cho phần (nếu có) nằm ngoài biên ảnh.
    canvas.drawRect(dstRect, Paint()..color = Colors.black);

    // Vùng nguồn quanh điểm góc, kẹp trong biên ảnh để không lấy mẫu ra ngoài
    // (đặc biệt khi góc nằm sát mép/góc ảnh).
    final srcSize = diameter / magnification;
    double clampCenter(double c, double max) {
      final half = srcSize / 2;
      if (max <= srcSize) return max / 2;
      return c.clamp(half, max - half);
    }

    final srcCenter = Offset(
      clampCenter(focal.dx, image.width.toDouble()),
      clampCenter(focal.dy, image.height.toDouble()),
    );
    final srcRect =
        Rect.fromCenter(center: srcCenter, width: srcSize, height: srcSize);
    canvas.drawImageRect(
      image,
      srcRect,
      dstRect,
      Paint()..filterQuality = FilterQuality.medium,
    );

    // Map từ hệ pixel ảnh sang toạ độ bên trong kính lúp.
    Offset toLoupe(Offset p) => Offset(
          (p.dx - srcRect.left) / srcSize * diameter,
          (p.dy - srcRect.top) / srcSize * diameter,
        );

    // Viền quad đi qua vùng nhìn — thấy được mép crop ngay trong kính lúp.
    final pts = [for (final p in corners) toLoupe(p)];
    final quadPath = Path()..moveTo(pts[0].dx, pts[0].dy);
    for (int i = 1; i < pts.length; i++) {
      quadPath.lineTo(pts[i].dx, pts[i].dy);
    }
    quadPath.close();
    canvas.drawPath(
      quadPath,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = Colors.greenAccent,
    );

    // Dấu chữ thập tại vị trí thực của góc (bù lệch khi srcRect bị kẹp ở mép).
    final mark = toLoupe(focal);
    final crossPaint = Paint()
      ..color = Colors.redAccent
      ..strokeWidth = 1.5;
    const half = 9.0;
    canvas.drawLine(
        mark - const Offset(half, 0), mark + const Offset(half, 0), crossPaint);
    canvas.drawLine(
        mark - const Offset(0, half), mark + const Offset(0, half), crossPaint);

    canvas.restore();

    // Viền tròn của kính lúp.
    canvas.drawCircle(
      dstCenter,
      radius - 1,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = Colors.white,
    );
  }

  @override
  bool shouldRepaint(covariant MagnifierPainter old) =>
      old.image != image ||
      old.focal != focal ||
      old.corners != corners ||
      old.magnification != magnification;
}
