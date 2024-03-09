# bsnap

| Linux | Android |
| - | - |
| ![demo](../_img/demo.png) | ![demo](../_img/demo_android.png) |

## Prepare

Create project:

```bash
flutter create --project-name bsnap --org dev.flutter --android-language java --ios-language objc --platforms=android,ios,linux bsnap
```

Install deps:

```bash
cd bsnap/
dart pub get
```

Install prebuild binary for plugin.

<!--
dart pub add ffi path logging image easy_debounce
dart pub add -d ffigen

flutter pub add mobx flutter_mobx provider path_provider
flutter pub add -d build_runner mobx_codegen
-->

## Linux

Run app:

```bash
cd bsnap/
flutter run -d linux
```

If wanna rebuild mobx stores,

```bash
dart run build_runner build
```

## Android

Run app:

```bash
cd bsnap/
flutter run
# flutter run --release
```

## References

- [nihui/ncnn-android-yolox](https://github.com/nihui/ncnn-android-yolox)
- [KoheiKanagu/ncnn_yolox_flutter](https://github.com/KoheiKanagu/ncnn_yolox_flutter)
- [tomassasovsky/ncnn.dart](https://github.com/tomassasovsky/ncnn.dart)
- https://github.com/ikuokuo/start-flutter/tree/main?tab=readme-ov-file

