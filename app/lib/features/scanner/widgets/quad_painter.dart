import 'package:flutter/material.dart';

/// Vẽ tứ giác detect (góc preview) lên khung xem trực tiếp, có bù phần xoay
/// 1/4 vòng theo hướng cảm biến để khung khớp với ảnh người dùng nhìn thấy.
class QuadPainter extends CustomPainter {
  QuadPainter({
    required this.quad,
    required this.imgW,
    required this.imgH,
    required this.sensorOrientation,
    required this.color,
  });

  final List<Offset> quad;
  final int imgW;
  final int imgH;
  final int sensorOrientation;
  final Color color;

  Offset _map(Offset p, Size size) {
    double rx, ry, rw, rh;
    switch (sensorOrientation) {
      case 90:
        rx = imgH - p.dy;
        ry = p.dx;
        rw = imgH.toDouble();
        rh = imgW.toDouble();
        break;
      case 270:
        rx = p.dy;
        ry = imgW - p.dx;
        rw = imgH.toDouble();
        rh = imgW.toDouble();
        break;
      case 180:
        rx = imgW - p.dx;
        ry = imgH - p.dy;
        rw = imgW.toDouble();
        rh = imgH.toDouble();
        break;
      default:
        rx = p.dx;
        ry = p.dy;
        rw = imgW.toDouble();
        rh = imgH.toDouble();
    }
    return Offset(rx / rw * size.width, ry / rh * size.height);
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (imgW == 0 || imgH == 0) return;
    final pts = [for (final p in quad) _map(p, size)];
    final path = Path()..moveTo(pts[0].dx, pts[0].dy);
    for (int i = 1; i < pts.length; i++) {
      path.lineTo(pts[i].dx, pts[i].dy);
    }
    path.close();

    final fill = Paint()
      ..style = PaintingStyle.fill
      ..color = color.withValues(alpha: 0.15);
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..color = color;
    canvas.drawPath(path, fill);
    canvas.drawPath(path, stroke);
    for (final p in pts) {
      canvas.drawCircle(p, 6, Paint()..color = color);
    }
  }

  @override
  bool shouldRepaint(covariant QuadPainter old) =>
      old.quad != quad || old.color != color;
}
