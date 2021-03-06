/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <set>
#include <vector>

#include "src/kernel_registry.h"
#include "include/errorcode.h"
#include "src/runtime/kernel/opencl/kernel/prelu.h"
#include "src/runtime/opencl/opencl_runtime.h"
#include "src/runtime/kernel/opencl/cl/prelu.cl.inc"

using mindspore::kernel::KERNEL_ARCH::kGPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_Prelu;

namespace mindspore::kernel {

void PReluOpenCLKernel::InitBuffer() {
  int C = in_tensors_[1]->shape()[0];
  int div_ci = UP_DIV(C, C4NUM);
  std::cout << div_ci << std::endl;
  auto allocator = lite::opencl::OpenCLRuntime::GetInstance()->GetAllocator();
  PReluWeight_ = reinterpret_cast<FLOAT_t *>(allocator->Malloc(div_ci * C4NUM * sizeof(FLOAT_t)));
  PReluWeight_ = reinterpret_cast<FLOAT_t *>(allocator->MapBuffer(PReluWeight_, CL_MAP_WRITE, nullptr, true));
  memset(PReluWeight_, 0x00, div_ci * C4NUM * sizeof(FLOAT_t));
  auto origin_weight = reinterpret_cast<FLOAT_t *>(in_tensors_[1]->Data());
  for (int i = 0; i < in_tensors_[1]->ElementsNum(); ++i) {
    PReluWeight_[i] = origin_weight[i];
  }
  allocator->UnmapBuffer(PReluWeight_);
}

int PReluOpenCLKernel::Init() {
  if (in_tensors_[0]->shape().size() != 4) {
    MS_LOG(ERROR) << "PRelu only support dim=4, but your dim=" << in_tensors_[0]->shape().size();
    return RET_ERROR;
  }
  int C_Weight = in_tensors_[1]->shape()[0];
  int C = in_tensors_[0]->shape()[3];
  if (C_Weight != 1 && UP_DIV(C_Weight, C4NUM) != UP_DIV(C, C4NUM)) {
    MS_LOG(ERROR)
      << "PRelu weight channel size must be 1 or must be equal with in_teneors channel size, but your weight size is "
      << C_Weight << " and your input channel size is " << C;
    return RET_ERROR;
  }
  if (C_Weight != 1) {
    InitBuffer();
  }
  std::set<std::string> build_options;
  std::string source = prelu_source;
  std::string program_name = "PRelu";
  std::string kernel_name = "PRelu";
  auto ocl_runtime = lite::opencl::OpenCLRuntime::GetInstance();
  ocl_runtime->LoadSource(program_name, source);
  ocl_runtime->BuildKernel(kernel_, program_name, kernel_name, build_options);
  in_ori_format_ = in_tensors_[0]->GetFormat();
  in_tensors_[0]->SetFormat(schema::Format_NHWC4);
  out_ori_format_ = out_tensors_[0]->GetFormat();
  out_tensors_[0]->SetFormat(schema::Format_NHWC4);
  MS_LOG(DEBUG) << program_name << " init Done!";
  return RET_OK;
}

int PReluOpenCLKernel::Run() {
  MS_LOG(DEBUG) << op_parameter_->name_ << " Running!";
  int N = in_tensors_[0]->shape()[0];
  int H = in_tensors_[0]->shape()[1];
  int W = in_tensors_[0]->shape()[2];
  int C = in_tensors_[0]->shape()[3];
  cl_int4 input_shape = {N, H, W, C};

  auto ocl_runtime = lite::opencl::OpenCLRuntime::GetInstance();
  int arg_idx = 0;
  ocl_runtime->SetKernelArg(kernel_, arg_idx++, in_tensors_[0]->Data());
  ocl_runtime->SetKernelArg(kernel_, arg_idx++, out_tensors_[0]->Data());
  ocl_runtime->SetKernelArg(kernel_, arg_idx++, input_shape);
  if (in_tensors_[1]->shape()[0] == 1) {
    ocl_runtime->SetKernelArg(kernel_, arg_idx++, reinterpret_cast<float *>(in_tensors_[1]->Data()));
  } else {
    ocl_runtime->SetKernelArg(kernel_, arg_idx++, PReluWeight_);
  }
  ocl_runtime->SetKernelArg(kernel_, arg_idx++, reinterpret_cast<int>(in_tensors_[1]->shape()[0]));

  std::vector<size_t> local = {1, 1};
  std::vector<size_t> global = {static_cast<size_t>(H), static_cast<size_t>(W)};
  auto ret = ocl_runtime->RunKernel(kernel_, global, local, nullptr);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Run kernel " << op_parameter_->name_ << " error.";
    return RET_ERROR;
  }
  return RET_OK;
}

int PReluOpenCLKernel::GetImageSize(size_t idx, std::vector<size_t> *img_size) {
  int H = in_tensors_[0]->shape()[1];
  int W = in_tensors_[0]->shape()[2];
  int C = in_tensors_[0]->shape()[3];

#ifdef ENABLE_FP16
  size_t img_dtype = CL_HALF_FLOAT;
#else
  size_t img_dtype = CL_FLOAT;
#endif

  img_size->clear();
  img_size->push_back(W * UP_DIV(C, C4NUM));
  img_size->push_back(H);
  img_size->push_back(img_dtype);
  return RET_OK;
}

kernel::LiteKernel *OpenCLPReluKernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                             const std::vector<lite::tensor::Tensor *> &outputs,
                                             OpParameter *opParameter, const lite::Context *ctx,
                                             const kernel::KernelKey &desc, const lite::PrimitiveC *primitive) {
  if (inputs.size() == 0) {
    MS_LOG(ERROR) << "Input data size must be greater than 0, but your size is " << inputs.size();
    return nullptr;
  }
  if (inputs[0]->shape()[0] > 1) {
    MS_LOG(ERROR) << "Init PRelu kernel failed: Unsupported multi-batch.";
    return nullptr;
  }
  auto *kernel = new (std::nothrow) PReluOpenCLKernel(reinterpret_cast<OpParameter *>(opParameter), inputs, outputs);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel " << opParameter->name_ << "is nullptr.";
    return nullptr;
  }

  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init PRelu kernel failed!";
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Prelu, OpenCLPReluKernelCreator)
}  // namespace mindspore::kernel
