/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
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

#ifndef LITE_MINDSPORE_LITE_C_OPS_DE_DEPTHWISE_CONV2_D_H_
#define LITE_MINDSPORE_LITE_C_OPS_DE_DEPTHWISE_CONV2_D_H_

#include <vector>
#include <set>
#include <cmath>
#include "ir/dtype/type_id.h"
#include "src/ops/primitive_c.h"

namespace mindspore {
namespace lite {
class DeDepthwiseConv2D : public PrimitiveC {
 public:
#ifdef PRIMITIVE_WRITEABLE
  MS_DECLARE_PARENT(DeDepthwiseConv2D, PrimitiveC);
  DeDepthwiseConv2D() = default;
  explicit DeDepthwiseConv2D(schema::PrimitiveT *primitive) : PrimitiveC(primitive) {}
  void SetFormat(int format);
  void SetChannelIn(int channel_in);
  void SetChannelMultiplier(int channel_multiplier);
  void SetKernelW(int kernel_w);
  void SetKernelH(int kernel_h);
  void SetStrideW(int stride_w);
  void SetStrideH(int stride_h);
  void SetPadMode(int pad_mode);
  void SetPadUp(int pad_up);
  void SetPadDown(int pad_down);
  void SetPadLeft(int pad_left);
  void SetPadRight(int pad_right);
  void SetDilateW(int dilate_w);
  void SetDilateH(int dilate_h);
  void SetHasBias(bool has_bias);
  void SetActivationType(int activation_type);
#else
  explicit DeDepthwiseConv2D(schema::Primitive *primitive) : PrimitiveC(primitive) {}

  schema::Primitive *Init(schema::Primitive *primitive) {
    flatbuffers::FlatBufferBuilder fbb(1024);

    auto attr = primitive->value_as_DeDepthwiseConv2D();
    MS_ASSERT(attr != nullptr);

    auto val_offset = schema::CreateDeDepthwiseConv2D(fbb, attr->format(), attr->channelIn(), attr->channelMultiplier(),
                                             attr->kernelW(), attr->kernelH(), attr->strideW(), attr->strideH(),
                                             attr->padMode(), attr->padUp(), attr->padDown(), attr->padLeft(),
                                             attr->padRight(), attr->dilateW(), attr->dilateH(),
                                             attr->hasBias(), attr->activationType());
    auto prim_offset = schema::CreatePrimitive(fbb, schema::PrimitiveType_DeDepthwiseConv2D, val_offset.o);
    fbb.Finish(prim_offset);

    auto buf = fbb.GetBufferPointer();
    MS_ASSERT(buf != nullptr);
    auto buf_bak = new char[fbb.GetSize()];
    memcpy(buf_bak, buf, fbb.GetSize());

    auto root = flatbuffers::GetRoot<schema::Primitive>(buf_bak);
    auto prim = const_cast<schema::Primitive *>(root);

    delete[] buf_bak;
    fbb.Clear();
    return prim;
  }
#endif
  int InferShape(std::vector<lite::tensor::Tensor *> inputs_, std::vector<lite::tensor::Tensor *> outputs_) override;
  int GetFormat() const;
  int GetChannelIn() const;
  int GetChannelMultiplier() const;
  int GetKernelW() const;
  int GetKernelH() const;
  int GetStrideW() const;
  int GetStrideH() const;
  int GetPadMode() const;
  int GetPadUp() const;
  int GetPadDown() const;
  int GetPadLeft() const;
  int GetPadRight() const;
  int GetDilateW() const;
  int GetDilateH() const;
  bool GetHasBias() const;
  int GetActivationType() const;

  int PadUp() const { return this->pad_u_; }
  int PadDown() const { return this->pad_d_; }
  int PadLeft() const { return this->pad_l_; }
  int PadRight() const { return this->pad_r_; }

 protected:
  int pad_u_ = 0;
  int pad_d_ = 0;
  int pad_l_ = 0;
  int pad_r_ = 0;
};
}  // namespace lite
}  // namespace mindspore

#endif  // LITE_MINDSPORE_LITE_C_OPS_DE_DEPTHWISE_CONV2_D_H_
