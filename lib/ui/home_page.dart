//---------------------------------------------------------
//	ncnn-ai
//
//		©2024 Yuichiro Nakada
//---------------------------------------------------------

import 'package:easy_debounce/easy_debounce.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:provider/provider.dart';

import '../store/stores.dart';
import 'detect_result_page.dart';

class _HomePageState extends State<HomePage> {
  late ImageStore _imageStore;
  late YoloxStore _yoloxStore;
  late OptionStore _optionStore;
  var outImage;

  @override
  void initState() {
    super.initState();
  }

  @override
  void didChangeDependencies() {
    _imageStore = Provider.of<ImageStore>(context);
    _yoloxStore = Provider.of<YoloxStore>(context);
    _optionStore = Provider.of<OptionStore>(context);

    _imageStore.load();

    //outImage = Image.network('https://images.unsplash.com/photo-1516750484197-6b28d10c91ea?ixlib=rb-1.2.1&ixid=MnwxMjA3fDB8MHxwaG90by1wYWdlfHx8fGVufDB8fHx8&auto=format&fit=crop&w=1170&q=80');
    //Uint8List blankBytes = Base64Codec().decode("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
    //Uint8List blankBytes;
    //Image.memory(blankBytes, height:1,);

    super.didChangeDependencies();
  }

  void _debounce(String tag, void Function() onExecute,
      {Duration duration = const Duration(milliseconds: 200)}) {
    EasyDebounce.debounce(tag, duration, onExecute);
  }

  void _pickImage() async {
    final result = await FilePicker.platform.pickFiles(type: FileType.image);
    if (result == null) return;

    final image = result.files.first;
    _openImage(image);
  }

  void _openImage(PlatformFile file) {
    _imageStore.load(imagePath: file.path);
  }

  void _detectImage() {
    if (_imageStore.loadFuture.futureState != FutureState.loaded) return;
    _yoloxStore.detect(_imageStore.loadFuture.data!);
  }

  void _styleImage() {
    //print('click');
    showDialog(
      context: context,
      builder: (context) {
        return AlertDialog(
          title: Text("タイトル"),
          content: Text("メッセージ内容"),
          actions: [
            TextButton(
              child: Text("Cancel"),
              onPressed: () => Navigator.pop(context),
            ),
            TextButton(
              child: Text("OK"),
              onPressed: () => Navigator.pop(context),
            ),
          ],
        );
      },
    );
    //_yoloxStore.detect(_imageStore.loadFuture.data!);
    //_imageStore.loadFuture.data.imageUi = _yoloxStore.style(_imageStore.loadFuture.data!);
    outImage = _yoloxStore.style(_imageStore.loadFuture.data!);
    //print(outImage);
    //_imageStore.loadFuture.data.imageUi = im;
    //_imageStore.loadFuture.data = Image.memory(im, 512, 512);
    //_imageStore.loadFuture.data = Image.memory(_yoloxStore.style(_imageStore.loadFuture.data!), 512, 512);
    //Uint8List im = _yoloxStore.style(_imageStore.loadFuture.data!);
  }

  @override
  Widget build(BuildContext context) {
    const pad = 20.0;
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: Text(widget.title),
      ),
      body: Padding(
        padding: const EdgeInsets.all(pad),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
                flex: 1,
                child: Observer(builder: (context) {
                  if (_imageStore.loadFuture.futureState == FutureState.loading) {
                    return const Center(child: CircularProgressIndicator());
                  }

                  if (_imageStore.loadFuture.errorMessage != null) {
                    return Center(
                        child: Text(_imageStore.loadFuture.errorMessage!));
                  }

                  final data = _imageStore.loadFuture.data;
                  if (data == null) {
                    return const Center(child: Text('Image load null :('));
                  }

                  _yoloxStore.detectFuture.reset();

                  return Container(
                    decoration: BoxDecoration(
                        border: Border.all(color: Colors.orangeAccent)),
                    child: DetectResultPage(imageData: data),
                  );
                })),
                //Image.network('https://images.unsplash.com/photo-1516750484197-6b28d10c91ea?ixlib=rb-1.2.1&ixid=MnwxMjA3fDB8MHxwaG90by1wYWdlfHx8fGVufDB8fHx8&auto=format&fit=crop&w=1170&q=80'),
                //Image.memory(outImage),
            Expanded(
                child: Observer(builder: (context) {
                  if (outImage == null) {
                    return const Center(child: Text('Image load null :('));
                  }
                  return Image.memory(outImage);
                })
            ),
            const SizedBox(height: pad),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Expanded(
                  child: ElevatedButton(
                    child: const Text('Pick image'),
                    onPressed: () => _debounce('_pickImage', _pickImage),
                  ),
                ),
                const SizedBox(width: pad),
                Expanded(
                  child: ElevatedButton(
                    child: const Text('Style'),
                    onPressed: () => _debounce('_styleImage', _styleImage),
                  ),
                ),
                const SizedBox(width: pad),
                Expanded(
                  child: ElevatedButton(
                    child: const Text('Detect objects'),
                    onPressed: () => _debounce('_detectImage', _detectImage),
                  ),
                ),
                const SizedBox(width: pad),
                Expanded(
                  child: Observer(builder: (context) {
                    return ElevatedButton.icon(
                      icon: Icon(_optionStore.bboxesVisible
                          ? Icons.check_box_outlined
                          : Icons.check_box_outline_blank),
                      label: const Text('Binding boxes'),
                      onPressed: () => _optionStore
                          .setBboxesVisible(!_optionStore.bboxesVisible),
                    );
                  }),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key, required this.title});

  final String title;

  @override
  State<HomePage> createState() => _HomePageState();
}
