#ifndef BESS_MODULES_URL_FILTER_H_
#define BESS_MODULES_URL_FILTER_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/tcp_flow_reconstruct.h"
#include "../utils/trie.h"

using bess::utils::TcpFlowReconstruct;
using bess::utils::Trie;

// A helper class that defines a TCP flow
class Flow {
 public:
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t padding;

  Flow() : src_ip(0), dst_ip(0), src_port(0), dst_port(0), padding(0) {}

  bool operator==(const Flow &other) const {
    return src_ip == other.src_ip && src_port == other.src_port &&
           dst_ip == other.dst_ip && dst_port == other.dst_port;
  }
};

// Hash function for std::unordered_map
struct FlowHash {
  std::size_t operator()(const Flow &f) const {
    static_assert(sizeof(Flow) == 2 * sizeof(uint64_t),
                  "Flow must be 16 bytes.");
    const uint64_t *flowdata = reinterpret_cast<const uint64_t *>(&f);
    uint32_t init_val = 0;
#if __SSE4_2__ && __x86_64
    init_val = crc32c_sse42_u64(*flowdata++, init_val);
    init_val = crc32c_sse42_u64(*flowdata++, init_val);
#else
    init_val = rte_hash_crc(flowdata, sizeof(Flow), init_val);
#endif
    return init_val;
  }
};

// A module of HTTP URL filtering. Ends an HTTP connection if the Host field
// matches the blacklist.
// igate/ogate 0: traffic from internal network to external network
// igate/ogate 1: traffic from external network to internal network
class UrlFilter final : public Module {
 public:
  typedef std::pair<std::string, std::string> Url;

  static const Commands cmds;
  static const gate_idx_t kNumIGates = 2;
  static const gate_idx_t kNumOGates = 2;

  pb_error_t Init(const bess::pb::UrlFilterArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::UrlFilterArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

 private:
  std::unordered_map<std::string, Trie> blacklist_;
  std::unordered_map<Flow, TcpFlowReconstruct, FlowHash> flow_cache_;
  std::unordered_map<Flow, uint64_t, FlowHash> blocked_flows_;
};

#endif  // BESS_MODULES_URL_FILTER_H_