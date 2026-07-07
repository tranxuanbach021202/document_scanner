import 'package:flutter/material.dart';

import '../../scanner/view/scanner_page.dart';

/// Màn hình ngoài (Home): người dùng bấm nút để mở màn quét tài liệu.
class HomePage extends StatelessWidget {
  const HomePage({super.key});

  void _openScanner(BuildContext context) {
    Navigator.of(context).push(
      MaterialPageRoute(builder: (_) => const ScannerPage()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Scaffold(
      appBar: AppBar(title: const Text('DocScan')),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 32),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.document_scanner_outlined,
                  size: 96, color: scheme.primary),
              const SizedBox(height: 20),
              Text(
                'Quét tài liệu',
                style: Theme.of(context).textTheme.headlineSmall,
              ),
              const SizedBox(height: 8),
              Text(
                'Chụp, cắt và trải phẳng tài liệu thành ảnh sắc nét.',
                textAlign: TextAlign.center,
                style: Theme.of(context)
                    .textTheme
                    .bodyMedium
                    ?.copyWith(color: scheme.onSurfaceVariant),
              ),
              const SizedBox(height: 36),
              FilledButton.icon(
                onPressed: () => _openScanner(context),
                icon: const Icon(Icons.camera_alt),
                label: const Text('Bắt đầu quét'),
                style: FilledButton.styleFrom(
                  padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 16),
                  textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
