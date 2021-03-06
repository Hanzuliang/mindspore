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

#include <dirent.h>
#include <stdio.h>
#include <fstream>
#include <tuple>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <utility>
#include <map>
#include "debug/debugger/debugger.h"
#include "debug/data_dump_parser.h"
#include "pipeline/jit/pipeline.h"
#include "backend/session/anf_runtime_algorithm.h"
#include "runtime/device/kernel_runtime_manager.h"

#include "utils/system/file_system.h"
#include "utils/system/env.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using debugger::EventReply;
using debugger::GraphProto;
using debugger::ModelProto;
using debugger::TensorProto;
using debugger::WatchCondition;
using debugger::WatchCondition_Condition_inf;
using debugger::WatchCondition_Condition_nan;
using debugger::WatchNode;
using debugger::WatchpointHit;

#define CHUNK_SIZE 1024 * 1024 * 3

namespace mindspore {

DebuggerPtr Debugger::debugger_ = nullptr;
std::mutex Debugger::instance_lock_;

Debugger::Debugger()
    : grpc_client_(nullptr),
      debug_services_(nullptr),
      device_id_(0),
      device_target_(""),
      num_step_(0),
      debugger_enabled_(false),
      run_level_(""),
      node_name_(""),
      cur_name_(""),
      is_dataset_graph_(false),
      partial_memory_(false),
      last_overflow_bin_(0),
      overflow_bin_path_(""),
      ssl_certificate(true),
      certificate_dir(""),
      certificate_passphrase("") {}

void Debugger::Init(const uint32_t device_id, const std::string device_target) {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  // save device_id
  MS_LOG(INFO) << "Debugger got device_id: " << device_id;
  device_id_ = device_id;
  MS_LOG(INFO) << "Debugger got device_target: " << device_target;
  device_target_ = device_target;
}

void Debugger::EnableDebugger() {
  // reset some of the class members
  num_step_ = 0;
  debugger_enabled_ = false;
  partial_memory_ = false;
  grpc_client_ = nullptr;
  debug_services_ = nullptr;

  // see if dump is enabled
  bool dump_enabled = false;
  if (device_target_ == kGPUDevice) {
    auto runtime_instance = device::KernelRuntimeManager::Instance().GetSingleKernelRuntime(kGPUDevice, device_id_);
    MS_EXCEPTION_IF_NULL(runtime_instance);
    dump_enabled = runtime_instance->DumpDataEnabled();
  }

  // get env variables to configure debugger
  const char *env_enable_str = std::getenv("ENABLE_MS_DEBUGGER");
  if (env_enable_str != nullptr) {
    MS_LOG(INFO) << "Getenv ENABLE_MS_DEBUGGER: " << env_enable_str;
    if (std::strcmp(env_enable_str, "1") == 0) {
      debugger_enabled_ = true;
    }
  }

  if (!debugger_enabled_ && !dump_enabled) {
    MS_LOG(WARNING) << "Not enabling debugger. Set environment variable ENABLE_MS_DEBUGGER=1 to enable debugger.";
    return;
  }

  // configure grpc host
  const char *env_host_str = std::getenv("MS_DEBUGGER_HOST");
  std::string host;
  if (env_host_str != nullptr) {
    MS_LOG(INFO) << "Getenv MS_DEBUGGER_HOST: " << env_host_str;
    host = std::string(env_host_str);
  } else {
    MS_LOG(WARNING) << "Environment variable MS_DEBUGGER_HOST doesn't exist. Using default debugger host: localhost";
    host = "localhost";
  }
  // configure grpc port
  const char *env_port_str = std::getenv("MS_DEBUGGER_PORT");
  std::string port;
  if (env_port_str != nullptr) {
    MS_LOG(INFO) << "Getenv MS_DEBUGGER_PORT: " << env_port_str;
    port = std::string(env_port_str);
  } else {
    MS_LOG(WARNING) << "Environment variable MS_DEBUGGER_PORT doesn't exist. Using default debugger port: 50051";
    port = "50051";
  }

  // configure partial memory reuse
  const char *env_partial_mem_str = std::getenv("MS_DEBUGGER_PARTIAL_MEM");
  if (env_partial_mem_str != nullptr) {
    MS_LOG(INFO) << "Getenv MS_DEBUGGER_PARTIAL_MEM: " << env_partial_mem_str;
    if (std::strcmp(env_partial_mem_str, "1") == 0) {
      partial_memory_ = true;
    }
  }
  // switch memory reuse on or off
  auto context_ptr = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context_ptr);
  context_ptr->set_enable_mem_reuse(partial_memory_);
  // print some message about memory reuse to user
  if (partial_memory_) {
    MS_LOG(WARNING) << "Partial Memory Reuse is enabled. Note: 1. Please only set watchpoints before running the first "
                       "step. 2. Tensor values are only available for nodes that are watched by any watchpoint.";
  } else {
    MS_LOG(WARNING) << "Memory Reuse is disabled. Set environment variable MS_DEBUGGER_PARTIAL_MEM=1 to reduce memory "
                       "usage for large models.";
  }
#ifdef ENABLE_D
  // set operation overflow info
  overflow_bin_path_ = DataDumpParser::GetInstance().GetOpOverflowBinPath(graph_ptr_->graph_id(), device_id_);
  // new overflow dump files will have a timestamp greater than last_overflow_bin_
  last_overflow_bin_ = 0;
  DIR *d;
  d = opendir(overflow_bin_path_.c_str());
  if (d != nullptr) {
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_type == DT_REG) {
        std::string file_path = overflow_bin_path_;
        file_path.append(dir->d_name);
        std::size_t found = file_path.find_last_of(".");
        if (found == std::string::npos) {
          continue;
        }
        std::string overflow_time = file_path.substr(found + 1);
        if (stod(overflow_time) <= last_overflow_bin_) {
          MS_LOG(INFO) << "Old op overflow bin folder" << file_path;
          continue;
        }
        last_overflow_bin_ = stod(overflow_time);
      }
    }
    MS_LOG(INFO) << "last op overflow bin folder" << last_overflow_bin_;
  }
#endif

  // initialize grpc client
  if (debugger_enabled_) {
    SetDebuggerConfFromJsonFile();
    grpc_client_ = std::make_unique<GrpcClient>(host, port, ssl_certificate, certificate_dir, certificate_passphrase);
  }

  debug_services_ = std::make_unique<DebugServices>();
}

void Debugger::Reset() {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  // reset components
  device_id_ = 0;
  device_target_ = "";
  num_step_ = 0;
  debugger_enabled_ = false;
  is_dataset_graph_ = false;
  partial_memory_ = false;
  graph_ptr_ = nullptr;
  grpc_client_ = nullptr;
  debug_services_ = nullptr;
  last_overflow_bin_ = 0;
  overflow_bin_path_ = "";
  stream_task_to_opname_.clear();
}

void Debugger::PreExecute(const KernelGraphPtr &graph_ptr) {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  // check and save graph_ptr, suspend if graph is new
  CheckGraphPtr(graph_ptr);
}

void Debugger::PostExecute() {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  // analyze tensor data and send the watchpoints been hit
  if (run_level_ == "node") {
    MS_LOG(INFO) << "Debugger is in node level mode ";
    return;
  }
  if (debugger_enabled_ && !is_dataset_graph_) {
    if (device_target_ != kGPUDevice) {
      num_step_++;
      MS_LOG(INFO) << "Debugger suspend at end of step; number of steps executed: " << num_step_;
      SendWatchpointsAndSuspend(CheckWatchpoints());
    } else {
      CommandLoop();
    }
  }
}

bool Debugger::ReadNodeDataRequired() {
  if (debugger_enabled_ && !is_dataset_graph_) {
    auto watchpoint_table = debug_services_->GetWatchpointTable();
    auto is_watchpoint = debug_services_->IsWatchPoint(cur_name_, watchpoint_table);
    // if node has a watchpoint on it, is next_to node, or continue_to node then read the kernel tensor data
    if (is_watchpoint || (run_level_ == "node" && (node_name_ == "" || node_name_ == cur_name_))) {
      return true;
    }
  }
  return false;
}

void Debugger::PostExecuteNode() {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  if (debugger_enabled_ && !is_dataset_graph_) {
    auto watchpoint_table = debug_services_->GetWatchpointTable();
    auto is_watchpoint = debug_services_->IsWatchPoint(cur_name_, watchpoint_table);

    // if kernel is watchpoint,and get hit. suspend.
    if (is_watchpoint) {
      auto hits = CheckSingleWatchpoint(cur_name_);
      if (!hits.empty()) {
        SendWatchpointsAndSuspend(hits);
      }
    }
    // if kernel is not watchpoint and is next_to or continue_to node, suspend.
    if (run_level_ == "node" && (node_name_ == "" || node_name_ == cur_name_)) {
      CommandLoop();
    }
    return;
  }
}

void Debugger::PostDebugOp() {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  // suspend if debugger is enabled
  if (debugger_enabled_ && !is_dataset_graph_) {
    MS_LOG(INFO) << "Debugger suspend at debug_op";
    CommandLoop();
  }
}

std::map<std::pair<uint32_t, uint32_t>, std::string> &Debugger::GetStreamTaskToOpnameMap() {
  return stream_task_to_opname_;
}

void Debugger::CheckGraphPtr(const KernelGraphPtr &graph_ptr) {
  if (graph_ptr_ != graph_ptr) {
    MS_LOG(INFO) << "Debugger got new graph: " << graph_ptr->graph_id();
    // save new graph_ptr
    graph_ptr_ = graph_ptr;
    // check if it is dataset graph
    CheckDatasetGraph();
    if (!is_dataset_graph_) {
      // only try to enable debugger if it is not a dataset graph
      EnableDebugger();
      if (debugger_enabled_) {
        // get graph proto and send to mindinsight
        SendGraphAndSuspend(GetGraphProto());
      }
    }
  }
}

void Debugger::CheckDatasetGraph() {
  // print parameter node names
  const auto &params = graph_ptr_->inputs();
  for (const auto &param : params) {
    MS_LOG(INFO) << "param: " << param->fullname_with_scope();
  }
  // check if there is GetNext or InitDataSetQueue node
  const auto &nodes = graph_ptr_->execution_order();
  for (const auto &node : nodes) {
    auto node_name = AnfAlgo::GetCNodeName(node);
    MS_LOG(INFO) << "node: " << node->fullname_with_scope();
    if (node_name == "GetNext" || node_name == "InitDataSetQueue") {
      MS_LOG(WARNING) << "Not enabling debugger for graph " << graph_ptr_->graph_id() << ": found dataset graph node "
                      << node_name;
      is_dataset_graph_ = true;
      return;
    }
  }
  is_dataset_graph_ = false;
}

GraphProto Debugger::GetGraphProto() const {
  // convert kernel graph to debugger modelproto
  ModelProto model = GetDebuggerFuncGraphProto(graph_ptr_);
  return model.graph();
}

void Debugger::SendGraphAndSuspend(const GraphProto &graph_proto) {
  // prepare metadata
  std::string device_name = std::to_string(device_id_) + ":" + std::to_string(graph_ptr_->graph_id());
  Metadata metadata;
  metadata.set_device_name(device_name);
  metadata.set_cur_step(num_step_);
  metadata.set_backend(device_target_);
  metadata.set_cur_node(cur_name_);
  EventReply reply_metadata = grpc_client_->SendMetadata(metadata);
  if (reply_metadata.status() != reply_metadata.OK) {
    MS_LOG(ERROR) << "Error: SendMetadata failed";
  }
  // send graph to mindinght server
  EventReply reply = grpc_client_->SendGraph(graph_proto);
  if (reply.status() != reply.OK) {
    MS_LOG(ERROR) << "Error: SendGraph failed";
  }
  // enter command loop, wait and process commands
  CommandLoop();
}

void Debugger::CommandLoop() {
  // prepare metadata
  std::string device_name = std::to_string(device_id_) + ":" + std::to_string(graph_ptr_->graph_id());
  Metadata metadata;

  metadata.set_device_name(device_name);
  metadata.set_cur_step(num_step_);
  metadata.set_backend(device_target_);
  metadata.set_cur_node(cur_name_);

  // loop exit flag
  bool run = false;
  int num_wait_fail = 0;
  const int max_num_wait_fail = 5;

  while (!run) {
    // wait for command
    EventReply reply = grpc_client_->WaitForCommand(metadata);
    if (reply.status() != reply.OK) {
      MS_LOG(ERROR) << "Error: WaitForCommand failed";
      num_wait_fail++;
      if (num_wait_fail > max_num_wait_fail) {
        MS_LOG(ERROR) << "Maximum number of WaitForCommand retry reached: exiting training session";
        Exit();
      }
      MS_LOG(ERROR) << "Number of consecutive WaitForCommand fail:" << num_wait_fail << "; Retry after "
                    << num_wait_fail << "s";
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 * num_wait_fail));
      continue;
    }

    // get type of the command in reply
    DebuggerCommand cmd = GetCommand(reply);
    if (cmd == DebuggerCommand::kUnknownCMD) {
      MS_LOG(ERROR) << "Error: debugger recieved unknown command";
      continue;
    }

    MS_LOG(INFO) << "recieved command: ";
    switch (cmd) {
      case DebuggerCommand::kUnknownCMD:
        MS_LOG(INFO) << "UnknownCMD";
        break;
      case DebuggerCommand::kExitCMD:
        MS_LOG(INFO) << "ExitCMD";
        Exit();
        break;
      case DebuggerCommand::kRunCMD:
        MS_LOG(INFO) << "RunCMD";
        {
          // print run cmd content
          // get run_level and node_name
          run_level_ = GetRunLevel(reply);
          node_name_ = GetNodeName(reply);

          MS_LOG(INFO) << "run_level: " << run_level_;
          MS_LOG(INFO) << "node_name_: " << node_name_;
        }

        // exit loop
        run = true;
        break;
      case DebuggerCommand::kSetCMD:
        MS_LOG(INFO) << "SetCMD";
        {
          // print set cmd content
          ProtoVector<WatchNode> recieved_nodes = GetWatchnodes(reply);
          for (auto node : recieved_nodes) {
            MS_LOG(INFO) << "node name: " << node.node_name();
            MS_LOG(INFO) << "node type: " << node.node_type();
          }
          MS_LOG(INFO) << "condition: " << GetWatchcondition(reply).condition();
          MS_LOG(INFO) << "id: " << GetWatchpointID(reply);
          MS_LOG(INFO) << "delete: " << GetWatchpointDelete(reply);
        }
        MS_LOG(INFO) << "Setting watchpoint";
        if (GetWatchpointDelete(reply)) {
          RemoveWatchpoint(GetWatchpointID(reply));
        } else {
          SetWatchpoint(GetWatchnodes(reply), GetWatchcondition(reply), GetWatchpointID(reply));
        }
        break;
      case DebuggerCommand::kViewCMD:
        MS_LOG(INFO) << "ViewCMD";
        {
          // print view cmd content
          ProtoVector<TensorProto> received_tensors = GetTensors(reply);
          for (auto tensor : received_tensors) {
            MS_LOG(INFO) << "tensor node name: " << tensor.node_name();
            MS_LOG(INFO) << "tensor slot: " << tensor.slot();
            MS_LOG(INFO) << "tensor finished: " << std::boolalpha << tensor.finished() << std::noboolalpha;
            MS_LOG(INFO) << "tensor iter: " << tensor.iter();
            MS_LOG(INFO) << "tensor truncate: " << std::boolalpha << tensor.truncate() << std::noboolalpha;
          }
        }
        MS_LOG(INFO) << "Sending tensors";
        std::list<TensorProto> tensors = LoadTensors(GetTensors(reply));
        {
          // print view cmd reply
          for (auto tensor : tensors) {
            MS_LOG(INFO) << "tensor node name: " << tensor.node_name();
            MS_LOG(INFO) << "tensor slot: " << tensor.slot();
            MS_LOG(INFO) << "tensor finished: " << std::boolalpha << tensor.finished() << std::noboolalpha;
            MS_LOG(INFO) << "tensor iter: " << tensor.iter();
            MS_LOG(INFO) << "tensor truncate: " << std::boolalpha << tensor.truncate() << std::noboolalpha;
            MS_LOG(INFO) << "tensor dims: ";
            for (auto dim : tensor.dims()) {
              MS_LOG(INFO) << dim << ",";
            }
            MS_LOG(INFO) << "tensor dtype: " << tensor.data_type();
          }
        }
        EventReply send_tensors_reply = grpc_client_->SendTensors(tensors);
        if (send_tensors_reply.status() != send_tensors_reply.OK) {
          MS_LOG(ERROR) << "Error: SendTensors failed";
        }
        break;
    }
  }
}

void AddTensorProtoInfo(TensorProto *tensor_item, TensorProto tensor) {
  tensor_item->set_node_name(tensor.node_name());
  tensor_item->set_slot(tensor.slot());
  tensor_item->set_iter(tensor.iter());
  tensor_item->set_truncate(tensor.truncate());
  tensor_item->clear_tensor_content();
  tensor_item->clear_data_type();
  tensor_item->clear_dims();
}

void Debugger::SetWatchpoint(const ProtoVector<WatchNode> &nodes, const WatchCondition &condition, const int32_t id) {
  std::vector<std::tuple<std::string, bool>> check_node_list;
  std::transform(nodes.begin(), nodes.end(), std::back_inserter(check_node_list),
                 [](WatchNode node) -> std::tuple<std::string, bool> {
                   return make_tuple(node.node_name(), node.node_type() == "scope");
                 });
  debug_services_->AddWatchpoint(id, condition.condition(), check_node_list);
}

void Debugger::RemoveWatchpoint(const int32_t id) { debug_services_->RemoveWatchpoint(id); }

std::list<TensorProto> Debugger::LoadTensors(const ProtoVector<TensorProto> &tensors) const {
  std::vector<std::string> name;
  std::vector<std::string> ret_name;
  std::vector<char *> data_ptr;
  std::vector<unsigned int> data_size;
  std::vector<TypePtr> dtype;
  std::vector<std::vector<int>> shape;

  std::transform(tensors.begin(), tensors.end(), std::back_inserter(name), GetTensorFullName);

  // ret_name will contain tensor names that are found in TensorLoader
  // items in ret_name will be in the same order with tensors if found
  debug_services_->ReadNodesTensors(name, &ret_name, &data_ptr, &data_size, &dtype, &shape);
  std::list<TensorProto> tensor_list;
  unsigned int result_index = 0;

  for (auto tensor : tensors) {
    int size_iter = 0;
    if (result_index >= ret_name.size() || ret_name[result_index] != GetTensorFullName(tensor)) {
      TensorProto tensor_item;
      tensor_item.set_finished(true);
      AddTensorProtoInfo(&tensor_item, tensor);
      tensor_list.push_back(tensor_item);
      continue;
    }
    int tensor_size = data_size[result_index];
    while (size_iter < tensor_size) {
      int chunk_size = CHUNK_SIZE;
      TensorProto tensor_item;
      tensor_item.set_finished(false);
      if (tensor_size - size_iter <= CHUNK_SIZE) {
        chunk_size = tensor_size - size_iter;
        tensor_item.set_finished(true);
      }
      AddTensorProtoInfo(&tensor_item, tensor);
      // return empty tensor if didn't find the requested tensor

      tensor_item.set_tensor_content(data_ptr[result_index] + size_iter, chunk_size);

      tensor_item.set_data_type(GetDebuggerNumberDataType(dtype[result_index]));
      for (auto &elem : shape[result_index]) {
        tensor_item.add_dims(elem);
      }
      // add tensor to result list and increment result_index to check next item in ret_name
      tensor_list.push_back(tensor_item);
      size_iter += CHUNK_SIZE;
    }
    result_index++;
  }
  return tensor_list;
}

void Debugger::Exit() {
  // clear resource before exit
  pipeline::ClearResAtexit();
  std::exit(EXIT_FAILURE);
}

std::list<WatchpointHit> Debugger::CheckWatchpoints() {
  std::vector<std::string> name;
  std::vector<std::string> slot;
  std::vector<int> condition;
  std::vector<unsigned int> watchpoint_id;
  std::vector<std::string> overflow_ops;
#ifdef ENABLE_D
  overflow_ops = CheckOpOverflow();
#endif
  debug_services_->CheckWatchpoints(&name, &slot, &condition, &watchpoint_id, overflow_ops);
  std::list<WatchpointHit> hits;
  for (unsigned int i = 0; i < name.size(); i++) {
    WatchpointHit hit;
    hit.set_id(watchpoint_id[i]);

    // here TensorProto act as a tensor indicator, not sending tensor content
    TensorProto *tensor_item = hit.mutable_tensor();
    tensor_item->set_node_name(name[i]);
    tensor_item->set_slot(slot[i]);
    tensor_item->set_finished(true);

    WatchCondition *condition_item = hit.mutable_watch_condition();
    condition_item->set_condition(debugger::WatchCondition_Condition(condition[i]));

    hits.push_back(hit);
  }
  return hits;
}

std::list<WatchpointHit> Debugger::CheckSingleWatchpoint(std::string watchnode) const {
  auto tensor_loader = debug_services_->tensor_loader();
  auto tensors = tensor_loader->GetNodeTensorMap(watchnode);
  std::list<WatchpointHit> hits;
  for (std::vector<std::shared_ptr<TensorData>>::iterator it = tensors.begin(); it != tensors.end(); ++it) {
    auto cur_tensor = *it;
    std::string name = "";
    std::string slot = "";
    char *data_ptr = nullptr;
    unsigned int data_size = 0;
    int condition = -1;
    unsigned int watchpoint_id = -1;
    WatchpointHit hit;
    debug_services_->CheckSingleWatchpoint(cur_tensor, &name, &slot, &data_ptr, &data_size, &condition, &watchpoint_id);
    if (name != "") {
      hit.set_id(watchpoint_id);
      // here TensorProto act as a tensor indicator, not sending tensor content
      TensorProto *tensor_item = hit.mutable_tensor();
      tensor_item->set_node_name(name);
      tensor_item->set_slot(slot);
      tensor_item->set_finished(true);
      WatchCondition *condition_item = hit.mutable_watch_condition();
      condition_item->set_condition(debugger::WatchCondition_Condition(condition));
      hits.push_back(hit);
    }
  }
  return hits;
}

void Debugger::SendWatchpointsAndSuspend(const std::list<WatchpointHit> &points) {
  // send info about watchpoint
  if (!points.empty()) {
    EventReply reply = grpc_client_->SendWatchpointHits(points);
    if (reply.status() != reply.OK) {
      MS_LOG(ERROR) << "Error: SendWatchpointHits failed";
    }
  }
  // enter command loop
  CommandLoop();
}

DebugServices *Debugger::debug_services() const { return debug_services_.get(); }

bool Debugger::debugger_enabled() const { return debugger_enabled_; }

DebuggerCommand GetCommand(const EventReply &reply) {
  DebuggerCommand cmd = DebuggerCommand::kUnknownCMD;
  switch (reply.cmd_case()) {
    case debugger::EventReply::CmdCase::kExit:
      cmd = DebuggerCommand::kExitCMD;
      break;
    case debugger::EventReply::CmdCase::kRunCmd:
      cmd = DebuggerCommand::kRunCMD;
      break;
    case debugger::EventReply::CmdCase::kSetCmd:
      cmd = DebuggerCommand::kSetCMD;
      break;
    case debugger::EventReply::CmdCase::kViewCmd:
      cmd = DebuggerCommand::kViewCMD;
      break;
    default:
      MS_LOG(ERROR) << "Error: UnknownCMD";
      break;
  }
  return cmd;
}

ProtoVector<WatchNode> GetWatchnodes(const EventReply &reply) {
  if (!reply.has_set_cmd()) {
    MS_LOG(ERROR) << "Error: Not SetCMD, can not get WatchNodes. Returning default value: ProtoVector<WatchNode>().";
    return ProtoVector<WatchNode>();
  }
  return reply.set_cmd().watch_nodes();
}

std::string GetRunLevel(const EventReply &reply) {
  if (!reply.has_run_cmd()) {
    MS_LOG(ERROR) << "Error: Not RunCMD, can not get RunLevel. Returning default value: "
                     "";
    return "";
  }
  return reply.run_cmd().run_level();
}

std::string GetNodeName(const EventReply &reply) {
  if (!reply.has_run_cmd()) {
    MS_LOG(ERROR) << "Error: Not RunCMD, can not get NodeName. Returning default value: "
                     "";
    return "";
  }
  return reply.run_cmd().node_name();
}

WatchCondition GetWatchcondition(const EventReply &reply) {
  if (!reply.has_set_cmd() || !reply.set_cmd().has_watch_condition()) {
    MS_LOG(ERROR) << "Error: Can not get WatchCondition from command. Returning default value: WatchCondition().";
    return WatchCondition();
  }
  return reply.set_cmd().watch_condition();
}

int32_t GetWatchpointID(const EventReply &reply) {
  if (!reply.has_set_cmd()) {
    MS_LOG(ERROR) << "Error: Not SetCMD, can not get Watchpoint ID. Returning default value: 0.";
    return 0;
  }
  return reply.set_cmd().id();
}

bool GetWatchpointDelete(const EventReply &reply) {
  if (!reply.has_set_cmd()) {
    MS_LOG(ERROR) << "Error: Not SetCMD, can not get Watchpoint delete flag. Returning default value: false.";
    return false;
  }
  return reply.set_cmd().delete_();
}

ProtoVector<TensorProto> GetTensors(const EventReply &reply) {
  if (!reply.has_view_cmd()) {
    MS_LOG(ERROR) << "Error: Not ViewCMD, can not get Tensors. Returning default value: ProtoVector<TensorProto>().";
    return ProtoVector<TensorProto>();
  }
  return reply.view_cmd().tensors();
}

std::string GetTensorFullName(const TensorProto &tensor) {
  string node_name = tensor.node_name();
  if (tensor.truncate()) {
    // scopes in node name are seperated by '/'
    // use the name without scope if truncate is true
    std::size_t found = node_name.find_last_of("/");
    node_name = node_name.substr(found + 1);
  }
  return node_name + ":" + tensor.slot() + (tensor.iter() == "" ? "" : ":" + tensor.iter());
}

bool Debugger::partial_memory() { return partial_memory_; }

void Debugger::SetCurNode(std::string cur_name) {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  cur_name_ = cur_name;
}

std::string Debugger::run_level() const { return run_level_; }

void Debugger::SetStepNum(int32_t cur_num_step) {
  // access lock for public method
  std::lock_guard<std::mutex> a_lock(access_lock_);
  num_step_ = cur_num_step;
}

int32_t Debugger::step_num() const { return num_step_; }

uint64_t BytestoInt64(const std::vector<char> &buffer) {
  uint64_t ret;

  ret = ((uint64_t)buffer[7] << 56) | ((uint64_t)buffer[6] << 48) | ((uint64_t)buffer[5] << 40) |
        ((uint64_t)buffer[4] << 32) | (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];

  return ret;
}

#define BUF_SIZ 256
std::vector<std::string> Debugger::CheckOpOverflow() {
  std::vector<double> bin_list;
  std::vector<std::string> op_names;
  DIR *d;
  struct dirent *dir = nullptr;
  d = opendir(overflow_bin_path_.c_str());
  if (d != nullptr) {
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_type == DT_REG) {
        std::string file_path = overflow_bin_path_;
        file_path.append(dir->d_name);
        std::string file_name = dir->d_name;
        std::size_t found = file_name.find_last_of(".");
        if (found == std::string::npos) {
          continue;
        }
        std::string overflow_time = file_name.substr(found + 1);
        if (stod(overflow_time) <= last_overflow_bin_) {
          MS_LOG(INFO) << "File already processed " << file_name;
          continue;
        }
        bin_list.push_back(stod(overflow_time));
        std::fstream infile;
        infile.open(file_path.c_str(), std::ios::binary | std::ios::in);
        infile.seekg(313, std::ios::beg);
        std::vector<char> buffer;
        buffer.resize(BUF_SIZ);
        infile.read(buffer.data(), BUF_SIZ);
        uint64_t stream_id = BytestoInt64(std::vector<char>(buffer.begin() + 8, buffer.end()));
        uint64_t task_id = BytestoInt64(std::vector<char>(buffer.begin() + 16, buffer.end()));
        MS_LOG(INFO) << "Overflow stream_id " << stream_id << ", task_id " << task_id << ".";
        auto op = debugger_->stream_task_to_opname_.find(std::make_pair(stream_id, task_id));
        if (op != debugger_->stream_task_to_opname_.end()) {
          MS_LOG(ERROR) << "Overflow detected on node " << op->second << std::endl;
          op_names.push_back(op->second);
        } else {
          MS_LOG(INFO) << "No overflow is detected " << std::endl;
        }
        infile.close();
      }
    }
  } else {
    MS_LOG(INFO) << "OverFlow bin directory does not exist!";
  }
  closedir(d);

  if (op_names.size()) {
    MS_LOG(ERROR) << "These operation overflows are detected " << op_names;
  }

  for (auto &i : bin_list) {
    if (i > last_overflow_bin_) {
      last_overflow_bin_ = i;
    }
  }

  return op_names;
}

bool Debugger::SetDebuggerConfFromJsonFile() {
  const char *config_path_str = std::getenv("MINDSPORE_CONFIG_PATH");
  if (config_path_str != nullptr) {
    MS_LOG(INFO) << "Getenv MINDSPORE_CONFIG_PATH :" << config_path_str;
  } else {
    MS_LOG(INFO) << "No need debugger config path. please export MINDSPORE_CONFIG_PATH eg: MINDSPORE_CONFIG_PATH=/etc";
    ssl_certificate = false;
    return false;
  }
  char real_path[4096] = {0};
  if (nullptr == realpath(config_path_str, real_path)) {
    MS_LOG(ERROR) << "Env debugger config path error, " << config_path_str;
    ssl_certificate = false;
    return false;
  }
  std::string debugger_config_file = std::string(real_path) + "/debugger_config.json";
  std::shared_ptr<system::FileSystem> fs = system::Env::GetFileSystem();
  MS_EXCEPTION_IF_NULL(fs);
  if (!fs->FileExist(debugger_config_file)) {
    MS_LOG(ERROR) << debugger_config_file << " not exist.";
    ssl_certificate = false;
    return false;
  }

  return ParseDebuggerConfig(debugger_config_file);
}

bool Debugger::ParseDebuggerConfig(const std::string &debugger_config_file) {
  std::ifstream jsonFile(debugger_config_file);
  if (!jsonFile.is_open()) {
    MS_LOG(ERROR) << debugger_config_file << " open failed.";
    ssl_certificate = false;
    return false;
  }
  json j;
  jsonFile >> j;
  if (j.find("DebuggerSettings") == j.end()) {
    MS_LOG(ERROR) << "DebuggerSettings is not exist.";
    ssl_certificate = false;
    return false;
  } else {
    json debuggerSettings = j.at("DebuggerSettings");
    // convert json to string
    std::stringstream ss;
    ss << debuggerSettings;
    std::string cfg = ss.str();
    MS_LOG(INFO) << "Debugger Settings Json: " << cfg;
    if (!IsConfigExist(debuggerSettings)) {
      return false;
    }
    if (!IsConfigValid(debuggerSettings)) {
      return false;
    }
  }
  return true;
}

bool Debugger::IsConfigExist(const nlohmann::json &debuggerSettings) {
  if (debuggerSettings.find("ssl_certificate") == debuggerSettings.end() ||
      debuggerSettings.find("certificate_dir") == debuggerSettings.end() ||
      debuggerSettings.find("certificate_passphrase") == debuggerSettings.end()) {
    MS_LOG(ERROR) << "DebuggerSettings keys is not exist.";
    ssl_certificate = false;
    return false;
  }
  return true;
}

bool Debugger::IsConfigValid(const nlohmann::json &debuggerSettings) {
  auto enable_secure = debuggerSettings.at("ssl_certificate");
  auto certificate_dir_ = debuggerSettings.at("certificate_dir");
  auto certificate_passphrase_ = debuggerSettings.at("certificate_passphrase");
  if (!(enable_secure.is_boolean() && certificate_dir_.is_string() && certificate_passphrase_.is_string())) {
    MS_LOG(ERROR) << "Element's type in Debugger config json is invalid.";
    ssl_certificate = false;
    return false;
  }

  ssl_certificate = enable_secure;
  certificate_dir = certificate_dir_;
  certificate_passphrase = certificate_passphrase_;
  return true;
}

}  // namespace mindspore
