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

#include "tools/benchmark/benchmark.h"
#define __STDC_FORMAT_MACROS
#include <cinttypes>
#undef __STDC_FORMAT_MACROS
#include <cmath>
#include <algorithm>
#include <utility>
#include <cfloat>
#include "src/common/common.h"
#include "include/ms_tensor.h"
#include "include/context.h"
#include "src/runtime/runtime_api.h"

namespace mindspore {
namespace lite {
static const char *DELIM_COLON = ":";
static const char *DELIM_COMMA = ",";
static const char *DELIM_SLASH = "/";

int Benchmark::GenerateRandomData(size_t size, void *data) {
  MS_ASSERT(data != nullptr);
  char *castedData = static_cast<char *>(data);
  for (size_t i = 0; i < size; i++) {
    castedData[i] = static_cast<char>(i);
  }
  return RET_OK;
}

int Benchmark::GenerateInputData() {
  for (auto tensor : msInputs) {
    MS_ASSERT(tensor != nullptr);
    auto inputData = tensor->MutableData();
    if (inputData == nullptr) {
      MS_LOG(ERROR) << "MallocData for inTensor failed";
      return RET_ERROR;
    }
    MS_ASSERT(tensor->GetData() != nullptr);
    auto tensorByteSize = tensor->Size();
    auto status = GenerateRandomData(tensorByteSize, inputData);
    if (status != 0) {
      std::cerr << "GenerateRandomData for inTensor failed: " << status << std::endl;
      MS_LOG(ERROR) << "GenerateRandomData for inTensor failed:" << status;
      return status;
    }
  }
  return RET_OK;
}

int Benchmark::LoadInput() {
  if (_flags->inDataPath.empty()) {
    auto status = GenerateInputData();
    if (status != 0) {
      std::cerr << "Generate input data error " << status << std::endl;
      MS_LOG(ERROR) << "Generate input data error " << status;
      return status;
    }
  } else {
    auto status = ReadInputFile();
    if (status != 0) {
      std::cerr << "ReadInputFile error, " << status << std::endl;
      MS_LOG(ERROR) << "ReadInputFile error, " << status;
      return status;
    }
  }
  return RET_OK;
}

int Benchmark::ReadInputFile() {
  if (msInputs.empty()) {
    return RET_OK;
  }

  if (this->_flags->inDataType == kImage) {
    MS_LOG(ERROR) << "Not supported image input";
    return RET_ERROR;
  } else {
    for (size_t i = 0; i < _flags->input_data_list.size(); i++) {
      auto cur_tensor = msInputs.at(i);
      MS_ASSERT(cur_tensor != nullptr);
      size_t size;
      char *binBuf = ReadFile(_flags->input_data_list[i].c_str(), &size);
      if (binBuf == nullptr) {
        MS_LOG(ERROR) << "ReadFile return nullptr";
        return RET_ERROR;
      }
      auto tensorDataSize = cur_tensor->Size();
      if (size != tensorDataSize) {
        std::cerr << "Input binary file size error, required: %zu, in fact: %zu" << tensorDataSize << size << std::endl;
        MS_LOG(ERROR) << "Input binary file size error, required: " << tensorDataSize << ", in fact: " << size;
        delete binBuf;
        return RET_ERROR;
      }
      auto inputData = cur_tensor->MutableData();
      memcpy(inputData, binBuf, tensorDataSize);
      delete[](binBuf);
    }
  }
  return RET_OK;
}

// calibData is FP32
int Benchmark::ReadCalibData() {
  const char *calibDataPath = _flags->calibDataPath.c_str();
  // read calib data
  std::ifstream inFile(calibDataPath);
  if (!inFile.good()) {
    std::cerr << "file: " << calibDataPath << " is not exist" << std::endl;
    MS_LOG(ERROR) << "file: " << calibDataPath << " is not exist";
    return RET_ERROR;
  }

  if (!inFile.is_open()) {
    std::cerr << "file: " << calibDataPath << " open failed" << std::endl;
    MS_LOG(ERROR) << "file: " << calibDataPath << " open failed";
    inFile.close();
    return RET_ERROR;
  }

  std::string line;

  MS_LOG(INFO) << "Start reading calibData file";
  std::string tensorName;
  while (!inFile.eof()) {
    getline(inFile, line);
    std::stringstream stringLine1(line);
    size_t dim = 0;
    stringLine1 >> tensorName >> dim;
    std::vector<size_t> dims;
    size_t shapeSize = 1;
    for (size_t i = 0; i < dim; i++) {
      size_t tmpDim;
      stringLine1 >> tmpDim;
      dims.push_back(tmpDim);
      shapeSize *= tmpDim;
    }

    getline(inFile, line);
    std::stringstream stringLine2(line);
    std::vector<float> tensorData;
    for (size_t i = 0; i < shapeSize; i++) {
      float tmpData;
      stringLine2 >> tmpData;
      tensorData.push_back(tmpData);
    }

    auto *checkTensor = new CheckTensor(dims, tensorData);
    this->calibData.insert(std::make_pair(tensorName, checkTensor));
  }
  inFile.close();
  MS_LOG(INFO) << "Finish reading calibData file";
  return RET_OK;
}

// tensorData need to be converter first
float Benchmark::CompareData(const std::string &nodeName, std::vector<int> msShape, float *msTensorData) {
  auto iter = this->calibData.find(nodeName);
  if (iter != this->calibData.end()) {
    std::vector<size_t> castedMSShape;
    size_t shapeSize = 1;
    for (int64_t dim : msShape) {
      castedMSShape.push_back(size_t(dim));
      shapeSize *= dim;
    }

    CheckTensor *calibTensor = iter->second;
    if (calibTensor->shape != castedMSShape) {
      std::ostringstream oss;
      oss << "Shape of mslite output(";
      for (auto dim : castedMSShape) {
        oss << dim << ",";
      }
      oss << ") and shape source model output(";
      for (auto dim : calibTensor->shape) {
        oss << dim << ",";
      }
      oss << ") are different";
      std::cerr << oss.str() << std::endl;
      MS_LOG(ERROR) << oss.str().c_str();
      return RET_ERROR;
    }
    size_t errorCount = 0;
    float meanError = 0;
    std::cout << "Data of node " << nodeName << " : ";
    for (size_t j = 0; j < shapeSize; j++) {
      if (j < 50) {
        std::cout << msTensorData[j] << " ";
      }

      if (std::isnan(msTensorData[j]) || std::isinf(msTensorData[j])) {
        std::cerr << "Output tensor has nan or inf data, compare fail" << std::endl;
        MS_LOG(ERROR) << "Output tensor has nan or inf data, compare fail";
        return RET_ERROR;
      }

      auto tolerance = absoluteTolerance + relativeTolerance * fabs(calibTensor->data.at(j));
      auto absoluteError = std::fabs(msTensorData[j] - calibTensor->data.at(j));
      if (absoluteError > tolerance) {
        // just assume that atol = rtol
        meanError += absoluteError / (fabs(calibTensor->data.at(j)) + FLT_MIN);
        errorCount++;
      }
    }
    std::cout << std::endl;
    if (meanError > 0.0f) {
      meanError /= errorCount;
    }

    if (meanError <= 0.0000001) {
      std::cout << "Mean bias of node " << nodeName << " : 0%" << std::endl;
    } else {
      std::cout << "Mean bias of node " << nodeName << " : " << meanError * 100 << "%" << std::endl;
    }
    return meanError;
  } else {
    MS_LOG(INFO) << "%s is not in Source Model output", nodeName.c_str();
    return RET_ERROR;
  }
}

int Benchmark::CompareOutput() {
  std::cout << "================ Comparing Output data ================" << std::endl;
  float totalBias = 0;
  int totalSize = 0;
  bool hasError = false;
  for (const auto &calibTensor : calibData) {
    std::string nodeName = calibTensor.first;
    auto tensors = session->GetOutputsByNodeName(nodeName);
    if (tensors.empty()) {
      MS_LOG(ERROR) << "Cannot find output node: " << nodeName.c_str() << " , compare output data fail.";
      std::cerr << "Cannot find output node: " << nodeName.c_str() << " , compare output data fail." << std::endl;
      return RET_ERROR;
    }
    // make sure tensor size is 1
    if (tensors.size() != 1) {
      MS_LOG(ERROR) << "Only support 1 tensor with a name now.";
      std::cerr << "Only support 1 tensor with a name now." << std::endl;
      return RET_ERROR;
    }
    auto &tensor = tensors.front();
    MS_ASSERT(tensor->GetDataType() == DataType_DT_FLOAT);
    MS_ASSERT(tensor->GetData() != nullptr);
    float bias = CompareData(nodeName, tensor->shape(), static_cast<float *>(tensor->MutableData()));
    if (bias >= 0) {
      totalBias += bias;
      totalSize++;
    } else {
      hasError = true;
      break;
    }
  }

  if (!hasError) {
    float meanBias;
    if (totalSize != 0) {
      meanBias = totalBias / totalSize * 100;
    } else {
      meanBias = 0;
    }

    std::cout << "Mean bias of all nodes: " << meanBias << "%" << std::endl;
    std::cout << "=======================================================" << std::endl << std::endl;

    if (meanBias > this->_flags->accuracyThreshold) {
      MS_LOG(ERROR) << "Mean bias of all nodes is too big: " << meanBias << "%";
      std::cerr << "Mean bias of all nodes is too big: " << meanBias << "%" << std::endl;
      return RET_ERROR;
    } else {
      return RET_OK;
    }
  } else {
    MS_LOG(ERROR) << "Error in CompareData";
    std::cerr << "Error in CompareData" << std::endl;
    std::cout << "=======================================================" << std::endl << std::endl;
    return RET_ERROR;
  }
}

int Benchmark::MarkPerformance() {
  MS_LOG(INFO) << "Running warm up loops...";
  std::cout << "Running warm up loops..." << std::endl;
  for (int i = 0; i < _flags->warmUpLoopCount; i++) {
    auto status = session->RunGraph();
    if (status != 0) {
      MS_LOG(ERROR) << "Inference error " << status;
      std::cerr << "Inference error " << status << std::endl;
      return status;
    }
  }

  MS_LOG(INFO) << "Running benchmark loops...";
  std::cout << "Running benchmark loops..." << std::endl;
  uint64_t timeMin = 1000000;
  uint64_t timeMax = 0;
  uint64_t timeAvg = 0;

  for (int i = 0; i < _flags->loopCount; i++) {
    session->BindThread(true);
    auto start = GetTimeUs();
    auto status = session->RunGraph();
    if (status != 0) {
      MS_LOG(ERROR) << "Inference error " << status;
      std::cerr << "Inference error " << status;
      return status;
    }

    auto end = GetTimeUs();
    auto time = end - start;
    timeMin = std::min(timeMin, time);
    timeMax = std::max(timeMax, time);
    timeAvg += time;

    session->BindThread(false);
  }
  if (_flags->loopCount > 0) {
    timeAvg /= _flags->loopCount;
    MS_LOG(INFO) << "Model = " << _flags->modelPath.substr(_flags->modelPath.find_last_of(DELIM_SLASH) + 1).c_str()
                 << ", NumThreads = " << _flags->numThreads << ", MinRunTime = " << timeMin / 1000.0f
                 << ", MaxRuntime = " << timeMax / 1000.0f << ", AvgRunTime = " << timeAvg / 1000.0f;
    printf("Model = %s, NumThreads = %d, MinRunTime = %f ms, MaxRuntime = %f ms, AvgRunTime = %f ms\n",
           _flags->modelPath.substr(_flags->modelPath.find_last_of(DELIM_SLASH) + 1).c_str(), _flags->numThreads,
           timeMin / 1000.0f, timeMax / 1000.0f, timeAvg / 1000.0f);
  }
  return RET_OK;
}

int Benchmark::MarkAccuracy() {
  MS_LOG(INFO) << "MarkAccuracy";
  std::cout << "MarkAccuracy" << std::endl;
  for (size_t i = 0; i < msInputs.size(); i++) {
    MS_ASSERT(msInputs.at(i) != nullptr);
    MS_ASSERT(msInputs.at(i)->data_type() == TypeId::kNumberTypeFloat32);
    auto inData = reinterpret_cast<float *>(msInputs.at(i)->MutableData());
    std::cout << "InData" << i << ": ";
    for (size_t j = 0; j < 20; j++) {
      std::cout << inData[j] << " ";
    }
    std::cout << std::endl;
  }
  auto status = session->RunGraph();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Inference error " << status;
    std::cerr << "Inference error " << status << std::endl;
    return status;
  }

  status = ReadCalibData();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Read calib data error " << status;
    std::cerr << "Read calib data error " << status << std::endl;
    return status;
  }

  status = CompareOutput();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Compare output error " << status;
    std::cerr << "Compare output error " << status << std::endl;
    return status;
  }
  return RET_OK;
}

int Benchmark::RunBenchmark(const std::string &deviceType) {
  auto startPrepareTime = GetTimeUs();
  // Load graph
  std::string modelName = _flags->modelPath.substr(_flags->modelPath.find_last_of(DELIM_SLASH) + 1);

  MS_LOG(INFO) << "start reading model file";
  std::cout << "start reading model file" << std::endl;
  size_t size = 0;
  char *graphBuf = ReadFile(_flags->modelPath.c_str(), &size);
  if (graphBuf == nullptr) {
    MS_LOG(ERROR) << "Read model file failed while running " << modelName.c_str();
    std::cerr << "Read model file failed while running " << modelName.c_str() << std::endl;
    return RET_ERROR;
  }
  auto model = lite::Model::Import(graphBuf, size);
  if (model == nullptr) {
    MS_LOG(ERROR) << "Import model file failed while running " << modelName.c_str();
    std::cerr << "Import model file failed while running " << modelName.c_str() << std::endl;
    delete[](graphBuf);
    return RET_ERROR;
  }
  delete[](graphBuf);
  auto context = new (std::nothrow) lite::Context;
  if (context == nullptr) {
    MS_LOG(ERROR) << "New context failed while running " << modelName.c_str();
    std::cerr << "New context failed while running " << modelName.c_str() << std::endl;
    return RET_ERROR;
  }
  if (_flags->device == "CPU") {
    context->device_ctx_.type = lite::DT_CPU;
  } else if (_flags->device == "GPU") {
    context->device_ctx_.type = lite::DT_GPU;
  } else {
    context->device_ctx_.type = lite::DT_NPU;
  }

  if (_flags->cpuBindMode == -1) {
    context->cpu_bind_mode_ = MID_CPU;
  } else if (_flags->cpuBindMode == 0) {
    context->cpu_bind_mode_ = HIGHER_CPU;
  } else {
    context->cpu_bind_mode_ = NO_BIND;
  }
  context->thread_num_ = _flags->numThreads;
  context->float16_priority = _flags->fp16Priority;
  session = session::LiteSession::CreateSession(context);
  delete (context);
  if (session == nullptr) {
    MS_LOG(ERROR) << "CreateSession failed while running ", modelName.c_str();
    std::cout << "CreateSession failed while running ", modelName.c_str();
    return RET_ERROR;
  }
  auto ret = session->CompileGraph(model);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "CompileGraph failed while running ", modelName.c_str();
    std::cout << "CompileGraph failed while running ", modelName.c_str();
    delete (session);
    delete (model);
    return ret;
  }
  msInputs = session->GetInputs();
  auto endPrepareTime = GetTimeUs();
#if defined(__arm__)
  MS_LOG(INFO) << "PrepareTime = " << (endPrepareTime - startPrepareTime) / 1000 << " ms";
  printf("PrepareTime = %lld ms, ", (endPrepareTime - startPrepareTime) / 1000);
#else
  MS_LOG(INFO) << "PrepareTime = " << (endPrepareTime - startPrepareTime) / 1000 << " ms ";
  printf("PrepareTime = %ld ms, ", (endPrepareTime - startPrepareTime) / 1000);
#endif

  // Load input
  MS_LOG(INFO) << "start generate input data";
  auto status = LoadInput();
  if (status != 0) {
    MS_LOG(ERROR) << "Generate input data error";
    delete (session);
    delete (model);
    return status;
  }
  if (!_flags->calibDataPath.empty()) {
    status = MarkAccuracy();
    for (auto &data : calibData) {
      data.second->shape.clear();
      data.second->data.clear();
      delete data.second;
    }
    calibData.clear();
    if (status != 0) {
      MS_LOG(ERROR) << "Run MarkAccuracy error: " << status;
      std::cout << "Run MarkAccuracy error: " << status << std::endl;
      delete (session);
      delete (model);
      return status;
    }
  } else {
    status = MarkPerformance();
    if (status != 0) {
      MS_LOG(ERROR) << "Run MarkPerformance error: " << status;
      std::cout << "Run MarkPerformance error: " << status << std::endl;
      delete (session);
      delete (model);
      return status;
    }
  }
  delete (session);
  delete (model);
  return RET_OK;
}

void BenchmarkFlags::InitInputDataList() {
  char *input_list = new char[this->inDataPath.length() + 1];
  snprintf(input_list, this->inDataPath.length() + 1, "%s", this->inDataPath.c_str());
  char *cur_input;
  const char *split_c = ",";
  cur_input = strtok(input_list, split_c);
  while (cur_input != nullptr) {
    input_data_list.emplace_back(cur_input);
    cur_input = strtok(nullptr, split_c);
  }
  delete[] input_list;
}

void BenchmarkFlags::InitResizeDimsList() {
  std::string content;
  content = this->resizeDimsIn;
  std::vector<int64_t> shape;
  auto shapeStrs = StringSplit(content, std::string(DELIM_COLON));
  for (const auto &shapeStr : shapeStrs) {
    shape.clear();
    auto dimStrs = StringSplit(shapeStr, std::string(DELIM_COMMA));
    std::cout << "Resize Dims: ";
    for (const auto &dimStr : dimStrs) {
      std::cout << dimStr << " ";
      shape.emplace_back(static_cast<int64_t>(std::stoi(dimStr)));
    }
    std::cout << std::endl;
    this->resizeDims.emplace_back(shape);
  }
}

int Benchmark::Init() {
  if (this->_flags == nullptr) {
    return 1;
  }
  MS_LOG(INFO) << "ModelPath = " << this->_flags->modelPath;
  MS_LOG(INFO) << "InDataPath = " << this->_flags->inDataPath;
  MS_LOG(INFO) << "InDataType = " << this->_flags->inDataTypeIn;
  MS_LOG(INFO) << "LoopCount = " << this->_flags->loopCount;
  MS_LOG(INFO) << "DeviceType = " << this->_flags->device;
  MS_LOG(INFO) << "AccuracyThreshold = " << this->_flags->accuracyThreshold;
  MS_LOG(INFO) << "WarmUpLoopCount = " << this->_flags->warmUpLoopCount;
  MS_LOG(INFO) << "NumThreads = " << this->_flags->numThreads;
  MS_LOG(INFO) << "Fp16Priority = " << this->_flags->fp16Priority;
  MS_LOG(INFO) << "calibDataPath = " << this->_flags->calibDataPath;

  if (this->_flags->loopCount < 1) {
    MS_LOG(ERROR) << "LoopCount:" << this->_flags->loopCount << " must be greater than 0";
    std::cerr << "LoopCount:" << this->_flags->loopCount << " must be greater than 0" << std::endl;
    return RET_ERROR;
  }

  if (this->_flags->numThreads < 1) {
    MS_LOG(ERROR) << "numThreads:" << this->_flags->numThreads << " must be greater than 0";
    std::cerr << "numThreads:" << this->_flags->numThreads << " must be greater than 0" << std::endl;
    return RET_ERROR;
  }

  if (this->_flags->cpuBindMode == -1) {
    MS_LOG(INFO) << "cpuBindMode = MID_CPU";
    std::cout << "cpuBindMode = MID_CPU" << std::endl;
  } else if (this->_flags->cpuBindMode == 1) {
    MS_LOG(INFO) << "cpuBindMode = HIGHER_CPU";
    std::cout << "cpuBindMode = HIGHER_CPU" << std::endl;
  } else {
    MS_LOG(INFO) << "cpuBindMode = NO_BIND";
    std::cout << "cpuBindMode = NO_BIND" << std::endl;
  }

  this->_flags->inDataType = this->_flags->inDataTypeIn == "img" ? kImage : kBinary;

  if (_flags->modelPath.empty()) {
    MS_LOG(ERROR) << "modelPath is required";
    std::cerr << "modelPath is required" << std::endl;
    return 1;
  }
  _flags->InitInputDataList();
  _flags->InitResizeDimsList();
  if (!_flags->resizeDims.empty() && _flags->resizeDims.size() != _flags->input_data_list.size()) {
    MS_LOG(ERROR) << "Size of input resizeDims should be equal to size of input inDataPath";
    std::cerr << "Size of input resizeDims should be equal to size of input inDataPath" << std::endl;
    return RET_ERROR;
  }

  if (_flags->device != "CPU" && _flags->device != "GPU") {
    MS_LOG(ERROR) << "Device type:" << _flags->device << " is not supported.";
    std::cerr << "Device type:" << _flags->device << " is not supported." << std::endl;
    return RET_ERROR;
  }

  return RET_OK;
}

Benchmark::~Benchmark() {
  for (auto iter : this->calibData) {
    delete (iter.second);
  }
  this->calibData.clear();
}

int RunBenchmark(int argc, const char **argv) {
  BenchmarkFlags flags;
  Option<std::string> err = flags.ParseFlags(argc, argv);

  if (err.IsSome()) {
    std::cerr << err.Get() << std::endl;
    std::cerr << flags.Usage() << std::endl;
    return RET_ERROR;
  }

  if (flags.help) {
    std::cerr << flags.Usage() << std::endl;
    return RET_OK;
  }

  Benchmark mBenchmark(&flags);
  auto status = mBenchmark.Init();
  if (status != 0) {
    MS_LOG(ERROR) << "Benchmark init Error : " << status;
    std::cerr << "Benchmark init Error : " << status << std::endl;
    return RET_ERROR;
  }

  if (flags.device == "GPU") {
    status = mBenchmark.RunBenchmark("GPU");
  } else if (flags.device == "CPU") {
    status = mBenchmark.RunBenchmark("CPU");
  } else {
    MS_LOG(ERROR) << "Device type" << flags.device << " not support.";
    std::cerr << "Device type" << flags.device << " not support." << std::endl;
    return RET_ERROR;
  }

  if (status != 0) {
    MS_LOG(ERROR) << "Run Benchmark " << flags.modelPath.substr(flags.modelPath.find_last_of(DELIM_SLASH) + 1).c_str()
                  << " Failed : " << status;
    std::cerr << "Run Benchmark " << flags.modelPath.substr(flags.modelPath.find_last_of(DELIM_SLASH) + 1).c_str()
              << " Failed : " << status << std::endl;
    return RET_ERROR;
  }

  MS_LOG(INFO) << "Run Benchmark " << flags.modelPath.substr(flags.modelPath.find_last_of(DELIM_SLASH) + 1).c_str()
               << " Success.";
  std::cout << "Run Benchmark " << flags.modelPath.substr(flags.modelPath.find_last_of(DELIM_SLASH) + 1).c_str()
            << " Success." << std::endl;
  return RET_OK;
}
}  // namespace lite
}  // namespace mindspore
