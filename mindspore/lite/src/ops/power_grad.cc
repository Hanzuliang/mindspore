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

#include "src/ops/power_grad.h"

namespace mindspore {
namespace lite {
#ifdef PRIMITIVE_WRITEABLE
float PowerGrad::GetPower() const { return this->primitive_->value.AsPowerGrad()->power; }
float PowerGrad::GetScale() const { return this->primitive_->value.AsPowerGrad()->scale; }
float PowerGrad::GetShift() const { return this->primitive_->value.AsPowerGrad()->shift; }

void PowerGrad::SetPower(float power) { this->primitive_->value.AsPowerGrad()->power = power; }
void PowerGrad::SetScale(float scale) { this->primitive_->value.AsPowerGrad()->scale = scale; }
void PowerGrad::SetShift(float shift) { this->primitive_->value.AsPowerGrad()->shift = shift; }

#else

float PowerGrad::GetPower() const { return this->primitive_->value_as_PowerGrad()->power(); }
float PowerGrad::GetScale() const { return this->primitive_->value_as_PowerGrad()->scale(); }
float PowerGrad::GetShift() const { return this->primitive_->value_as_PowerGrad()->shift(); }

#endif
}  // namespace lite
}  // namespace mindspore
