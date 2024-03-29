//---------------------------------------------------------
//	ncnn-ai
//
//		©2024 Yuichiro Nakada
//---------------------------------------------------------

#include <cstdlib>
// ncnn
#include "layer.h"
#include "net.h"

#include "ncnn_ai.h" // for binding

/*#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include "stb_image_resize.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"*/
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

FFI_PLUGIN_EXPORT int styletransfer(const char *model, uint8_t *p, int w, int h, uint8_t *o, int ow, int oh)
{
	ncnn::Net net;
	char param_path[256];
	char model_path[256];
	sprintf(param_path, "%s.param", model);
	sprintf(model_path, "%s.bin", model);
	int ret0 = net.load_param(param_path);
	int ret1 = net.load_model(model_path);
	fprintf(stderr, "%s load %d %d\n", model, ret0, ret1);

	const float mean_vals[] = { 127.5f, 127.5f,  127.5f };
	const float norm_vals[] = { 1 / 127.5f, 1 / 127.5f, 1 / 127.5f };
	ncnn::Mat in = ncnn::Mat::from_pixels_resize(p, ncnn::Mat::PIXEL_RGB, w, h, ow, oh);
	in.substract_mean_normalize(mean_vals, norm_vals);
	ncnn::Mat out;
	{
		ncnn::Extractor ex = net.create_extractor();

		ex.input("in0", in);
		ex.extract("out0", out);
	}

	for (int i=0; i<out.c; i++) {
		float* out_data = out.channel(i);
		for (int h=0; h<out.h; h++) {
			for (int w=0; w<out.w; w++) {
				o[(h*ow+w)*3+i] = (out_data[h * out.h + w] +1.0)*127.5;
			}
		}
	}
	return 0;
}


namespace yolox
{

#define YOLOX_NMS_THRESH  0.45 // nms threshold
#define YOLOX_CONF_THRESH 0.25 // threshold of bounding box prob
#define YOLOX_TARGET_SIZE 640  // target image size after resize, might use 416 for small model

// YOLOX use the same focus in yolov5
class YoloV5Focus : public ncnn::Layer
{
public:
	YoloV5Focus()
	{
		one_blob_only = true;
	}

	virtual int forward(const ncnn::Mat& bottom_blob, ncnn::Mat& top_blob, const ncnn::Option& opt) const
	{
		int w = bottom_blob.w;
		int h = bottom_blob.h;
		int channels = bottom_blob.c;

		int outw = w / 2;
		int outh = h / 2;
		int outc = channels * 4;

		top_blob.create(outw, outh, outc, 4u, 1, opt.blob_allocator);
		if (top_blob.empty()) {
			return -100;
		}

		#pragma omp parallel for num_threads(opt.num_threads)
		for (int p = 0; p < outc; p++) {
			const float* ptr = bottom_blob.channel(p % channels).row((p / channels) % 2) + ((p / channels) / 2);
			float* outptr = top_blob.channel(p);

			for (int i = 0; i < outh; i++) {
				for (int j = 0; j < outw; j++) {
					*outptr = *ptr;

					outptr += 1;
					ptr += 2;
				}

				ptr += w;
			}
		}

		return 0;
	}
};

DEFINE_LAYER_CREATOR(YoloV5Focus)

struct Rect {
	float x;
	float y;
	float width;
	float height;
};
struct Object {
	Rect rect;
	//cv::Rect_<float> rect;
	int label;
	float prob;
};

struct GridAndStride {
	int grid0;
	int grid1;
	int stride;
};

static inline float intersection_area(const Object& a, const Object& b)
{
	//cv::Rect_<float> inter = a.rect & b.rect;
	//return inter.area();
	Rect inter;
	if (a.rect.x < b.rect.x) {
		inter.x = b.rect.x;
		inter.width = (a.rect.width -(b.rect.x -a.rect.x));
	} else {
		inter.x = a.rect.x;
		inter.width = (b.rect.width -(a.rect.x -b.rect.x));
	}
	if (a.rect.y < b.rect.y) {
		inter.y = b.rect.y;
		inter.height = (a.rect.height -(b.rect.y -a.rect.y));
	} else {
		inter.y = a.rect.x;
		inter.height = (b.rect.height -(a.rect.y -b.rect.y));
	}
	return inter.width * inter.height;
}

static void qsort_descent_inplace(std::vector<Object>& faceobjects, int left, int right)
{
	int i = left;
	int j = right;
	float p = faceobjects[(left + right) / 2].prob;

	while (i <= j) {
		while (faceobjects[i].prob > p) {
			i++;
		}

		while (faceobjects[j].prob < p) {
			j--;
		}

		if (i <= j) {
			// swap
			std::swap(faceobjects[i], faceobjects[j]);

			i++;
			j--;
		}
	}

	#pragma omp parallel sections
	{
		#pragma omp section
		{
			if (left < j)
			{
				qsort_descent_inplace(faceobjects, left, j);
			}
		}
		#pragma omp section
		{
			if (i < right)
			{
				qsort_descent_inplace(faceobjects, i, right);
			}
		}
	}
}

static void qsort_descent_inplace(std::vector<Object>& objects)
{
	if (objects.empty()) {
		return;
	}

	qsort_descent_inplace(objects, 0, objects.size() - 1);
}

static void nms_sorted_bboxes(const std::vector<Object>& faceobjects, std::vector<int>& picked, float nms_threshold, bool agnostic = false)
{
	picked.clear();

	const int n = faceobjects.size();

	std::vector<float> areas(n);
	for (int i = 0; i < n; i++) {
		//areas[i] = faceobjects[i].rect.area();
		areas[i] = faceobjects[i].rect.width * faceobjects[i].rect.height;
	}

	for (int i = 0; i < n; i++) {
		const Object& a = faceobjects[i];

		int keep = 1;
		for (int j = 0; j < (int)picked.size(); j++) {
			const Object& b = faceobjects[picked[j]];

			if (!agnostic && a.label != b.label) {
				continue;
			}

			// intersection over union
			float inter_area = intersection_area(a, b);
			float union_area = areas[i] + areas[picked[j]] - inter_area;
			// float IoU = inter_area / union_area
			if (inter_area / union_area > nms_threshold) {
				keep = 0;
			}
		}

		if (keep) {
			picked.push_back(i);
		}
	}
}

static void generate_grids_and_stride(const int target_w, const int target_h, std::vector<int>& strides, std::vector<GridAndStride>& grid_strides)
{
	for (int i = 0; i < (int)strides.size(); i++) {
		int stride = strides[i];
		int num_grid_w = target_w / stride;
		int num_grid_h = target_h / stride;
		for (int g1 = 0; g1 < num_grid_h; g1++) {
			for (int g0 = 0; g0 < num_grid_w; g0++) {
				GridAndStride gs;
				gs.grid0 = g0;
				gs.grid1 = g1;
				gs.stride = stride;
				grid_strides.push_back(gs);
			}
		}
	}
}

static void generate_yolox_proposals(std::vector<GridAndStride> grid_strides, const ncnn::Mat& feat_blob, float prob_threshold, std::vector<Object>& objects)
{
	const int num_grid = feat_blob.h;
	const int num_class = feat_blob.w - 5;
	const int num_anchors = grid_strides.size();

	const float* feat_ptr = feat_blob.channel(0);
	for (int anchor_idx = 0; anchor_idx < num_anchors; anchor_idx++) {
		const int grid0 = grid_strides[anchor_idx].grid0;
		const int grid1 = grid_strides[anchor_idx].grid1;
		const int stride = grid_strides[anchor_idx].stride;

		// yolox/models/yolo_head.py decode logic
		//  outputs[..., :2] = (outputs[..., :2] + grids) * strides
		//  outputs[..., 2:4] = torch.exp(outputs[..., 2:4]) * strides
		float x_center = (feat_ptr[0] + grid0) * stride;
		float y_center = (feat_ptr[1] + grid1) * stride;
		float w = exp(feat_ptr[2]) * stride;
		float h = exp(feat_ptr[3]) * stride;
		float x0 = x_center - w * 0.5f;
		float y0 = y_center - h * 0.5f;

		float box_objectness = feat_ptr[4];
		for (int class_idx = 0; class_idx < num_class; class_idx++) {
			float box_cls_score = feat_ptr[5 + class_idx];
			float box_prob = box_objectness * box_cls_score;
			if (box_prob > prob_threshold) {
				Object obj;
				obj.rect.x = x0;
				obj.rect.y = y0;
				obj.rect.width = w;
				obj.rect.height = h;
				obj.label = class_idx;
				obj.prob = box_prob;

				objects.push_back(obj);
			}

		} // class loop
		feat_ptr += feat_blob.w;

	} // point anchor loop
}

}  // namespace yolox

// external

FFI_PLUGIN_EXPORT struct YoloX *yoloxCreate()
{
	return (YoloX *) malloc(sizeof(struct YoloX));
}

FFI_PLUGIN_EXPORT void yoloxDestroy(struct YoloX *yolox)
{
	if (yolox != NULL) {
		free(yolox);
		yolox = NULL;
	}
}

FFI_PLUGIN_EXPORT struct DetectResult *detectResultCreate()
{
	auto result = (DetectResult *) malloc(sizeof(struct DetectResult));
	result->object_num = 0;
	result->object = NULL;
	return result;
}

FFI_PLUGIN_EXPORT void detectResultDestroy(struct DetectResult *result)
{
	if (result == NULL) {
		return;
	}

	if (result->object != NULL) {
		free(result->object);
		result->object = NULL;
	}

	free(result);
	result = NULL;
}

FFI_PLUGIN_EXPORT yolox_err_t detectWithImagePath(
        struct YoloX *yolox, const char *image_path, struct DetectResult *result)
{
	uint8_t *pixels;
	int w, h, bpp;
	pixels = stbi_load(image_path, &w, &h, &bpp, 3);
	return detectWithPixels(yolox, pixels, PIXEL_RGB, w, h, result);
}

FFI_PLUGIN_EXPORT yolox_err_t detectWithPixels(
        struct YoloX *yolox, const uint8_t *pixels, enum PixelType pixelType,
        int img_w, int img_h, struct DetectResult *result)
{
	using namespace yolox;

	ncnn::Net net;

	net.opt.use_vulkan_compute = true;
	// net.opt.use_bf16_storage = true;

	// Focus in yolov5
	net.register_custom_layer("YoloV5Focus", YoloV5Focus_layer_creator);

	if (net.load_param(yolox->param_path)) {
		return YOLOX_ERROR;
	}
	if (net.load_model(yolox->model_path)) {
		return YOLOX_ERROR;
	}

	if (yolox->nms_thresh <= 0) {
		yolox->nms_thresh = YOLOX_NMS_THRESH;
	}
	if (yolox->conf_thresh <= 0) {
		yolox->conf_thresh = YOLOX_CONF_THRESH;
	}
	if (yolox->target_size <= 0) {
		yolox->target_size = YOLOX_TARGET_SIZE;
	}

	int w = img_w;
	int h = img_h;
	float scale = 1.f;
	if (w > h) {
		scale = yolox->target_size / w;
		w = yolox->target_size;
		h = h * scale;
	} else {
		scale = yolox->target_size / h;
		h = yolox->target_size;
		w = w * scale;
	}
	ncnn::Mat in = ncnn::Mat::from_pixels_resize(pixels, pixelType, img_w, img_h, w, h);

	// pad to YOLOX_TARGET_SIZE rectangle
	int wpad = (w + 31) / 32 * 32 - w;
	int hpad = (h + 31) / 32 * 32 - h;
	ncnn::Mat in_pad;
	// different from yolov5, yolox only pad on bottom and right side,
	// which means users don't need to extra padding info to decode boxes coordinate.
	ncnn::copy_make_border(in, in_pad, 0, hpad, 0, wpad, ncnn::BORDER_CONSTANT, 114.f);

	ncnn::Extractor ex = net.create_extractor();

	ex.input("images", in_pad);

	std::vector<yolox::Object> proposals;

	{
		ncnn::Mat out;
		ex.extract("output", out);

		static const int stride_arr[] = {8, 16, 32}; // might have stride=64 in YOLOX
		std::vector<int> strides(stride_arr, stride_arr + sizeof(stride_arr) / sizeof(stride_arr[0]));
		std::vector<GridAndStride> grid_strides;
		generate_grids_and_stride(in_pad.w, in_pad.h, strides, grid_strides);
		generate_yolox_proposals(grid_strides, out, yolox->conf_thresh, proposals);
	}

	// sort all proposals by score from highest to lowest
	qsort_descent_inplace(proposals);

	// apply nms with nms_threshold
	std::vector<int> picked;
	nms_sorted_bboxes(proposals, picked, yolox->nms_thresh);

	int count = picked.size();

	using Obj = struct::Object;
	Obj *obj = (Obj *) malloc(count * sizeof(Obj));

	for (int i = 0; i < count; i++) {
		auto object = proposals[picked[i]];

		// adjust offset to original unpadded
		float x0 = (object.rect.x) / scale;
		float y0 = (object.rect.y) / scale;
		float x1 = (object.rect.x + object.rect.width) / scale;
		float y1 = (object.rect.y + object.rect.height) / scale;

		// clip
		x0 = std::max(std::min(x0, (float)(img_w - 1)), 0.f);
		y0 = std::max(std::min(y0, (float)(img_h - 1)), 0.f);
		x1 = std::max(std::min(x1, (float)(img_w - 1)), 0.f);
		y1 = std::max(std::min(y1, (float)(img_h - 1)), 0.f);

		auto obj_i = obj + i;
		obj_i->label = object.label;
		obj_i->prob = object.prob;
		obj_i->rect.x = x0;
		obj_i->rect.y = y0;
		obj_i->rect.w = x1 - x0;
		obj_i->rect.h = y1 - y0;
	}

	result->object_num = count;
	result->object = obj;
	return YOLOX_OK;
}
