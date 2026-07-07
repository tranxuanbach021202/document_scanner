# Riverpod v3 + MVVM — Quy tắc & thực hành tốt nhất

> Tài liệu cho người mới, dùng ví dụ **thật** từ chính app này
> (`lib/features/scanner/…`). Riverpod bản 3.x, kiểu **manual** (không codegen).

---

## 1. Tư duy cốt lõi: Provider là "định nghĩa", ref là "cổng truy cập"

- **Provider** = một biến toàn cục **bất biến** mô tả *cách tạo ra và quản lý* một giá trị
  (object, state, future…). Nó KHÔNG phải là giá trị — nó là "công thức".
- Giá trị thật chỉ sinh ra khi có ai đó **đọc** provider qua `ref`.
- Mọi thứ nối với nhau bằng `ref`: widget đọc provider, provider đọc provider khác.

```dart
// "Công thức": làm sao có được DocumentDetector (lib/features/scanner/state/providers.dart)
final documentDetectorProvider = Provider<DocumentDetector>((ref) {
  return DocumentDetector();
});
```

Lợi ích so với biến global/singleton tự chế:
- Lazy (chỉ tạo khi cần), cache, tự huỷ (autoDispose).
- Test dễ: override provider bằng mock trong `ProviderScope(overrides: [...])`.
- Widget tự rebuild khi giá trị đổi.

---

## 2. Các loại provider hay dùng (v3)

| Loại | Dùng khi | Ví dụ trong app |
|---|---|---|
| `Provider<T>` | Object/service **không đổi** (repository, FFI, client) | `documentDetectorProvider` |
| `FutureProvider<T>` | Giá trị lấy **bất đồng bộ 1 lần** (config, danh sách camera) | `camerasProvider` |
| `StreamProvider<T>` | Nguồn dữ liệu dạng stream (socket, firestore…) | (chưa dùng) |
| `NotifierProvider<N, S>` | **State thay đổi được + logic** — chính là ViewModel | `scannerViewModelProvider` |
| `AsyncNotifierProvider` | Như trên nhưng state khởi tạo async (`build()` trả `Future`) | (chưa dùng) |

Quy tắc chọn nhanh:
- Chỉ cần "phát" 1 object → `Provider`.
- Cần load async rồi hiển thị → `FutureProvider` (UI nhận `AsyncValue`).
- Cần **sửa state + có method** (tap, submit, capture…) → `Notifier` + `NotifierProvider`.

```dart
// ViewModel (lib/features/scanner/state/scanner_view_model.dart)
class ScannerViewModel extends Notifier<ScannerState> {
  @override
  ScannerState build() {              // build() trả state ban đầu
    ref.onDispose(() { ... });        // dọn dẹp khi provider bị huỷ
    _init();                          // side-effect khởi động
    return const ScannerState();
  }

  Future<CaptureRequest?> capture() async { ... } // method cho View gọi
}

final scannerViewModelProvider =
    NotifierProvider.autoDispose<ScannerViewModel, ScannerState>(ScannerViewModel.new);
```

---

## 3. `watch` vs `read` vs `listen` — quy tắc quan trọng nhất

| API | Ý nghĩa | Dùng ở đâu |
|---|---|---|
| `ref.watch(p)` | Đọc **và đăng ký rebuild** khi p đổi | Trong `build()` của widget/provider |
| `ref.read(p)` | Đọc **1 lần**, không đăng ký | Trong callback (onTap, onPressed), trong method của Notifier |
| `ref.listen(p, cb)` | Không rebuild, chạy callback khi đổi | Side-effect: hiện SnackBar, điều hướng |

```dart
// ĐÚNG (lib/features/scanner/view/scanner_page.dart)
Widget build(BuildContext context) {
  final state = ref.watch(scannerViewModelProvider);   // watch trong build
  ...
}

Future<void> _onCapture() async {
  final vm = ref.read(scannerViewModelProvider.notifier); // read trong callback
  await vm.capture();
}
```

**Cấm kỵ:**
- ❌ `ref.watch` trong `onPressed`/callback → đăng ký thừa, hành vi khó lường.
- ❌ `ref.read` trong `build()` để "né rebuild" → UI không cập nhật khi state đổi.
- Đọc **state** = `ref.watch(provider)`; gọi **method** = `ref.read(provider.notifier)`.

---

## 4. State bất biến + `copyWith`

State của Notifier phải là **immutable**: mọi field `final`, thay đổi bằng cách
**gán state mới**, không mutate object cũ.

```dart
// lib/features/scanner/state/scanner_state.dart
class ScannerState {
  const ScannerState({this.isCapturing = false, this.capturedPaths = const [], ...});
  final bool isCapturing;
  final List<String> capturedPaths;

  ScannerState copyWith({bool? isCapturing, List<String>? capturedPaths, ...}) =>
      ScannerState(
        isCapturing: isCapturing ?? this.isCapturing,
        capturedPaths: capturedPaths ?? this.capturedPaths,
      );
}

// Trong Notifier: LUÔN gán state mới
state = state.copyWith(isCapturing: true);
state = state.copyWith(capturedPaths: [...state.capturedPaths, path]); // list mới!
```

Vì sao: Riverpod so sánh state cũ/mới để quyết định rebuild. Mutate tại chỗ →
không phát hiện thay đổi → UI không cập nhật.

Mẹo trong app: field nullable muốn "xoá" phải thêm cờ riêng
(`copyWith(clearQuad: true)`) vì `copyWith(quad: null)` bị `??` nuốt mất.

---

## 5. `autoDispose` và `family`

- **`.autoDispose`**: provider tự huỷ khi không còn ai watch. Trong app:
  `scannerViewModelProvider` là autoDispose → rời màn scan là camera được
  `dispose()` (qua `ref.onDispose`), không chạy nền tốn pin. **Mặc định nên dùng
  autoDispose cho state gắn với 1 màn hình.** Chỉ bỏ khi state cần sống toàn app.
- **`.family`**: provider có tham số, ví dụ `detailProvider(documentId)`. Mỗi bộ
  tham số là 1 instance riêng. Dùng khi màn hình cần dữ liệu theo id.

---

## 6. Widget: Consumer các loại

| Widget | Thay cho | Khi nào |
|---|---|---|
| `ConsumerWidget` | `StatelessWidget` | Chỉ cần `ref` trong build |
| `ConsumerStatefulWidget` + `ConsumerState` | `StatefulWidget` | Cần cả `ref` lẫn state cục bộ UI (animation, controller, vị trí kéo góc…) |

Trong app: `ScannerPage` là `ConsumerStatefulWidget`; `CropPage` cũng vậy —
**4 góc đang kéo là state cục bộ của UI** (chưa cần chia sẻ) nên để trong
`setState`, không nhét vào Riverpod. Đây là ranh giới quan trọng:

> **State cục bộ thuần UI** (focus, scroll, drag tạm) → `setState`.
> **State nghiệp vụ / cần chia sẻ / sống qua widget** → Riverpod.

`main.dart` phải bọc app trong `ProviderScope`:

```dart
runApp(const ProviderScope(child: DocScanApp()));
```

---

## 7. Ánh xạ MVVM trong project này

```
lib/
├── core/constants.dart          # Tham số tinh chỉnh (comment tiếng Việt)
├── data/                        # MODEL — nguồn dữ liệu (FFI native, repo, API)
│   └── document_detector.dart   #   không biết gì về Flutter/UI
└── features/scanner/
    ├── state/                   # VIEWMODEL
    │   ├── scanner_state.dart   #   State bất biến (immutable + copyWith)
    │   ├── scanner_view_model.dart # Notifier: logic, gọi tầng data
    │   └── providers.dart       #   Khai báo provider (điểm nối DI)
    ├── view/                    # VIEW — chỉ hiển thị + điều hướng
    │   ├── scanner_page.dart    #   watch state, read notifier, KHÔNG logic
    │   ├── crop_page.dart
    │   └── result_page.dart
    └── widgets/                 # Widget con tái sử dụng
```

Luật phân tầng:
1. **View** không gọi thẳng tầng data — mọi thứ đi qua ViewModel.
   (Ngoại lệ nhỏ có chủ đích: `CropPage` gọi `warpFile` trực tiếp vì thao tác
   1-lần thuần dữ liệu; nếu app lớn lên thì chuyển vào 1 ViewModel/UseCase.)
2. **ViewModel** không import Flutter UI (không `BuildContext`, không widget).
   Điều hướng/SnackBar là việc của View — ViewModel chỉ trả kết quả/ghi lỗi vào
   state (`errorMessage`), View đọc và hiển thị.
3. **Model/data** không biết Riverpod — được "tiêm" qua `Provider` ở `providers.dart`.
4. Provider là **điểm dependency-injection duy nhất**: ViewModel lấy detector qua
   `ref.read(documentDetectorProvider)`, test chỉ cần override provider này.

---

## 8. Repository — khi nào cần, cách dùng đúng

### 8.1. Repository là gì (và KHÔNG phải của MVVM/MVC)

Repository là pattern của **tầng data**, không thuộc riêng MVVM hay MVC. Vai trò
duy nhất của nó: **giấu đi "dữ liệu đến từ đâu và bằng cách nào"** để ViewModel
chỉ làm việc với API sạch theo ngôn ngữ nghiệp vụ.

> Nguyên tắc vàng: **ViewModel không được biết dữ liệu đến từ FFI, HTTP, SQLite
> hay cache.** Nếu điều đó đã được đảm bảo, thì "repository" đã tồn tại về bản
> chất — dù bạn có đặt tên nó hay không.

### 8.2. Khi nào NÊN có repository

Thêm repository khi nó thật sự **trừu tượng hoá** một trong các thứ sau:

1. **≥ 2 nguồn cho cùng một loại dữ liệu** — vd. đọc cache local trước, miss thì
   gọi API; repository quyết định thứ tự + đồng bộ.
2. **Đổi nguồn mà không đụng ViewModel** — hôm nay lưu file, mai chuyển SQLite/
   cloud → chỉ sửa impl của repository.
3. **Map DTO → domain model** — JSON/row thô → object sạch cho ViewModel.
4. **Gộp nhiều nguồn thành 1 nghiệp vụ** — vd. "lưu tài liệu đã quét" = ghi ảnh
   (file) + ghi metadata (DB) + (tùy) đẩy cloud, gộp sau 1 hàm `save()`.

### 8.3. Khi nào KHÔNG cần (đừng over-engineer)

- Chỉ có **1 nguồn** và interface của nó đã sạch.
- Repository chỉ **chuyển tiếp 1-1** (`repo.getX() => api.getX()`): thêm file,
  thêm indirection, **không thêm giá trị**. Đây là lỗi hay gặp khi bê "clean
  architecture" máy móc.

### 8.4. Trạng thái app này

`data/document_detector.dart` (`DocumentDetector`) **đang đóng vai repository rồi**:
nó giấu toàn bộ chi tiết native/FFI (`Pointer`, `calloc`, plane YUV…) và expose API
sạch (`prepareCapture`, `warpFile`). ViewModel không biết gì về FFI. Vì vậy **bọc
thêm một lớp `ScannerRepository` quanh nó lúc này chỉ là chuyển tiếp 1-1 → chưa cần.**

**Thời điểm nên thêm repository thật:** khi có nghiệp vụ "thư viện tài liệu đã quét"
— danh sách scan lưu DB, sửa/xoá, đồng bộ cloud. Lúc đó tạo `DocumentRepository`
gộp: detector (FFI) + storage (file) + metadata (DB).

### 8.5. Cách làm ĐÚNG với Riverpod (khi đã cần)

**Bước 1 — Định nghĩa interface trừu tượng** (theo ngôn ngữ nghiệp vụ, không lộ hạ tầng):

```dart
// data/document_repository.dart
abstract interface class DocumentRepository {
  Future<List<ScannedDoc>> getAll();
  Future<ScannedDoc> save({required String imagePath});
  Future<void> delete(String id);
}
```

**Bước 2 — Triển khai cụ thể** (gộp các nguồn; đây là nơi DUY NHẤT biết chi tiết hạ tầng):

```dart
class LocalDocumentRepository implements DocumentRepository {
  LocalDocumentRepository(this._db, this._files); // phụ thuộc được tiêm vào
  final DocsDatabase _db;
  final FileStorage _files;

  @override
  Future<ScannedDoc> save({required String imagePath}) async {
    final stored = await _files.copyIntoAppDir(imagePath); // nguồn 1: file
    final meta = await _db.insert(path: stored);            // nguồn 2: DB
    return ScannedDoc(id: meta.id, path: stored, createdAt: meta.createdAt);
  }
  // ...
}
```

**Bước 3 — Phơi ra bằng Provider** (đúng tinh thần DI của mục 7):

```dart
// providers.dart — trả về KIỂU INTERFACE, không phải impl
final documentRepositoryProvider = Provider<DocumentRepository>((ref) {
  return LocalDocumentRepository(ref.watch(dbProvider), ref.watch(fileStorageProvider));
});
```

**Bước 4 — ViewModel chỉ gọi interface** (không biết là local hay remote):

```dart
class LibraryViewModel extends Notifier<LibraryState> {
  DocumentRepository get _repo => ref.read(documentRepositoryProvider);

  Future<void> add(String imagePath) async {
    final doc = await _repo.save(imagePath: imagePath); // sạch, không lộ file/DB
    state = state.copyWith(docs: [...state.docs, doc]);
  }
}
```

### 8.6. Luật dùng repository cho đúng

- ✅ Repository trả **domain model**, không trả DTO/row/`Map`/`Pointer` thô.
- ✅ ViewModel phụ thuộc **interface** (`DocumentRepository`), không phụ thuộc impl
  → test chỉ cần `overrideWithValue(FakeRepository())`.
- ✅ Chi tiết hạ tầng (SQL, đường dẫn file, HTTP, FFI) **chỉ nằm trong impl**.
- ✅ 1 repository = 1 nhóm nghiệp vụ (Document, User…), không phải 1 repository/ màn hình.
- ❌ Không nhét logic UI/điều hướng vào repository.
- ❌ Không để ViewModel `import` thẳng `sqflite`/`dio`/`dart:ffi` — đó là dấu hiệu
  thiếu repository.
- ❌ Đừng tạo repository chỉ để "cho đủ tầng" nếu nó chỉ gọi lại 1 nguồn y hệt.

> Quy tắc nhớ: **Thêm lớp khi có lý do (nhiều nguồn / map / đổi nguồn), không thêm
> theo nghi thức.** Với app hiện tại: `DocumentDetector` đã đủ vai repository cho
> việc detect; chỉ tạo `DocumentRepository` khi thêm lưu trữ/đồng bộ.

---

## 9. Best practices checklist

**Nên**
- ✅ Khai báo provider là `final` top-level, đặt tên `xxxProvider`.
- ✅ Mặc định `.autoDispose` cho state theo màn hình.
- ✅ State immutable + `copyWith`; list/map luôn tạo bản mới (`[...]`).
- ✅ `ref.watch` trong build, `ref.read` trong callback, `ref.listen` cho side-effect.
- ✅ Rebuild hẹp lại bằng `select` khi widget chỉ cần 1 phần state:
  `ref.watch(scannerViewModelProvider.select((s) => s.isCapturing))`.
- ✅ Với `FutureProvider`, xử lý đủ 3 nhánh `AsyncValue`:
  `async.when(data: ..., loading: ..., error: ...)`.
- ✅ Dọn tài nguyên trong `ref.onDispose` (camera, subscription, timer).
- ✅ Tham số "magic number" đưa vào `core/constants.dart` kèm comment.

**Tránh**
- ❌ Logic nghiệp vụ trong widget (View chỉ vẽ + gọi ViewModel).
- ❌ Truyền `WidgetRef`/`BuildContext` vào ViewModel.
- ❌ Mutate state tại chỗ (`state.capturedPaths.add(...)` — sai, UI không rebuild).
- ❌ Tạo provider bên trong hàm/widget (phải top-level).
- ❌ Gọi `ref.watch` sau `await` trong callback (dùng `ref.read`, và check `mounted`).
- ❌ Dùng Riverpod cho state kéo-thả/animation thuần UI (dùng `setState`).

---

## 10. Lỗi người mới hay gặp

| Triệu chứng | Nguyên nhân | Sửa |
|---|---|---|
| UI không cập nhật | Mutate state tại chỗ, hoặc `ref.read` trong build | copyWith + gán `state =`; đổi sang `watch` |
| Rebuild liên tục | watch cả object to khi chỉ cần 1 field | `select` |
| "No ProviderScope found" | Quên bọc `ProviderScope` ở `runApp` | Bọc lại |
| State mất khi quay lại màn | autoDispose đúng chức năng của nó | Nếu muốn giữ: bỏ autoDispose hoặc dùng `ref.keepAlive()` |
| Gọi method mà không thấy chạy | `ref.watch(provider)` trả state, không phải notifier | `ref.read(provider.notifier).method()` |
| Crash sau `await` trong callback | Widget đã unmount | Check `if (!mounted) return;` trước khi dùng context |

---

## 11. Test nhanh (điểm mạnh nhất của Riverpod)

```dart
test('capture trả về seed', () async {
  final container = ProviderContainer(overrides: [
    documentDetectorProvider.overrideWithValue(FakeDetector()), // mock tầng data
  ]);
  addTearDown(container.dispose);

  final vm = container.read(scannerViewModelProvider.notifier);
  // ... gọi method, assert trên container.read(scannerViewModelProvider)
});
```

Không cần widget, không cần mock framework phức tạp — override provider là đủ.

---

*Ghi chú: project này chủ đích dùng kiểu manual. Kiểu `@riverpod` annotation
(codegen với `riverpod_generator`) là lựa chọn tương đương, mạnh hơn khi app có
nhiều provider/family — xem memory `riverpod-manual-style` trước khi đổi.*
