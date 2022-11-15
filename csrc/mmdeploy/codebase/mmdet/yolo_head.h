// Copyright (c) OpenMMLab. All rights reserved.
#ifndef MMDEPLOY_CODEBASE_MMDET_YOLO_HEAD_H_
#define MMDEPLOY_CODEBASE_MMDET_YOLO_HEAD_H_

#include "mmdeploy/codebase/mmdet/mmdet.h"
#include "mmdeploy/core/tensor.h"

namespace mmdeploy::mmdet {

class YOLOHead : public MMDetection {
 public:
  explicit YOLOHead(const Value& cfg);
  Result<Value> operator()(const Value& prep_res, const Value& infer_res);
  int YOLOFeatDecode(const Tensor& feat_map, const std::vector<std::vector<int>>& anchor,
                     int grid_h, int grid_w, int height, int width, int stride,
                     std::vector<float>& boxes, std::vector<float>& objProbs,
                     std::vector<int>& classId, float threshold) const;
  Result<Detections> GetBBoxes(const Value& prep_res, const std::vector<Tensor>& pred_maps) const;
  virtual std::vector<float> yolo_decode(float box_x, float box_y, float box_w, float box_h,
                                         int stride, const std::vector<std::vector<int>>& anchor,
                                         int j, int i, int a) const = 0;

 private:
  float score_thr_{0.4f};
  int nms_pre_{1000};
  float iou_threshold_{0.45f};
  int min_bbox_size_{0};
  std::vector<std::vector<std::vector<int>>> anchors_;
  std::vector<unsigned int> strides_;
};

class YOLOV3Head : public YOLOHead {
 public:
  explicit YOLOV3Head(const Value& cfg);
  Result<Value> operator()(const Value& prep_res, const Value& infer_res);
  std::vector<float> yolo_decode(float box_x, float box_y, float box_w, float box_h, int stride,
                                 const std::vector<std::vector<int>>& anchor, int j, int i,
                                 int a) const override;
};

class YOLOV5Head : public YOLOHead {
 public:
  explicit YOLOV5Head(const Value& cfg);
  Result<Value> operator()(const Value& prep_res, const Value& infer_res);
  std::vector<float> yolo_decode(float box_x, float box_y, float box_w, float box_h, int stride,
                                 const std::vector<std::vector<int>>& anchor, int j, int i,
                                 int a) const override;
};

}  // namespace mmdeploy::mmdet

#endif  // MMDEPLOY_CODEBASE_MMDET_YOLO_HEAD_H_
