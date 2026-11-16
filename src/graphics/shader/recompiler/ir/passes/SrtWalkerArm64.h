#ifndef EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_SRTWALKERARM64_H_
#define EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_SRTWALKERARM64_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <memory>
#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::IR {

struct Arm64WalkCode {
	void*    rx_code          = nullptr;
	void*    rw_code          = nullptr;
	size_t   alloc_size       = 0;
	uint32_t user_data_count  = 0;
	uint32_t capture_count    = 0;
	uint64_t scratch_bytes    = 0;

	Arm64WalkCode() = default;
	~Arm64WalkCode();

	Arm64WalkCode(const Arm64WalkCode&)            = delete;
	Arm64WalkCode& operator=(const Arm64WalkCode&) = delete;

	Arm64WalkCode(Arm64WalkCode&& other) noexcept;
	Arm64WalkCode& operator=(Arm64WalkCode&& other) noexcept;

	[[nodiscard]] bool IsValid() const noexcept { return rx_code != nullptr; }
};

std::shared_ptr<Arm64WalkCode> CompileArm64SrtWalker(ResourcePlan& plan, std::span<const Value> values = {});

bool RunArm64SrtWalk(const Arm64WalkCode& code, const ResourcePlan& plan, const SrtRuntime& runtime,
                     std::vector<uint32_t>& flat, std::vector<std::pair<uint64_t, uint64_t>>* reads);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif // EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_SRTWALKERARM64_H_
