/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <iostream>
#include <memory>
#include "utils/log_adapter.h"
#include "common/common_test.h"
#include "mindspore/lite/src/runtime/opencl/opencl_runtime.h"
#include "mindspore/lite/src/common/file_utils.h"
#include "mindspore/lite/src/runtime/kernel/opencl/subgraph_opencl_kernel.h"
#include "mindspore/lite/src/runtime/kernel/opencl/kernel/batchnorm.h"

namespace mindspore {
class TestBatchnormOpenCLfp32 : public mindspore::CommonTest {
 public:
  TestBatchnormOpenCLfp32() {}
};
class TestBatchnormOpenCLfp16 : public mindspore::CommonTest {
 public:
  TestBatchnormOpenCLfp16() {}
};

template <typename T>
void CompareOutputData1(T *output_data, T *correct_data, int size, float err_bound) {
  for (size_t i = 0; i < size; i++) {
    T abs = fabs(output_data[i] - correct_data[i]);
    ASSERT_LE(abs, err_bound);
  }
}
TEST_F(TestBatchnormOpenCLfp16, Batchnormfp16input_dim4) {
  MS_LOG(INFO) << "begin test";
  auto ocl_runtime = lite::opencl::OpenCLRuntime::GetInstance();
  ocl_runtime->SetFp16Enable(true);
  ocl_runtime->Init();
  auto allocator = ocl_runtime->GetAllocator();

  MS_LOG(INFO) << "Read tensors from .bin";
  std::vector<int> input_shape = {1, 256, 256, 48};
  std::vector<int> output_shape = {1, 256, 256, 48};
  auto data_type = kNumberTypeFloat32;
  auto tensor_type = schema::NodeType_ValueNode;

  // get the input from .bin
  size_t input_size, output_size;
  std::string input_path = "./test_data/batchnorm_in_datafp16.bin";
  std::string mean_path = "./test_data/batchnorm_meanfp16.bin";
  std::string var_path = "./test_data/batchnorm_varfp16.bin";
  std::string offset_path = "./test_data/batchnorm_offsetfp16.bin";
  std::string scale_path = "./test_data/batchnorm_scalefp16.bin";
  std::string output_path = "./test_data/batchnorm_out_datafp16.bin";
  auto input_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(input_path.c_str(), &input_size));
  auto correct_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(output_path.c_str(), &output_size));
  size_t mean_size, var_size, scale_size, offset_size;
  auto mean_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(mean_path.c_str(), &mean_size));
  auto var_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(var_path.c_str(), &var_size));
  auto scale_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(scale_path.c_str(), &scale_size));
  auto offset_data = reinterpret_cast<float16_t *>(mindspore::lite::ReadFile(offset_path.c_str(), &offset_size));

  MS_LOG(INFO) << "construct tensors";
  lite::tensor::Tensor *tensor_data =
    new (std::nothrow) lite::tensor::Tensor(data_type, input_shape, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_mean =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_var =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_scale =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_offset =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  if (tensor_data == nullptr || tensor_mean == nullptr || tensor_var == nullptr || tensor_scale == nullptr ||
      tensor_offset == nullptr) {
    MS_LOG(INFO) << "init tensor failed";
    return;
  }
  auto *output_tensor =
    new (std::nothrow) lite::tensor::Tensor(data_type, output_shape, schema::Format_NHWC4, tensor_type);
  if (output_tensor == nullptr) {
    MS_LOG(INFO) << "init tensor failed";
    delete tensor_data;
    delete tensor_mean;
    delete tensor_var;
    delete tensor_scale;
    delete tensor_offset;
    return;
  }
  std::vector<lite::tensor::Tensor *> inputs = {tensor_data, tensor_scale, tensor_offset, tensor_mean, tensor_var};
  std::vector<lite::tensor::Tensor *> outputs{output_tensor};

  MS_LOG(INFO) << "initialize tensors";
  auto param = new (std::nothrow) BatchNormParameter();
  if (param == nullptr) {
    MS_LOG(INFO) << "new BatchNormParameter failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    return;
  }
  param->epsilon_ = pow(10, -5);
  auto *batchnorm_kernel =
    new (std::nothrow) kernel::BatchNormOpenCLKernel(reinterpret_cast<OpParameter *>(param), inputs, outputs);
  if (batchnorm_kernel == nullptr) {
    MS_LOG(INFO) << "new kernel::BatchNorm_kernel failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    delete param;
    return;
  }
  batchnorm_kernel->Init();

  // to do allocate memory for inputs and outputs
  for (auto &input_tensor : inputs) {
    input_tensor->MallocData(allocator);
  }

  MS_LOG(INFO) << "initialize sub_graph";
  std::vector<kernel::LiteKernel *> kernels{batchnorm_kernel};
  auto *sub_graph = new (std::nothrow) kernel::SubGraphOpenCLKernel(inputs, outputs, kernels, kernels, kernels);
  if (sub_graph == nullptr) {
    MS_LOG(INFO) << "new kernel::SubGraphOpenCLKernel failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    delete param;
    delete batchnorm_kernel;
    return;
  }
  sub_graph->Init();
  MS_LOG(INFO) << "init tensors";
  memcpy(inputs[0]->Data(), input_data, input_size);
  memcpy(inputs[1]->Data(), scale_data, scale_size);
  memcpy(inputs[2]->Data(), offset_data, offset_size);
  memcpy(inputs[3]->Data(), mean_data, mean_size);
  memcpy(inputs[4]->Data(), var_data, var_size);
  std::cout << "==================output data================" << std::endl;
  sub_graph->Run();

  auto *output_data_gpu = reinterpret_cast<float16_t *>(output_tensor->Data());
  CompareOutputData1(output_data_gpu, correct_data, output_tensor->ElementsNum(), 0.0001);
  for (auto tensor : inputs) {
    delete tensor;
  }
  for (auto tensor : outputs) {
    delete tensor;
  }
  delete param;
  delete batchnorm_kernel;
  delete sub_graph;
  lite::opencl::OpenCLRuntime::DeleteInstance();
}
TEST_F(TestBatchnormOpenCLfp32, Batchnormfp32input_dim4) {
  MS_LOG(INFO) << "begin test";
  auto ocl_runtime = lite::opencl::OpenCLRuntime::GetInstance();
  ocl_runtime->Init();
  auto allocator = ocl_runtime->GetAllocator();

  MS_LOG(INFO) << "Read tensors from .bin";
  std::vector<int> input_shape = {1, 256, 256, 47};
  std::vector<int> output_shape = {1, 256, 256, 47};
  auto data_type = kNumberTypeFloat32;
  auto tensor_type = schema::NodeType_ValueNode;

  // get the input from .bin
  size_t input_size, output_size;
  std::string input_path = "./test_data/batchnorm_in_datafp32.bin";
  std::string mean_path = "./test_data/batchnorm_meanfp32.bin";
  std::string var_path = "./test_data/batchnorm_varfp32.bin";
  std::string offset_path = "./test_data/batchnorm_offsetfp32.bin";
  std::string scale_path = "./test_data/batchnorm_scalefp32.bin";
  std::string output_path = "./test_data/batchnorm_out_datafp32.bin";
  auto input_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(input_path.c_str(), &input_size));
  auto correct_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(output_path.c_str(), &output_size));
  size_t mean_size, var_size, scale_size, offset_size;
  auto mean_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(mean_path.c_str(), &mean_size));
  auto var_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(var_path.c_str(), &var_size));
  auto scale_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(scale_path.c_str(), &scale_size));
  auto offset_data = reinterpret_cast<float *>(mindspore::lite::ReadFile(offset_path.c_str(), &offset_size));

  MS_LOG(INFO) << "construct tensors";
  lite::tensor::Tensor *tensor_data =
    new (std::nothrow) lite::tensor::Tensor(data_type, input_shape, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_mean =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_var =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_scale =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  lite::tensor::Tensor *tensor_offset =
    new (std::nothrow) lite::tensor::Tensor(data_type, {1, 1, 1, input_shape[3]}, schema::Format_NHWC, tensor_type);
  if (tensor_data == nullptr || tensor_mean == nullptr || tensor_var == nullptr || tensor_scale == nullptr ||
      tensor_offset == nullptr) {
    MS_LOG(INFO) << "init tensor failed";
    return;
  }
  auto *output_tensor =
    new (std::nothrow) lite::tensor::Tensor(data_type, output_shape, schema::Format_NHWC4, tensor_type);
  if (output_tensor == nullptr) {
    MS_LOG(INFO) << "init tensor failed";
    delete tensor_data;
    delete tensor_mean;
    delete tensor_var;
    delete tensor_scale;
    delete tensor_offset;
    return;
  }
  std::vector<lite::tensor::Tensor *> inputs = {tensor_data, tensor_scale, tensor_offset, tensor_mean, tensor_var};
  std::vector<lite::tensor::Tensor *> outputs{output_tensor};

  MS_LOG(INFO) << "initialize tensors";
  auto param = new (std::nothrow) BatchNormParameter();
  if (param == nullptr) {
    MS_LOG(INFO) << "new BatchNormParameter failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    return;
  }
  param->epsilon_ = pow(10, -5);
  auto *batchnorm_kernel =
    new (std::nothrow) kernel::BatchNormOpenCLKernel(reinterpret_cast<OpParameter *>(param), inputs, outputs);
  if (batchnorm_kernel == nullptr) {
    MS_LOG(INFO) << "new kernel::BatchNorm_kernel failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    delete param;
    return;
  }
  batchnorm_kernel->Init();

  // to do allocate memory for inputs and outputs
  for (auto &input_tensor : inputs) {
    input_tensor->MallocData(allocator);
  }

  MS_LOG(INFO) << "initialize sub_graph";
  std::vector<kernel::LiteKernel *> kernels{batchnorm_kernel};
  auto *sub_graph = new (std::nothrow) kernel::SubGraphOpenCLKernel(inputs, outputs, kernels, kernels, kernels);
  if (sub_graph == nullptr) {
    MS_LOG(INFO) << "new kernel::SubGraphOpenCLKernel failed";
    for (auto tensor : outputs) {
      delete tensor;
    }
    delete param;
    delete batchnorm_kernel;
    return;
  }
  sub_graph->Init();
  MS_LOG(INFO) << "init tensors";
  memcpy(inputs[0]->Data(), input_data, input_size);
  memcpy(inputs[1]->Data(), scale_data, scale_size);
  memcpy(inputs[2]->Data(), offset_data, offset_size);
  memcpy(inputs[3]->Data(), mean_data, mean_size);
  memcpy(inputs[4]->Data(), var_data, var_size);
  std::cout << "==================output data================" << std::endl;
  sub_graph->Run();

  auto *output_data_gpu = reinterpret_cast<float *>(output_tensor->Data());
  CompareOutputData1(output_data_gpu, correct_data, output_tensor->ElementsNum(), 0.0001);
  for (auto tensor : inputs) {
    delete tensor;
  }
  for (auto tensor : outputs) {
    delete tensor;
  }
  delete param;
  delete batchnorm_kernel;
  delete sub_graph;
  lite::opencl::OpenCLRuntime::DeleteInstance();
}
}  // namespace mindspore
