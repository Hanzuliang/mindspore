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

#include "src/runtime/kernel/arm/fp16/arithmetic_fp16.h"
#include "nnacl/fp16/arithmetic_fp16.h"
#include "nnacl/fp16/cast_fp16.h"
#include "schema/model_generated.h"
#include "src/kernel_registry.h"
#include "src/runtime/runtime_api.h"
#include "include/errorcode.h"

using mindspore::kernel::KERNEL_ARCH::kCPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;

using mindspore::schema::PrimitiveType_Add;
using mindspore::schema::PrimitiveType_Div;
using mindspore::schema::PrimitiveType_Eltwise;
using mindspore::schema::PrimitiveType_Equal;
using mindspore::schema::PrimitiveType_FloorDiv;
using mindspore::schema::PrimitiveType_FloorMod;
using mindspore::schema::PrimitiveType_Greater;
using mindspore::schema::PrimitiveType_GreaterEqual;
using mindspore::schema::PrimitiveType_Less;
using mindspore::schema::PrimitiveType_LessEqual;
using mindspore::schema::PrimitiveType_LogicalAnd;
using mindspore::schema::PrimitiveType_LogicalOr;
using mindspore::schema::PrimitiveType_Maximum;
using mindspore::schema::PrimitiveType_Minimum;
using mindspore::schema::PrimitiveType_Mul;
using mindspore::schema::PrimitiveType_NotEqual;
using mindspore::schema::PrimitiveType_SquaredDifference;
using mindspore::schema::PrimitiveType_Sub;

namespace mindspore::kernel {
void ArithmeticFP16CPUKernel::FreeTmpBuffer() {
  if (input0_fp16_ != nullptr) {
    context_->allocator->Free(input0_fp16_);
    input0_fp16_ = nullptr;
  }
  if (input1_fp16_ != nullptr) {
    context_->allocator->Free(input1_fp16_);
    input1_fp16_ = nullptr;
  }
  if (output_fp16_ != nullptr) {
    context_->allocator->Free(output_fp16_);
    output_fp16_ = nullptr;
  }
}

ArithmeticFP16CPUKernel::~ArithmeticFP16CPUKernel() {}

int ArithmeticFP16CPUKernel::Init() {
  switch (op_parameter_->type_) {
    case PrimitiveType_Mul:
      switch (arithmeticParameter_->activation_type_) {
        case schema::ActivationType_RELU:
          arithmetic_run_ = ElementMulReluFp16;
          break;
        case schema::ActivationType_RELU6:
          arithmetic_run_ = ElementMulRelu6Fp16;
          break;
        default:
          arithmetic_run_ = ElementMulFp16;
          break;
      }
      break;
    case PrimitiveType_Add:
      switch (arithmeticParameter_->activation_type_) {
        case schema::ActivationType_RELU:
          arithmetic_run_ = ElementAddReluFp16;
          break;
        case schema::ActivationType_RELU6:
          arithmetic_run_ = ElementAddRelu6Fp16;
          break;
        default:
          arithmetic_run_ = ElementAddFp16;
          break;
      }
      break;
    case PrimitiveType_Sub:
      switch (arithmeticParameter_->activation_type_) {
        case schema::ActivationType_RELU:
          arithmetic_run_ = ElementSubReluFp16;
          break;
        case schema::ActivationType_RELU6:
          arithmetic_run_ = ElementSubRelu6Fp16;
          break;
        default:
          arithmetic_run_ = ElementSubFp16;
          break;
      }
      break;
    case PrimitiveType_Div:
      switch (arithmeticParameter_->activation_type_) {
        case schema::ActivationType_RELU:
          arithmetic_run_ = ElementDivReluFp16;
          break;
        case schema::ActivationType_RELU6:
          arithmetic_run_ = ElementDivRelu6Fp16;
          break;
        default:
          arithmetic_run_ = ElementDivFp16;
          break;
      }
      break;
    case PrimitiveType_FloorMod:
      arithmetic_run_ = ElementFloorModFp16;
      break;
    case PrimitiveType_FloorDiv:
      arithmetic_run_ = ElementFloorDivFp16;
      break;
    case PrimitiveType_LogicalAnd:
      arithmetic_run_ = ElementLogicalAndFp16;
      break;
    case PrimitiveType_LogicalOr:
      arithmetic_run_ = ElementLogicalOrFp16;
      break;
    case PrimitiveType_SquaredDifference:
      arithmetic_run_ = ElementSquaredDifferenceFp16;
      break;
    case PrimitiveType_Maximum:
      arithmetic_run_ = ElementMaximumFp16;
      break;
    case PrimitiveType_Minimum:
      arithmetic_run_ = ElementMinimumFp16;
      break;
    case PrimitiveType_NotEqual:
      arithmetic_run_ = ElementNotEqualFp16;
      break;
    case PrimitiveType_Equal:
      arithmetic_run_ = ElementEqualFp16;
      break;
    case PrimitiveType_Less:
      arithmetic_run_ = ElementLessFp16;
      break;
    case PrimitiveType_LessEqual:
      arithmetic_run_ = ElementLessEqual;
      break;
    case PrimitiveType_Greater:
      arithmetic_run_ = ElementGreaterFp16;
      break;
    case PrimitiveType_GreaterEqual:
      arithmetic_run_ = ElementGreaterEqualFp16;
      break;
    default:
      MS_LOG(ERROR) << "Error Operator type " << op_parameter_->type_;
      arithmetic_run_ = nullptr;
      break;
  }
  if (!InferShapeDone()) {
    return RET_OK;
  }
  return ReSize();
}

int ArithmeticFP16CPUKernel::ReSize() {
  arithmeticParameter_->in_elements_num0_ = in_tensors_[0]->ElementsNum();
  arithmeticParameter_->in_elements_num1_ = in_tensors_[1]->ElementsNum();
  arithmeticParameter_->out_elements_num_ = out_tensors_[0]->ElementsNum();

  if (arithmeticParameter_->in_elements_num0_ == 1 || arithmeticParameter_->in_elements_num1_ == 1) {
    switch (arithmeticParameter_->op_parameter_.type_) {
      case PrimitiveType_Mul:
        arithmeticParameter_->broadcasting_ = false;
        switch (arithmeticParameter_->activation_type_) {
          case schema::ActivationType_RELU:
            arithmetic_opt_run_ = ElementOptMulReluFp16;
            break;
          case schema::ActivationType_RELU6:
            arithmetic_opt_run_ = ElementOptMulRelu6Fp16;
            break;
          default:
            arithmetic_opt_run_ = ElementOptMulFp16;
            break;
        }
        break;
      case PrimitiveType_Add:
        arithmeticParameter_->broadcasting_ = false;
        switch (arithmeticParameter_->activation_type_) {
          case schema::ActivationType_RELU:
            arithmetic_opt_run_ = ElementOptAddReluFp16;
            break;
          case schema::ActivationType_RELU6:
            arithmetic_opt_run_ = ElementOptAddRelu6Fp16;
            break;
          default:
            arithmetic_opt_run_ = ElementOptAddFp16;
            break;
        }
        break;
      case PrimitiveType_Sub:
        arithmeticParameter_->broadcasting_ = false;
        switch (arithmeticParameter_->activation_type_) {
          case schema::ActivationType_RELU:
            arithmetic_opt_run_ = ElementOptSubReluFp16;
            break;
          case schema::ActivationType_RELU6:
            arithmetic_opt_run_ = ElementOptSubRelu6Fp16;
            break;
          default:
            arithmetic_opt_run_ = ElementOptSubFp16;
            break;
        }
        break;
      case PrimitiveType_Div:
        arithmeticParameter_->broadcasting_ = false;
        switch (arithmeticParameter_->activation_type_) {
          case schema::ActivationType_RELU:
            arithmetic_opt_run_ = ElementOptDivReluFp16;
            break;
          case schema::ActivationType_RELU6:
            arithmetic_opt_run_ = ElementOptDivRelu6Fp16;
            break;
          default:
            arithmetic_opt_run_ = ElementOptDivFp16;
            break;
        }
        break;
      case PrimitiveType_FloorMod:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptFloorModFp16;
        break;
      case PrimitiveType_FloorDiv:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptFloorDivFp16;
        break;
      case PrimitiveType_LogicalAnd:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptLogicalAndFp16;
        break;
      case PrimitiveType_LogicalOr:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptLogicalOrFp16;
        break;
      case PrimitiveType_SquaredDifference:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptSquaredDifferenceFp16;
        break;
      case PrimitiveType_Maximum:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptMaximumFp16;
        break;
      case PrimitiveType_Minimum:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptMinimumFp16;
        break;
      case PrimitiveType_NotEqual:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptNotEqualFp16;
        break;
      case PrimitiveType_Equal:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptEqualFp16;
        break;
      case PrimitiveType_Less:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptLessFp16;
        break;
      case PrimitiveType_LessEqual:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptLessEqualFp16;
        break;
      case PrimitiveType_Greater:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptGreaterFp16;
        break;
      case PrimitiveType_GreaterEqual:
        arithmeticParameter_->broadcasting_ = false;
        arithmetic_opt_run_ = ElementOptGreaterEqualFp16;
        break;
      default:
        break;
    }
  }

  if (arithmeticParameter_->broadcasting_) {
    outside_ = 1;
    for (int i = arithmeticParameter_->ndim_ - 1; i >= 0; --i) {
      if (arithmeticParameter_->in_shape0_[i] != arithmeticParameter_->in_shape1_[i]) {
        break_pos_ = i;
        break;
      }
      outside_ *= arithmeticParameter_->out_shape_[i];
    }
    ComputeStrides(arithmeticParameter_->in_shape0_, arithmeticParameter_->in_strides0_, arithmeticParameter_->ndim_);
    ComputeStrides(arithmeticParameter_->in_shape1_, arithmeticParameter_->in_strides1_, arithmeticParameter_->ndim_);
    ComputeStrides(arithmeticParameter_->out_shape_, arithmeticParameter_->out_strides_, arithmeticParameter_->ndim_);
  }
  return RET_OK;
}

int ArithmeticFP16CPUKernel::BroadcastRun(float16_t *input0, float16_t *input1, float16_t *output, int dim,
                                          int out_count, int out_thread_stride) {
  if (dim > break_pos_) {
    int error_code =
      arithmetic_run_(input0 + out_thread_stride, input1 + out_thread_stride, output + out_thread_stride, out_count);
    if (output_fp16_ != nullptr) {
      auto output_fp32 = reinterpret_cast<float *>(out_tensors_[0]->Data());
      int bias = output - output_fp16_;
      output_fp32 += bias;
      Float16ToFloat32(output + out_thread_stride, output_fp32 + out_thread_stride, out_count);
    }
    return error_code;
  }
  for (int i = 0; i < arithmeticParameter_->out_shape_[dim]; ++i) {
    int pos0_ = arithmeticParameter_->in_shape0_[dim] == 1 ? 0 : i;
    int pos1_ = arithmeticParameter_->in_shape1_[dim] == 1 ? 0 : i;
    int error_code =
      BroadcastRun(input0 + pos0_ * arithmeticParameter_->in_strides0_[dim],
                   input1 + pos1_ * arithmeticParameter_->in_strides1_[dim],
                   output + i * arithmeticParameter_->out_strides_[dim], dim + 1, out_count, out_thread_stride);
    if (error_code != RET_OK) {
      return RET_ERROR;
    }
  }
  return RET_OK;
}

int ArithmeticFP16CPUKernel::DoArithmetic(int task_id) {
  auto input0 = reinterpret_cast<float16_t *>(in_tensors_[0]->Data());
  auto input1 = reinterpret_cast<float16_t *>(in_tensors_[1]->Data());
  auto output = reinterpret_cast<float16_t *>(out_tensors_[0]->Data());
  auto element_num = out_tensors_[0]->ElementsNum();

  float16_t *input0_data = input0_fp16_ == nullptr ? input0 : input0_fp16_;
  float16_t *input1_data1 = input1_fp16_ == nullptr ? input1 : input1_fp16_;
  auto output_data = output_fp16_ == nullptr ? output : output_fp16_;
  int stride = UP_DIV(element_num, context_->thread_num_);
  int count = MSMIN(stride, element_num - stride * task_id);
  auto thread_stride = stride * task_id;

  if (arithmetic_run_ == nullptr) {
    MS_LOG(ERROR) << "arithmetic_run function is nullptr!";
    return RET_ERROR;
  }

  int error_code = RET_OK;
  if (arithmeticParameter_->broadcasting_) {
    stride = UP_DIV(outside_, context_->thread_num_);
    out_count_ = MSMIN(stride, outside_ - stride * task_id);
    out_thread_stride_ = stride * task_id;
    error_code = BroadcastRun(input0_data, input1_data1, output_data, 0, out_count_, out_thread_stride_);
  } else if (arithmetic_opt_run_ != nullptr) {
    if (arithmeticParameter_->in_elements_num0_ == 1) {
      error_code = arithmetic_opt_run_(input0_data, input1_data1 + thread_stride, output_data + thread_stride, count,
                                       arithmeticParameter_);
    } else if (arithmeticParameter_->in_elements_num1_ == 1) {
      error_code = arithmetic_opt_run_(input0_data + thread_stride, input1_data1, output_data + thread_stride, count,
                                       arithmeticParameter_);
    } else {
      error_code = arithmetic_opt_run_(input0_data + thread_stride, input1_data1 + thread_stride,
                                       output_data + thread_stride, count, arithmeticParameter_);
    }
  } else {
    error_code =
      arithmetic_run_(input0_data + thread_stride, input1_data1 + thread_stride, output_data + thread_stride, count);
  }
  if (error_code != RET_OK) {
    return RET_ERROR;
  }
  if (output_fp16_ != nullptr && !arithmeticParameter_->broadcasting_) {
    auto output_fp32 = reinterpret_cast<float *>(out_tensors_[0]->Data());
    Float16ToFloat32(output_data + thread_stride, output_fp32 + thread_stride, count);
  }
  return RET_OK;
}

static int ArithmeticsRun_Fp16(void *cdata, int task_id) {
  auto arithmetic_kernel = reinterpret_cast<ArithmeticFP16CPUKernel *>(cdata);
  auto error_code = arithmetic_kernel->DoArithmetic(task_id);
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "ArithmeticsRun error task_id[" << task_id << "] error_code[" << error_code << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int ArithmeticFP16CPUKernel::Run() {
  auto ret = Prepare();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Prepare fail!ret: " << ret;
    return ret;
  }

  arithmeticParameter_->in_elements_num0_ = in_tensors_[0]->ElementsNum();
  arithmeticParameter_->in_elements_num1_ = in_tensors_[1]->ElementsNum();
  arithmeticParameter_->out_elements_num_ = out_tensors_[0]->ElementsNum();
  if (out_tensors_[0]->data_type() == kNumberTypeFloat32 || out_tensors_[0]->data_type() == kNumberTypeFloat) {
    output_fp16_ = reinterpret_cast<float16_t *>(malloc(arithmeticParameter_->out_elements_num_ * sizeof(float16_t)));
    if (output_fp16_ == nullptr) {
      MS_LOG(ERROR) << "malloc data fail!";
      FreeTmpBuffer();
      return RET_ERROR;
    }
  }
  if (in_tensors_[0]->data_type() == kNumberTypeFloat32 || in_tensors_[0]->data_type() == kNumberTypeFloat) {
    input0_fp16_ = reinterpret_cast<float16_t *>(malloc(arithmeticParameter_->in_elements_num0_ * sizeof(float16_t)));
    if (input0_fp16_ == nullptr) {
      MS_LOG(ERROR) << "malloc data fail!";
      FreeTmpBuffer();
      return RET_ERROR;
    }
    Float32ToFloat16(reinterpret_cast<float *>(in_tensors_[0]->Data()), input0_fp16_,
                     arithmeticParameter_->in_elements_num0_);
  }
  if (in_tensors_[1]->data_type() == kNumberTypeFloat32 || in_tensors_[1]->data_type() == kNumberTypeFloat) {
    input1_fp16_ = reinterpret_cast<float16_t *>(malloc(arithmeticParameter_->in_elements_num0_ * sizeof(float16_t)));
    if (input1_fp16_ == nullptr) {
      MS_LOG(ERROR) << "malloc data fail!";
      FreeTmpBuffer();
      return RET_ERROR;
    }
    Float32ToFloat16(reinterpret_cast<float *>(in_tensors_[1]->Data()), input1_fp16_,
                     arithmeticParameter_->in_elements_num1_);
  }
  ret = ParallelLaunch(THREAD_POOL_DEFAULT, ArithmeticsRun_Fp16, this, context_->thread_num_);
  FreeTmpBuffer();
  return ret;
}

kernel::LiteKernel *CpuArithmeticFp16KernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                                   const std::vector<lite::tensor::Tensor *> &outputs,
                                                   OpParameter *parameter, const lite::Context *ctx,
                                                   const kernel::KernelKey &desc,
                                                   const mindspore::lite::PrimitiveC *primitive) {
  if (parameter == nullptr) {
    MS_LOG(ERROR) << "input parameter is null!";
    return nullptr;
  }
  auto kernel = new (std::nothrow) ArithmeticFP16CPUKernel(parameter, inputs, outputs, ctx, primitive);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "Create kernel failed, name: " << parameter->name_;
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init kernel failed, name: " << parameter->name_
                  << ", type: " << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(parameter->type_));
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Mul, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Add, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Sub, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Div, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_FloorMod, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_FloorDiv, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_LogicalAnd, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_LogicalOr, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Maximum, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Minimum, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_NotEqual, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Equal, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Less, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_LessEqual, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Greater, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_GreaterEqual, CpuArithmeticFp16KernelCreator)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Eltwise, CpuArithmeticFp16KernelCreator)
}  // namespace mindspore::kernel
