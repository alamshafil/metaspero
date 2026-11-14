#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKERNATIVE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKERNATIVE_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <memory>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
#define KYTY_SRT_NATIVE_WALKER 1
#else
#define KYTY_SRT_NATIVE_WALKER 0
#endif

namespace Libs::Graphics::ShaderRecompiler::IR {

struct NativeWalkPlan {
	bool                               resources = false;
	std::vector<std::vector<uint32_t>> gates;
	std::vector<std::vector<Value>>    descriptors;
	std::vector<Value>                 outputs;
	std::vector<std::vector<uint32_t>> output_gates;

	// Nodes classified unknown at context 0 so native matches the x86 lattice.
	std::unordered_set<const Inst*> unknown_nodes;

	uint32_t user_data         = 0;
	uint64_t scratch_bytes     = 0;
	uint32_t capture_count     = 0;
	uint32_t descriptor_stride = 0;
	uint32_t active_off        = 0;
	uint32_t output_off        = 0;
};

std::shared_ptr<NativeWalkPlan> BuildNativeWalkPlan(ResourcePlan& plan,
                                                    std::span<const Value> values);

// Test hook for FakeGuestMemory on hosts without fault-based guest memory.
void SetSrtTestMemoryReader(SrtMemoryReader reader, void* userdata = nullptr);

bool RunNativeWalk(const NativeWalkPlan& walk, const ResourcePlan& plan, const SrtRuntime& runtime,
                   std::vector<uint32_t>& flat,
                   std::vector<std::pair<uint64_t, uint64_t>>* reads);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKERNATIVE_H_ */
