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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_OPENCL_KERNEL_ACTIVATION_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_OPENCL_KERNEL_ACTIVATION_H_

#include <vector>

#include "src/runtime/opencl/opencl_runtime.h"
#include "src/runtime/kernel/opencl/opencl_kernel.h"
#include "nnacl/fp32/activation.h"

namespace mindspore::kernel {

class ActivationOpenClKernel : public OpenCLKernel {
 public:
  explicit ActivationOpenClKernel(OpParameter *parameter, const std::vector<lite::tensor::Tensor *> &inputs,
                                  const std::vector<lite::tensor::Tensor *> &outputs)
      : OpenCLKernel(parameter, inputs, outputs) {
    type_ = (reinterpret_cast<ActivationParameter *>(parameter))->type_;
    alpha_ = (reinterpret_cast<ActivationParameter *>(parameter))->alpha_;
  }
  ~ActivationOpenClKernel() override{};

  int Init() override;
  int Run() override;
  int GetImageSize(size_t idx, std::vector<size_t> *img_size) override;
  cl_int4 GetImg2dShape();

 private:
  cl::Kernel kernel_;
  int type_;
  float alpha_;
  int in_size_;
  int out_size_;
};

}  // namespace mindspore::kernel
#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_OPENCL_KERNEL_ACTIVATION_H_
