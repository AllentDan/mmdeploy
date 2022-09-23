// Copyright (c) OpenMMLab. All rights reserved.
#include "rknn_net.h"

#include <stdio.h>

#include <fstream>

#include "mmdeploy/core/logger.h"
#include "mmdeploy/core/model.h"
#include "mmdeploy/core/utils/filesystem.h"
#include "mmdeploy/core/utils/formatter.h"

namespace mmdeploy {

RKNNNet::~RKNNNet() {}

void RKNNNet::dump_tensor_attr(rknn_tensor_attr* attr) {
  printf(
      "  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, "
      "type=%s, qnt_type=%s, "
      "zp=%d, scale=%f\n",
      attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2],
      attr->dims[3], attr->n_elems, attr->size, get_format_string(attr->fmt),
      get_type_string(attr->type), get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

unsigned char* RKNNNet::load_model(const char* filename, int* model_size) {
  FILE* fp = fopen(filename, "rb");
  if (fp == nullptr) {
    printf("fopen %s fail!\n", filename);
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  int model_len = ftell(fp);
  unsigned char* model = (unsigned char*)malloc(model_len);
  fseek(fp, 0, SEEK_SET);
  if (model_len != fread(model, 1, model_len, fp)) {
    printf("fread %s fail!\n", filename);
    free(model);
    return NULL;
  }
  *model_size = model_len;
  if (fp) {
    fclose(fp);
  }
  return model;
}

Result<void> RKNNNet::Init(const Value& args) {
  auto& context = args["context"];
  device_ = context["device"].get<Device>();
  stream_ = context["stream"].get<Stream>();
  if (!device_.is_host()) {
    return Status(eNotSupported);
  }

  auto name = args["name"].get<std::string>();
  auto model = context["model"].get<Model>();
  OUTCOME_TRY(auto config, model.GetModelConfig(name));

  std::string content;
  OUTCOME_TRY(content, model.ReadFile(config.net));
  char* model_ptr = const_cast<char*>(content.data());
  int ret = rknn_init(&ctx_, model_ptr, content.size(), 0, NULL);
  if (ret < 0) {
    MMDEPLOY_ERROR("Load .rknn failed: {}", config.net);
    return Status(eInvalidArgument);
  }

  // Get Model Input Output Info
  rknn_input_output_num io_num;
  ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
  if (ret != RKNN_SUCC) {
    MMDEPLOY_ERROR("rknn_query fail! ret= {}", ret);
    return Status(eFail);
  }
  printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

  printf("input tensors:\n");
  rknn_tensor_attr input_attrs[io_num.n_input];
  memset(input_attrs, 0, sizeof(input_attrs));
  for (int i = 0; i < io_num.n_input; i++) {
    input_attrs[i].index = i;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      MMDEPLOY_ERROR("rknn_query fail! ret= {}", ret);
      return Status(eFail);
    }
    dump_tensor_attr(&(input_attrs[i]));
  }
  input_attrs_ = input_attrs;

  printf("output tensors:\n");
  rknn_tensor_attr output_attrs[io_num.n_output];
  memset(output_attrs, 0, sizeof(output_attrs));
  for (int i = 0; i < io_num.n_output; i++) {
    output_attrs[i].index = i;
    ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      MMDEPLOY_ERROR("rknn_query fail! ret= {}", ret);
      return Status(eFail);
    }
    dump_tensor_attr(&(output_attrs[i]));
  }
  output_attrs_ = output_attrs;

  for (int i = 0; i < io_num.n_input; ++i) {
    // TODO get real data type instead of hard code
    input_tensors_.emplace_back(TensorDesc{device_, DataType::kINT8, {}, "#" + std::to_string(i)});
  }

  for (int i = 0; i < io_num.n_output; ++i) {
    // TODO get real data type instead of hard code
    output_tensors_.emplace_back(
        TensorDesc{device_, DataType::kFLOAT, {}, "#" + std::to_string(i)});
  }

  return success();
}

Result<void> RKNNNet::ForwardAsync(Event* event) { return Status(eNotSupported); }

Result<void> RKNNNet::Deinit() { return success(); }

Result<Span<Tensor>> RKNNNet::GetInputTensors() { return input_tensors_; }

Result<Span<Tensor>> RKNNNet::GetOutputTensors() { return output_tensors_; }

Result<void> RKNNNet::Reshape(Span<TensorShape> input_shapes) {
  for (size_t i = 0; i < input_shapes.size(); ++i) {
    input_tensors_[i].Reshape(input_shapes[i]);
  }
  return success();
}

Result<void> RKNNNet::Forward() {
  OUTCOME_TRY(stream_.Wait());

  rknn_input inputs[input_tensors_.size()];
  memset(inputs, 0, input_tensors_.size() * sizeof(rknn_input));
  for (int i = 0; i < input_tensors_.size(); i++) {
    inputs[i].index = i;
    inputs[i].pass_through = 0;
    inputs[i].type = RKNN_TENSOR_UINT8;
    inputs[i].fmt = RKNN_TENSOR_NHWC;
    inputs[i].buf = input_tensors_[i].data<uint8_t>();
    inputs[i].size = input_attrs_[i].n_elems;
  }

  // Set input
  int ret = rknn_inputs_set(ctx_, input_tensors_.size(), inputs);
  if (ret < 0) {
    MMDEPLOY_ERROR("rknn_input_set fail! ret= {}", ret);
    return Status(eFail);
  }

  // Get output
  rknn_output outputs[output_tensors_.size()];
  memset(outputs, 0, output_tensors_.size() * sizeof(rknn_output));
  for (uint32_t i = 0; i < output_tensors_.size(); ++i) {
    outputs[i].want_float = 1;
    outputs[i].index = i;
    outputs[i].is_prealloc = 0;
  }

  ret = rknn_outputs_get(ctx_, output_tensors_.size(), outputs, NULL);
  if (ret < 0) {
    MMDEPLOY_ERROR("rknn_outputs_get fail! ret= {}", ret);
    return Status(eFail);
  }

  OUTCOME_TRY(stream_.Wait());

  return success();
}

class RKNNNetCreator : public Creator<Net> {
 public:
  const char* GetName() const override { return "rknn"; }
  int GetVersion() const override { return 0; }
  std::unique_ptr<Net> Create(const Value& args) override {
    try {
      auto p = std::make_unique<RKNNNet>();
      if (auto r = p->Init(args)) {
        return p;
      } else {
        MMDEPLOY_ERROR("error creating RKNNNet: {}", r.error().message().c_str());
        return nullptr;
      }
    } catch (const std::exception& e) {
      MMDEPLOY_ERROR("unhandled exception when creating RKNNNet: {}", e.what());
      return nullptr;
    }
  }
};

REGISTER_MODULE(Net, RKNNNetCreator);

}  // namespace mmdeploy
