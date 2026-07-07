import 'dart:io';

import 'package:flutter/material.dart';

import '../../../core/constants.dart';

/// Màn xem ảnh kết quả: lấp đầy màn hình, giữ đúng tỉ lệ, cho phép pinch-zoom.
class ResultPage extends StatelessWidget {
  const ResultPage({super.key, required this.imagePath});

  final String imagePath;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Kết quả quét')),
      backgroundColor: Colors.black,
      body: SizedBox.expand(
        child: InteractiveViewer(
          maxScale: ScannerConstants.resultMaxZoom,
          child: Image.file(
            File(imagePath),
            fit: BoxFit.contain,
            filterQuality: FilterQuality.medium,
          ),
        ),
      ),
    );
  }
}
