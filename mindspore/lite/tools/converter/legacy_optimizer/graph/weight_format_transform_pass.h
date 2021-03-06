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

#ifndef MINDSPORE_LITE_TOOLS_CONVERTER_LEGACY_OPTIMIZER_WEIGHT_FORMAT_TRANSFORM_PASS_H
#define MINDSPORE_LITE_TOOLS_CONVERTER_LEGACY_OPTIMIZER_WEIGHT_FORMAT_TRANSFORM_PASS_H

#include "tools/converter/optimizer.h"
#include "tools/common/graph_util.h"
#include "tools/converter/converter_flags.h"

namespace mindspore {
namespace lite {
class WeightFormatTransformPass : public GraphPass {
 public:
  WeightFormatTransformPass() = default;

  ~WeightFormatTransformPass() override = default;

  void SetQuantType(QuantType quantType);

  void SetFmkType(converter::FmkType fmkType);

  void SetDstFormat(Format format);

  STATUS Run(MetaGraphT *graph) override;

 private:
  STATUS QuantDataFormatTrans(MetaGraphT *graph);

  STATUS NonQuantDataFormatTrans(MetaGraphT *graph);

 private:
  QuantType quantType = QuantType_QUANT_NONE;
  converter::FmkType fmkType = converter::FmkType_TF;
  Format dstFormat = Format_NUM_OF_FORMAT;
};
}  // namespace lite
}  // namespace mindspore

#endif  // MINDSPORE_LITE_TOOLS_CONVERTER_LEGACY_OPTIMIZER_WEIGHT_FORMAT_TRANSFORM_PASS_H
