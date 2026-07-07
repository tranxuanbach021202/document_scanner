import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../state/providers.dart';
import '../state/scanner_view_model.dart';
import 'crop_page.dart';
import 'result_page.dart';

/// View màn quét: chỉ lo hiển thị + điều hướng; mọi logic ở ScannerViewModel.
class ScannerPage extends ConsumerStatefulWidget {
  const ScannerPage({super.key});

  @override
  ConsumerState<ScannerPage> createState() => _ScannerPageState();
}

class _ScannerPageState extends ConsumerState<ScannerPage> {
  Future<void> _onCapture() => _handleRequest(
        () => ref.read(scannerViewModelProvider.notifier).capture(),
      );

  Future<void> _onImport() => _handleRequest(
        () => ref.read(scannerViewModelProvider.notifier).importFromGallery(),
      );

  /// Chạy [produce] (chụp hoặc import), rồi mở màn chỉnh góc với kết quả.
  Future<void> _handleRequest(
    Future<CaptureRequest?> Function() produce,
  ) async {
    final vm = ref.read(scannerViewModelProvider.notifier);
    final request = await produce();
    if (!mounted) return;
    if (request != null) {
      final outPath = await Navigator.of(context).push<String>(
        MaterialPageRoute(
          builder: (_) => CropPage(request: request),
        ),
      );
      if (outPath != null) vm.addCaptured(outPath);
    } else if (mounted) {
      final err = ref.read(scannerViewModelProvider).errorMessage;
      if (err != null) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err)));
      }
    }
    await vm.resumePreview();
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(scannerViewModelProvider);
    final controller = state.controller;

    if (state.errorMessage != null && controller == null) {
      return Scaffold(
        backgroundColor: Colors.black,
        body: Center(
          child: Text(
            state.errorMessage!,
            style: const TextStyle(color: Colors.white),
          ),
        ),
      );
    }
    if (controller == null || !controller.value.isInitialized) {
      return const Scaffold(
        backgroundColor: Colors.black,
        body: Center(child: CircularProgressIndicator()),
      );
    }

    // Flow kiểu CamScanner: preview trơn, không vẽ khung detect trực tiếp.
    // Việc dò biên chỉ chạy sau khi nhấn chụp (rồi vào màn chỉnh góc).
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Column(
          children: [
            Expanded(
              child: Center(
                child: AspectRatio(
                  aspectRatio: 1 / controller.value.aspectRatio,
                  child: CameraPreview(controller),
                ),
              ),
            ),
            _BottomBar(
              capturedPaths: state.capturedPaths,
              capturing: state.isCapturing,
              onCapture: _onCapture,
              onImport: _onImport,
            ),
          ],
        ),
      ),
    );
  }
}

/// Thanh dưới: dải thumbnail + gợi ý + nút chụp.
class _BottomBar extends StatelessWidget {
  const _BottomBar({
    required this.capturedPaths,
    required this.capturing,
    required this.onCapture,
    required this.onImport,
  });

  final List<String> capturedPaths;
  final bool capturing;
  final Future<void> Function() onCapture;
  final Future<void> Function() onImport;

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Colors.black,
      padding: const EdgeInsets.symmetric(vertical: 16),
      child: Column(
        children: [
          if (capturedPaths.isNotEmpty)
            SizedBox(
              height: 90,
              child: ListView.builder(
                scrollDirection: Axis.horizontal,
                padding: const EdgeInsets.symmetric(horizontal: 12),
                itemCount: capturedPaths.length,
                itemBuilder: (context, i) {
                  final isLast = i == capturedPaths.length - 1;
                  return GestureDetector(
                    onTap: () => Navigator.of(context).push(
                      MaterialPageRoute(
                        builder: (_) => ResultPage(imagePath: capturedPaths[i]),
                      ),
                    ),
                    child: Container(
                      width: 70,
                      height: 70,
                      margin: const EdgeInsets.only(right: 8),
                      decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(
                          color: isLast ? Colors.greenAccent : Colors.white38,
                          width: isLast ? 2 : 1,
                        ),
                      ),
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(7),
                        child: Image.file(File(capturedPaths[i]), fit: BoxFit.cover),
                      ),
                    ),
                  );
                },
              ),
            ),
          if (capturedPaths.isNotEmpty) const SizedBox(height: 8),
          const Text(
            'Đưa tài liệu vào khung hình rồi bấm chụp',
            style: TextStyle(color: Colors.white),
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              // Chừa khoảng bên trái đối xứng để nút chụp luôn nằm chính giữa.
              const Expanded(child: SizedBox()),
              Stack(
                clipBehavior: Clip.none,
                children: [
                  GestureDetector(
                    onTap: capturing ? null : onCapture,
                    child: Container(
                      width: 70,
                      height: 70,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: capturing ? Colors.grey : Colors.white,
                        border: Border.all(color: Colors.white24, width: 4),
                      ),
                      child: capturing
                          ? const Padding(
                              padding: EdgeInsets.all(18),
                              child: CircularProgressIndicator(strokeWidth: 3),
                            )
                          : const Icon(Icons.camera_alt, color: Colors.black),
                    ),
                  ),
                  if (capturedPaths.isNotEmpty)
                    Positioned(
                      top: -4,
                      right: -4,
                      child: Container(
                        padding: const EdgeInsets.all(4),
                        decoration: const BoxDecoration(
                          color: Colors.greenAccent,
                          shape: BoxShape.circle,
                        ),
                        child: Text(
                          '${capturedPaths.length}',
                          style: const TextStyle(
                            color: Colors.black,
                            fontSize: 11,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              // Nút import ảnh từ thư viện, nằm bên phải nút chụp.
              Expanded(
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: Padding(
                    padding: const EdgeInsets.only(left: 28),
                    child: GestureDetector(
                      onTap: capturing ? null : onImport,
                      child: Container(
                        width: 54,
                        height: 54,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: Colors.white12,
                          border: Border.all(color: Colors.white24, width: 2),
                        ),
                        child: Icon(
                          Icons.photo_library_outlined,
                          color: capturing ? Colors.white38 : Colors.white,
                          size: 26,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
