import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';
//import 'package:flutter/widgets.dart'; // for Image
import 'package:image/image.dart' as img;
import 'package:mobx/mobx.dart';

import 'package:ncnn_ai/ncnn_ai_bindings_generated.dart' as yo;
import 'package:path/path.dart' show join;
import 'package:path_provider/path_provider.dart';
//import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
//import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import '../util/image.dart';
import '../util/log.dart';
import 'future_store.dart';

part 'yolox_store.g.dart';

class YoloxStore = YoloxBase with _$YoloxStore;

class YoloxObject {
  int label = 0;
  double prob = 0;
  Rect rect = Rect.zero;
}

class YoloxResult {
  List<YoloxObject> objects = [];
  Duration detectTime = Duration.zero;
}

abstract class YoloxBase with Store {
  late yo.NcnnYoloxBindings _ncnn_ai;

  YoloxBase() {
    final dylib = Platform.isAndroid || Platform.isLinux
        ? DynamicLibrary.open('libncnn_ai.so')
        : DynamicLibrary.process();

    _ncnn_ai = yo.NcnnYoloxBindings(dylib);
  }

  @action
  Uint8List style(ImageData data) {
    final pixels = data.image.getBytes(order: img.ChannelOrder.rgb);
    final pixelsPtr = calloc.allocate<Uint8>(pixels.length);
    for (int i=0; i<pixels.length; i++) {
      pixelsPtr[i] = pixels[i];
    }

    final outPtr = calloc.allocate<Uint8>(512*512*3);
    final err = _ncnn_ai.styletransfer(
        'assets/face_paint_512_v2'.toNativeUtf8().cast(),
        pixelsPtr,
        data.image.width,
        data.image.height,
        outPtr,
        512,
        512);
    print(err);

    Uint8List encodedImBytes = outPtr.asTypedList(512*512*3);
    //img.Image outImg = img.Image.fromBytes(512, 512, encodedImBytes);
    //img.Image outImg = img.Image.memory(Uint8List.fromList(outPtr));
    //Image image = Image(image:MemoryImage(encodedImBytes));

    calloc
      ..free(outPtr)
      ..free(pixelsPtr);
    //return outPtr;
    return encodedImBytes;
  }

  @observable
  FutureStore<YoloxResult> detectFuture = FutureStore<YoloxResult>();

  @action
  Future detect(ImageData data) async {
    try {
      detectFuture.errorMessage = null;

      detectFuture.future = ObservableFuture(_detect(data));

      detectFuture.data = await detectFuture.future;
    } catch (e) {
      detectFuture.errorMessage = e.toString();
    }
  }

  Future<YoloxResult> _detect(ImageData data) async {
    final timebeg = DateTime.now();
    // await Future.delayed(const Duration(seconds: 5));

    final modelPath = 'assets/yolox_nano_fp16.bin';
    final paramPath = 'assets/yolox_nano_fp16.param';
    /*final modelPath = await _copyAssetToLocal('assets/yolox_nano_fp16.bin',
        package: 'ncnn_ai', notCopyIfExist: false);
    final paramPath = await _copyAssetToLocal('assets/yolox_nano_fp16.param',
        package: 'ncnn_ai', notCopyIfExist: false);*/
    log.info('yolox modelPath=$modelPath');
    log.info('yolox paramPath=$paramPath');
    //print('yolox modelPath=$modelPath');
    //debugPrint('yolox modelPath=$modelPath');

    final modelPathUtf8 = modelPath.toNativeUtf8();
    final paramPathUtf8 = paramPath.toNativeUtf8();

    final yolox = _ncnn_ai.yoloxCreate();
    yolox.ref.model_path = modelPathUtf8.cast();
    yolox.ref.param_path = paramPathUtf8.cast();
    yolox.ref.nms_thresh = 0.45;
    yolox.ref.conf_thresh = 0.45;
    yolox.ref.target_size = 416;
    // yolox.ref.target_size = 640;

    final detectResult = _ncnn_ai.detectResultCreate();

    final pixels = data.image.getBytes(order: img.ChannelOrder.bgr);
    // Pass Uint8List to Pointer<Void>
    //  https://github.com/dart-lang/ffi/issues/27
    //  https://github.com/martin-labanic/camera_preview_ffi_image_processing/blob/master/lib/image_worker.dart
    final pixelsPtr = calloc.allocate<Uint8>(pixels.length);
    for (int i=0; i<pixels.length; i++) {
      pixelsPtr[i] = pixels[i];
    }

    final err = _ncnn_ai.detectWithPixels(
        yolox,
        pixelsPtr,
        yo.PixelType.PIXEL_BGR,
        data.image.width,
        data.image.height,
        detectResult);

    final objects = <YoloxObject>[];
    if (err == yo.YOLOX_OK) {
      final num = detectResult.ref.object_num;
      for (int i=0; i<num; i++) {
        final o = detectResult.ref.object.elementAt(i).ref;
        final obj = YoloxObject();
        obj.label = o.label;
        obj.prob = o.prob;
        obj.rect = Rect.fromLTWH(o.rect.x, o.rect.y, o.rect.w, o.rect.h);
        objects.add(obj);
      }
    } else {
      print('error');
    /*showDialog(
      context: context,
      builder: (context) {
        return AlertDialog(
          title: Text("Error"),
          content: Text("メッセージ内容"),
          actions: [
            TextButton(
              child: Text("OK"),
              onPressed: () => Navigator.pop(context),
            ),
          ],
        );
      },
    );*/
    }

    calloc
      ..free(pixelsPtr)
      ..free(modelPathUtf8)
      ..free(paramPathUtf8);

    _ncnn_ai.detectResultDestroy(detectResult);
    _ncnn_ai.yoloxDestroy(yolox);

    // final objects = List<YoloxObject>.generate(5, (i) {
    //   final obj = YoloxObject();
    //   obj.rect = Rect.fromLTRB(
    //     100.0 * i,
    //     100.0 * i,
    //     100.0 * (i + 1),
    //     100.0 * (i + 1),
    //   );
    //   return obj;
    // });
    final result = YoloxResult();
    result.objects = objects;
    result.detectTime = DateTime.now().difference(timebeg);
    return result;
  }

  Future<String> _copyAssetToLocal(String assetName,
      {AssetBundle? bundle,
      String? package,
      bool notCopyIfExist = false}) async {
      //final f = WFile(assetName);
      /*final _tempDirectory = await getTemporaryDirectory();
      print(_tempDirectory);
      final Directory appDocumentsDir = await getApplicationDocumentsDirectory();
      print(appDocumentsDir);
      print('_copyAssetToLocal');*/
    final docDir = await getApplicationDocumentsDirectory();
    /*if (docDir == null) {
      throw MissingPlatformDirectoryException('Unable to get application documents directory');
    }*/
    final filePath = join(docDir.path, assetName);

    if (notCopyIfExist &&
        FileSystemEntity.typeSync(filePath) != FileSystemEntityType.notFound) {
      return filePath;
    }

    final keyName = package == null ? assetName : 'packages/$package/$assetName';
    final data = await (bundle ?? rootBundle).load(keyName);

    final file = File(filePath)..createSync(recursive: true);
    await file.writeAsBytes(data.buffer.asUint8List(), flush: true);
    return file.path;
  }
}

const cocoLabels = [
  'person',
  'bicycle',
  'car',
  'motorcycle',
  'airplane',
  'bus',
  'train',
  'truck',
  'boat',
  'traffic light',
  'fire hydrant',
  'stop sign',
  'parking meter',
  'bench',
  'bird',
  'cat',
  'dog',
  'horse',
  'sheep',
  'cow',
  'elephant',
  'bear',
  'zebra',
  'giraffe',
  'backpack',
  'umbrella',
  'handbag',
  'tie',
  'suitcase',
  'frisbee',
  'skis',
  'snowboard',
  'sports ball',
  'kite',
  'baseball bat',
  'baseball glove',
  'skateboard',
  'surfboard',
  'tennis racket',
  'bottle',
  'wine glass',
  'cup',
  'fork',
  'knife',
  'spoon',
  'bowl',
  'banana',
  'apple',
  'sandwich',
  'orange',
  'broccoli',
  'carrot',
  'hot dog',
  'pizza',
  'donut',
  'cake',
  'chair',
  'couch',
  'potted plant',
  'bed',
  'dining table',
  'toilet',
  'tv',
  'laptop',
  'mouse',
  'remote',
  'keyboard',
  'cell phone',
  'microwave',
  'oven',
  'toaster',
  'sink',
  'refrigerator',
  'book',
  'clock',
  'vase',
  'scissors',
  'teddy bear',
  'hair drier',
  'toothbrush',
];
