#include "graphics/shader/recompiler/ir/passes/SrtWalkerNative.h"

#include "common/assert.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#include <mach/vm_map.h>
#else
#include <mach/mach_vm.h>
#endif
#elif defined(__linux__) || defined(__ANDROID__)
#include <sys/uio.h>
#include <unistd.h>
#endif
#include <unordered_map>

namespace Libs::Graphics::ShaderRecompiler::IR {

namespace {

thread_local SrtMemoryReader t_test_memory_reader   = nullptr;
thread_local void*           t_test_memory_userdata = nullptr;

// Fault-safe fallback reader when the caller supplies no reader.
bool DefaultReadMemory(void*, uint64_t address, std::span<uint32_t> values) {
	if (values.empty()) return true;
	if (t_test_memory_reader != nullptr &&
	    t_test_memory_reader(t_test_memory_userdata, address, values)) {
		return true;
	}
#if defined(__APPLE__) && TARGET_OS_IPHONE
	vm_size_t copied = 0;
	const kern_return_t result =
	    vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(address),
	                      values.size_bytes(), reinterpret_cast<vm_address_t>(values.data()),
	                      &copied);
	return result == KERN_SUCCESS && copied == values.size_bytes();
#elif defined(__APPLE__)
	mach_vm_size_t copied = 0;
	const kern_return_t result =
	    mach_vm_read_overwrite(mach_task_self(), address, values.size_bytes(),
	                           reinterpret_cast<mach_vm_address_t>(values.data()), &copied);
	return result == KERN_SUCCESS && copied == values.size_bytes();
#elif defined(__linux__) || defined(__ANDROID__)
	struct iovec local {values.data(), values.size_bytes()};
	struct iovec remote {reinterpret_cast<void*>(address), values.size_bytes()};
	return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) ==
	       static_cast<ssize_t>(values.size_bytes());
#else
	return false;
#endif
}

constexpr uint64_t AddressMask = 0x0000ffffffffffffull;

bool AddSignedAddress(uint64_t base, int64_t offset, uint64_t& result) {
	if (base > AddressMask) {
		return false;
	}
	if (offset < 0) {
		const auto magnitude = uint64_t {0} - static_cast<uint64_t>(offset);
		if (magnitude > base) {
			return false;
		}
		result = base - magnitude;
		return true;
	}
	const auto magnitude = static_cast<uint64_t>(offset);
	if (magnitude > AddressMask - base) {
		return false;
	}
	result = base + magnitude;
	return true;
}

bool IsRawRead(const ResourcePlan& values, const Inst& inst) {
	const auto op = inst.GetOpcode();
	if (op != ValueOpcode::LoadAddressU32 && op != ValueOpcode::ReadConstBuffer &&
	    op != ValueOpcode::LoadBufferU32) {
		return false;
	}
	const auto index = inst.Flags<MemoryFlags>().index;
	if (index >= values.memory_info.size()) {
		return false;
	}
	const auto& memory = values.memory_info[index];
	if (op != ValueOpcode::LoadBufferU32) {
		return (op == ValueOpcode::LoadAddressU32 && memory.kind == ResourceKind::ScalarAddress) ||
		       (op == ValueOpcode::ReadConstBuffer && memory.kind == ResourceKind::ScalarBuffer);
	}
	if (inst.NumArgs() != 5u || memory.kind != ResourceKind::Buffer || memory.typed ||
	    memory.formatted || memory.coherent || memory.data_bits != 32u ||
	    memory.data_dwords != 1u || memory.idxen || memory.offen || memory.offset != 0u ||
	    inst.Arg(4).GetType() != Type::U1) {
		return false;
	}
	for (uint32_t arg = 1; arg <= 3; ++arg) {
		if (inst.Arg(arg).Resolve() != Value(0u)) return false;
	}
	return true;
}

bool IsRuntimeSelect(ValueOpcode op) {
	return op == ValueOpcode::SelectU1 || op == ValueOpcode::SelectU32 ||
	       op == ValueOpcode::SelectF32;
}

Value NormalizeMask(Value mask) {
	mask = mask.Resolve();
	return mask.IsImmediate() ? Value {} : mask;
}

struct MemoEntry {
	uint64_t value  = 0;
	uint8_t  state  = 0;
	bool     masked = false;
};

struct FastMemoMap {
	static constexpr size_t Capacity  = 1024;
	static constexpr size_t Mask      = Capacity - 1;
	static constexpr size_t MaxProbes = 32;

	struct Slot {
		const Inst* key   = nullptr;
		uint32_t    epoch = 0;
		MemoEntry   entry{};
	};

	std::array<Slot, Capacity>                 slots{};
	uint32_t                                   current_epoch = 1;
	std::unordered_map<const Inst*, MemoEntry> overflow;

	static inline size_t HashInst(const Inst* inst) {
		const auto ptr = reinterpret_cast<uintptr_t>(inst);
		return ((ptr >> 4) ^ (ptr >> 11)) & Mask;
	}

	void Clear() {
		++current_epoch;
		if (current_epoch == 0) {
			for (auto& s : slots) {
				s.epoch = 0;
				s.key   = nullptr;
			}
			current_epoch = 1;
		}
		if (!overflow.empty()) {
			overflow.clear();
		}
	}

	MemoEntry* Find(const Inst* inst) {
		const size_t hash = HashInst(inst);
		for (size_t probe = 0; probe < MaxProbes; ++probe) {
			auto& s = slots[(hash + probe) & Mask];
			if (s.epoch == current_epoch && s.key == inst) {
				return &s.entry;
			}
			if (s.epoch != current_epoch) {
				break;
			}
		}
		if (!overflow.empty()) {
			auto it = overflow.find(inst);
			if (it != overflow.end()) {
				return &it->second;
			}
		}
		return nullptr;
	}

	const MemoEntry* Find(const Inst* inst) const {
		const size_t hash = HashInst(inst);
		for (size_t probe = 0; probe < MaxProbes; ++probe) {
			const auto& s = slots[(hash + probe) & Mask];
			if (s.epoch == current_epoch && s.key == inst) {
				return &s.entry;
			}
			if (s.epoch != current_epoch) {
				break;
			}
		}
		if (!overflow.empty()) {
			auto it = overflow.find(inst);
			if (it != overflow.end()) {
				return &it->second;
			}
		}
		return nullptr;
	}

	bool Contains(const Inst* inst) const {
		return Find(inst) != nullptr;
	}

	void Put(const Inst* inst, const MemoEntry& entry) {
		if (!overflow.empty()) {
			auto it = overflow.find(inst);
			if (it != overflow.end()) {
				it->second = entry;
				return;
			}
		}
		const size_t hash = HashInst(inst);
		for (size_t probe = 0; probe < MaxProbes; ++probe) {
			auto& s = slots[(hash + probe) & Mask];
			if (s.epoch == current_epoch && s.key == inst) {
				s.entry = entry;
				return;
			}
			if (s.epoch != current_epoch) {
				s.key   = inst;
				s.epoch = current_epoch;
				s.entry = entry;
				return;
			}
		}
		overflow[inst] = entry;
	}
};

struct WalkMemo {
	FastMemoMap                                                 unmasked;
	std::vector<std::pair<Value, std::unique_ptr<FastMemoMap>>> masked;

	FastMemoMap& MapFor(Value mask) {
		if (mask.IsEmpty()) return unmasked;
		for (auto& [key, map] : masked) {
			if (key == mask) return *map;
		}
		auto& entry = masked.emplace_back(mask, std::make_unique<FastMemoMap>());
		return *entry.second;
	}

	bool HasEvaluated(const Inst* inst) const {
		if (unmasked.Contains(inst)) return true;
		for (const auto& [_, map] : masked) {
			if (map && map->Contains(inst)) return true;
		}
		return false;
	}

	void Clear() {
		unmasked.Clear();
		for (auto& [_, map] : masked) {
			if (map) map->Clear();
		}
	}

	void Reset() {
		unmasked.Clear();
		masked.clear();
	}
};

} // namespace

struct WalkContext {
	const NativeWalkPlan*                       walk                = nullptr;
	std::vector<std::pair<uint64_t, uint64_t>>* capture_ranges      = nullptr;
	WalkMemo*                                   memo                = nullptr;
	bool                                        have_specialization = false;
};

class NativeSrtWalker {
public:
	NativeSrtWalker(const ResourcePlan& program, const SrtRuntime& runtime,
	                WalkContext context = {}, Value active_mask = {});
	~NativeSrtWalker();
	NativeSrtWalker(const NativeSrtWalker&)            = delete;
	NativeSrtWalker& operator=(const NativeSrtWalker&) = delete;

	bool Evaluate(Value value, uint32_t& result);
	bool Evaluate64(Value value, uint64_t& result) {
		bool masked = false;
		return EvaluateWide(value, result, masked);
	}
	bool EvaluateWide(Value value, uint64_t& result, bool& masked);
	bool RefreshFlatBuffer(std::vector<uint32_t>& flat);

private:
	static float Float32(uint64_t bits);
	bool         Arg(const Inst& inst, size_t index, uint64_t& result, bool& masked);
	bool         EvaluatePhi(const Inst& inst, uint64_t& result, bool& masked);
	bool         EvaluateExtract(const Inst& inst, uint64_t& result, bool& masked);
	bool         EvaluateRawRead(const Inst& inst, uint64_t& result, bool& masked);
	bool         EvaluateInst(const Inst& inst, uint64_t& result, bool& masked);

	const ResourcePlan& m_program;
	SrtRuntime          m_runtime;
	Value               m_active_mask;
	WalkContext         m_context;
};

NativeSrtWalker::NativeSrtWalker(const ResourcePlan& program, const SrtRuntime& runtime,
                                 WalkContext context, Value active_mask)
    : m_program(program), m_runtime(runtime), m_active_mask(NormalizeMask(active_mask)),
      m_context(context) {}

NativeSrtWalker::~NativeSrtWalker() = default;

bool NativeSrtWalker::Evaluate(Value value, uint32_t& result) {
	uint64_t wide   = 0;
	bool     masked = false;
	if (!EvaluateWide(value, wide, masked)) {
		return false;
	}
	result = static_cast<uint32_t>(wide);
	return true;
}

float NativeSrtWalker::Float32(uint64_t bits) {
	return std::bit_cast<float>(static_cast<uint32_t>(bits));
}

bool NativeSrtWalker::EvaluateWide(Value value, uint64_t& result, bool& masked) {
	value = value.Resolve();
	if (value.IsImmediate()) {
		switch (value.GetType()) {
			case Type::U1: result = value.U1(); return true;
			case Type::U8: result = value.U8(); return true;
			case Type::U16: result = value.U16(); return true;
			case Type::U32: result = value.U32(); return true;
			case Type::U64: result = value.U64(); return true;
			case Type::F32: result = std::bit_cast<uint32_t>(value.F32Value()); return true;
			default: return false;
		}
	}
	if (!m_active_mask.IsEmpty() && value == m_active_mask) {
		masked = true;
		result = 1u;
		return true;
	}
	auto* inst = value.TryInstruction();
	if (inst == nullptr) {
		return false;
	}
	if (!m_active_mask.IsEmpty() && IsRuntimeSelect(inst->GetOpcode()) &&
	    inst->NumArgs() == 3 && inst->Arg(0).Resolve() == m_active_mask) {
		masked = true;
		return EvaluateWide(inst->Arg(1), result, masked);
	}
	if (m_active_mask.IsEmpty() && m_context.walk != nullptr &&
	    !m_context.walk->unknown_nodes.empty() &&
	    m_context.walk->unknown_nodes.contains(inst)) {
		return false;
	}
	auto& memo = m_context.memo->MapFor(m_active_mask);
	if (const auto* found = memo.Find(inst); found != nullptr) {
		masked |= found->masked;
		if (found->state == 2) {
			result = found->value;
			return true;
		}
		return false;
	}
	if (!m_active_mask.IsEmpty()) {
		const auto& unmasked = m_context.memo->unmasked;
		if (const auto* found = unmasked.Find(inst);
		    found != nullptr && found->state == 2) {
			result = found->value;
			return true;
		}
	}
	memo.Put(inst, {.value = 0, .state = 1, .masked = !m_active_mask.IsEmpty()});
	uint64_t out         = 0;
	bool     inst_masked = false;
	bool     evaluated   = EvaluateInst(*inst, out, inst_masked);
	if (evaluated && !m_active_mask.IsEmpty() && !inst_masked && m_context.walk != nullptr &&
	    !m_context.walk->unknown_nodes.empty() &&
	    m_context.walk->unknown_nodes.contains(inst)) {
		evaluated = false;
	}
	auto& active_memo = m_context.memo->MapFor(m_active_mask);
	active_memo.Put(inst, {
	    .value  = out,
	    .state  = static_cast<uint8_t>(evaluated ? 2 : 1),
	    .masked = inst_masked,
	});
	if (evaluated && !m_active_mask.IsEmpty() && !inst_masked) {
		m_context.memo->unmasked.Put(inst, {.value = out, .state = 2, .masked = false});
	}
	masked |= inst_masked;
	if (!evaluated) {
		return false;
	}
	result = out;
	return true;
}

bool NativeSrtWalker::Arg(const Inst& inst, size_t index, uint64_t& result, bool& masked) {
	return EvaluateWide(inst.Arg(index), result, masked);
}

bool NativeSrtWalker::EvaluatePhi(const Inst& inst, uint64_t& result, bool& masked) {
	// A control Phi is known only when every reached predecessor agrees. This mirrors the
	// x86 walker's state handling and keeps loop-carried values live across restarts.
	const auto control = m_program.control_phi_predecessors.find(&inst);
	if (control != m_program.control_phi_predecessors.end()) {
		bool     have  = false;
		uint64_t value = 0;
		for (size_t index = 0; index < control->second.size() && index < inst.NumArgs(); ++index) {
			const auto block = control->second[index];
			if (block >= m_program.visited_blocks.size() || m_program.visited_blocks[block] == 0u) {
				continue;
			}
			uint64_t argument = 0;
			if (!EvaluateWide(inst.Arg(index), argument, masked)) return false;
			if (have && argument != value) return false;
			value = argument;
			have  = true;
		}
		if (have) {
			result = value;
			return true;
		}
		return false;
	}
	const auto value = ResolveInvariantPhi(m_program, Value(const_cast<Inst*>(&inst)));
	return !value.IsEmpty() && EvaluateWide(value, result, masked);
}

bool NativeSrtWalker::EvaluateExtract(const Inst& inst, uint64_t& result, bool& masked) {
	const auto index = inst.Arg(1).Resolve();
	if (!index.IsImmediate() || index.GetType() != Type::U32) {
		return false;
	}
	const auto component = index.U32();
	if (component >= 2u) {
		return false;
	}
	if (inst.GetOpcode() == ValueOpcode::CompositeExtractU64) {
		uint64_t packed = 0;
		if (!Arg(inst, 0, packed, masked)) {
			return false;
		}
		result = static_cast<uint32_t>(packed >> (component * 32u));
		return true;
	}
	const auto* source = inst.Arg(0).ResolveInstruction();
	if (source == nullptr) {
		return false;
	}
	if (source->GetOpcode() == ValueOpcode::CompositeConstructU32x2) {
		return EvaluateWide(source->Arg(component), result, masked);
	}
	return false;
}

bool NativeSrtWalker::EvaluateRawRead(const Inst& inst, uint64_t& result, bool& masked) {
	const auto flags = inst.Flags<MemoryFlags>();
	if (flags.index >= m_program.memory_info.size()) {
		return false;
	}
	const auto& mem    = m_program.memory_info[flags.index];
	const bool  vector = inst.GetOpcode() == ValueOpcode::LoadBufferU32;
	if (vector) {
		uint64_t enabled = 0;
		if (!Arg(inst, 4, enabled, masked)) return false;
		if (enabled == 0u) {
			result = 0u;
			return true;
		}
		if (!m_context.have_specialization) {
			// Without specialization data even an out-of-bounds/zero-sized read is unknown:
			// the caller must not let live memory decide reachability.
			return false;
		}
	}
	const auto* handle = inst.Arg(0).ResolveInstruction();
	if (handle == nullptr) {
		return false;
	}
	uint64_t low    = 0;
	uint64_t high   = 0;
	uint64_t offset = 0;
	if (!Arg(*handle, 0, low, masked) || !Arg(*handle, 1, high, masked) ||
	    !Arg(inst, 1, offset, masked)) {
		return false;
	}
	const auto base      = ((high << 32u) | static_cast<uint32_t>(low)) & AddressMask;
	const auto immediate = static_cast<int64_t>(static_cast<int32_t>(mem.offset));
	uint64_t   address   = 0;
	if (inst.GetOpcode() == ValueOpcode::ReadConstBuffer || vector) {
		uint64_t records = 0;
		uint64_t word3   = 0;
		if (handle->NumArgs() != 4u || !Arg(*handle, 2, records, masked) ||
		    !Arg(*handle, 3, word3, masked)) {
			return false;
		}
		if (immediate < 0) {
			return false;
		}
		const auto byte_offset =
		    (static_cast<uint64_t>(immediate) & ~uint64_t {3}) + (static_cast<uint32_t>(offset) & ~3u);
		const auto stride = (static_cast<uint32_t>(high) >> 16u) & 0x3fffu;
		if (vector) {
			// Only the uniform, unswizzled structured DWORD address is evaluated on the host.
			if ((high & (1u << 31u)) != 0u || (word3 & ((1u << 23u) | 0xf0000000u)) != 0u)
				return false;
			if (stride == 0u || records == 0u || ((word3 >> 12u) & 0x7fu) == 0u) {
				result = 0u;
				return true;
			}
		}
		const auto size = stride == 0u
		                      ? static_cast<uint64_t>(static_cast<uint32_t>(records))
		                      : static_cast<uint64_t>(stride) * static_cast<uint32_t>(records);
		if (byte_offset > size || size - byte_offset < sizeof(uint32_t)) {
			result = 0u;
			return true;
		}
		address = (base & ~uint64_t {3}) + byte_offset;
	} else {
		const auto relative = (immediate & ~int64_t {3}) +
		                      static_cast<int64_t>(static_cast<uint32_t>(offset) & ~3u);
		if (!AddSignedAddress(base & ~uint64_t {3}, relative, address)) {
			return false;
		}
	}
	uint32_t   word   = 0;
	const auto reader = vector ? m_runtime.read_specialization_memory : m_runtime.read_memory;
	if (reader == nullptr) {
		return false;
	}
	if (!reader(m_runtime.userdata, address, {&word, 1})) {
		result = 0u;
		return true;
	}
	result = word;
	return true;
}

bool NativeSrtWalker::EvaluateInst(const Inst& inst, uint64_t& result, bool& masked) {
	uint64_t   a       = 0;
	uint64_t   b       = 0;
	uint64_t   c       = 0;
	const auto binary  = [&]() { return Arg(inst, 0, a, masked) && Arg(inst, 1, b, masked); };
	const auto ternary = [&]() {
		return Arg(inst, 0, a, masked) && Arg(inst, 1, b, masked) && Arg(inst, 2, c, masked);
	};
	switch (inst.GetOpcode()) {
		case ValueOpcode::GetUserData: {
			const auto reg = RegIndex(inst.Arg(0).ScalarRegister());
			if (reg < m_program.user_data_base ||
			    reg - m_program.user_data_base >= m_runtime.user_data.size()) {
				return false;
			}
			result = m_runtime.user_data[reg - m_program.user_data_base];
			return true;
		}
		case ValueOpcode::GetShaderBase: result = m_runtime.shader_base; return true;
		case ValueOpcode::Phi: return EvaluatePhi(inst, result, masked);
		case ValueOpcode::ReadFirstLane: {
			// The lane read does not depend on the EXEC mask it is read under.
			NativeSrtWalker active(m_program, m_runtime, m_context, inst.Arg(1));
			bool            lane_masked = false;
			return active.EvaluateWide(inst.Arg(0), result, lane_masked);
		}
		case ValueOpcode::BitCastU32F32:
		case ValueOpcode::BitCastF32U32: return Arg(inst, 0, result, masked);
		case ValueOpcode::CompositeExtractU64:
		case ValueOpcode::CompositeExtractU32x2: return EvaluateExtract(inst, result, masked);
		case ValueOpcode::CompositeConstructU64:
			if (!binary()) {
				return false;
			}
			result = static_cast<uint32_t>(a) |
			         (static_cast<uint64_t>(static_cast<uint32_t>(b)) << 32u);
			return true;
		case ValueOpcode::LoadAddressU32:
		case ValueOpcode::ReadConstBuffer:
		case ValueOpcode::LoadBufferU32:
			if (IsRawRead(m_program, inst)) {
				return EvaluateRawRead(inst, result, masked);
			}
			break;
		case ValueOpcode::IAdd32:
			if (binary()) {
				result = static_cast<uint32_t>(a + b);
				return true;
			}
			return false;
		case ValueOpcode::IAdd64:
			if (binary()) {
				result = a + b;
				return true;
			}
			return false;
		case ValueOpcode::ISub32:
			if (binary()) {
				result = static_cast<uint32_t>(a - b);
				return true;
			}
			return false;
		case ValueOpcode::ISub64:
			if (binary()) {
				result = a - b;
				return true;
			}
			return false;
		case ValueOpcode::IMul32:
			if (binary()) {
				result = static_cast<uint32_t>(a * b);
				return true;
			}
			return false;
		case ValueOpcode::UMulHi:
			if (binary()) {
				result = (static_cast<uint64_t>(static_cast<uint32_t>(a)) * static_cast<uint32_t>(b)) >> 32u;
				return true;
			}
			return false;
		case ValueOpcode::UMin32:
			if (binary()) {
				result = std::min(static_cast<uint32_t>(a), static_cast<uint32_t>(b));
				return true;
			}
			return false;
		case ValueOpcode::UMax32:
			if (binary()) {
				result = std::max(static_cast<uint32_t>(a), static_cast<uint32_t>(b));
				return true;
			}
			return false;
		case ValueOpcode::SMin32:
			if (binary()) {
				result = static_cast<uint32_t>(
				    std::min(static_cast<int32_t>(a), static_cast<int32_t>(b)));
				return true;
			}
			return false;
		case ValueOpcode::SMax32:
			if (binary()) {
				result = static_cast<uint32_t>(
				    std::max(static_cast<int32_t>(a), static_cast<int32_t>(b)));
				return true;
			}
			return false;
		case ValueOpcode::SMinTri32:
			if (ternary()) {
				result = static_cast<uint32_t>(std::min(
				    {static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c)}));
				return true;
			}
			return false;
		case ValueOpcode::SMaxTri32:
			if (ternary()) {
				result = static_cast<uint32_t>(std::max(
				    {static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c)}));
				return true;
			}
			return false;
		case ValueOpcode::UMaxTri32:
			if (ternary()) {
				result = std::max(
				    {static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c)});
				return true;
			}
			return false;
		case ValueOpcode::ConvertF32U32: {
			if (!Arg(inst, 0, a, masked)) return false;
			result = std::bit_cast<uint32_t>(static_cast<float>(static_cast<uint32_t>(a)));
			return true;
		}
		case ValueOpcode::ConvertS32F32: {
			if (!Arg(inst, 0, a, masked)) return false;
			const auto value = Float32(a);
			if (std::isnan(value)) {
				result = 0u;
				return true;
			}
			if (value >= 2147483648.0f) {
				result = 0x7fffffffu;
				return true;
			}
			if (value <= -2147483649.0f) {
				result = 0x80000000u;
				return true;
			}
			result = static_cast<uint32_t>(static_cast<int32_t>(value));
			return true;
		}
		case ValueOpcode::ConvertU32F32: {
			if (!Arg(inst, 0, a, masked)) return false;
			const auto value = Float32(a);
			if (std::isnan(value) || value <= 0.0f) {
				result = 0u;
				return true;
			}
			if (!std::isfinite(value) || value >= 4294967296.0f) {
				result = UINT32_MAX;
				return true;
			}
			result = static_cast<uint32_t>(value);
			return true;
		}
		case ValueOpcode::FPMul32:
			if (binary()) {
				result = std::bit_cast<uint32_t>(Float32(a) * Float32(b));
				return true;
			}
			return false;
		case ValueOpcode::FPTrunc32:
			if (Arg(inst, 0, a, masked)) {
				result = std::bit_cast<uint32_t>(std::trunc(Float32(a)));
				return true;
			}
			return false;
		case ValueOpcode::FPRecipIFlag32:
			if (Arg(inst, 0, a, masked)) {
				const auto exponent = (a >> 23u) & 0xffu;
				// Normal positive powers of two have exact normal reciprocals in every FP mode.
				if ((a & 0x807fffffu) != 0u || exponent == 0u || exponent >= 254u) return false;
				result = (254u - exponent) << 23u;
				return true;
			}
			return false;
		case ValueOpcode::FPIsNan32:
			if (Arg(inst, 0, a, masked)) {
				result = std::isnan(Float32(a));
				return true;
			}
			return false;
		case ValueOpcode::FPOrdLessThanEqual32:
		case ValueOpcode::FPOrdGreaterThanEqual32:
			if (binary()) {
				const auto operand = [&](uint64_t bits) {
					if (inst.Flags<FPCompareFlags>().flush_input_denorms &&
					    (bits & 0x7fffffffu) < 0x00800000u) {
						bits &= 0x80000000u;
					}
					return Float32(bits);
				};
				result = inst.GetOpcode() == ValueOpcode::FPOrdLessThanEqual32
				             ? operand(a) <= operand(b)
				             : operand(a) >= operand(b);
				return true;
			}
			return false;
		case ValueOpcode::BitwiseAnd32:
			if (binary()) {
				result = static_cast<uint32_t>(a & b);
				return true;
			}
			return false;
		case ValueOpcode::BitwiseAnd64:
			if (binary()) {
				result = a & b;
				return true;
			}
			return false;
		case ValueOpcode::BitwiseOr32:
			if (binary()) {
				result = static_cast<uint32_t>(a | b);
				return true;
			}
			return false;
		case ValueOpcode::BitwiseXor32:
			if (binary()) {
				result = static_cast<uint32_t>(a ^ b);
				return true;
			}
			return false;
		case ValueOpcode::BitwiseNot32:
			if (Arg(inst, 0, a, masked)) {
				result = ~static_cast<uint32_t>(a);
				return true;
			}
			return false;
		case ValueOpcode::ShiftLeftLogical32:
			if (binary()) {
				result = static_cast<uint32_t>(a) << (b & 31u);
				return true;
			}
			return false;
		case ValueOpcode::ShiftLeftLogical64:
			if (binary()) {
				result = a << (b & 63u);
				return true;
			}
			return false;
		case ValueOpcode::ShiftRightLogical32:
			if (binary()) {
				result = static_cast<uint32_t>(a) >> (b & 31u);
				return true;
			}
			return false;
		case ValueOpcode::ShiftRightLogical64:
			if (binary()) {
				result = a >> (b & 63u);
				return true;
			}
			return false;
		case ValueOpcode::ShiftRightArithmetic32:
			if (binary()) {
				result = static_cast<uint32_t>(
				    std::bit_cast<int32_t>(static_cast<uint32_t>(a)) >> (b & 31u));
				return true;
			}
			return false;
		case ValueOpcode::ShiftRightArithmetic64:
			if (binary()) {
				result = static_cast<uint64_t>(std::bit_cast<int64_t>(a) >> (b & 63u));
				return true;
			}
			return false;
		case ValueOpcode::BitFieldUExtract:
			if (ternary()) {
				const auto offset = static_cast<uint32_t>(b);
				const auto width  = static_cast<uint32_t>(c);
				if (offset > 32u || width > 32u - offset) {
					return false;
				}
				const auto mask = width == 32u  ? UINT32_MAX
				                  : width == 0u ? 0u
				                                : (uint32_t {1} << width) - 1u;
				result = width == 0u ? 0u : (static_cast<uint32_t>(a) >> offset) & mask;
				return true;
			}
			return false;
		case ValueOpcode::BitFieldSExtract:
			if (ternary()) {
				const auto offset = static_cast<uint32_t>(b);
				const auto width  = static_cast<uint32_t>(c);
				if (offset > 32u || width > 32u - offset) {
					return false;
				}
				if (width == 0u) {
					result = 0;
					return true;
				}
				const auto mask = width == 32u ? UINT32_MAX : (uint32_t {1} << width) - 1u;
				auto       bits = (static_cast<uint32_t>(a) >> offset) & mask;
				if (width < 32u && (bits & (uint32_t {1} << (width - 1u))) != 0u) {
					bits |= ~mask;
				}
				result = bits;
				return true;
			}
			return false;
		case ValueOpcode::BitFieldInsert: {
			uint64_t d = 0;
			if (!ternary() || !Arg(inst, 3, d, masked)) {
				return false;
			}
			const auto offset = static_cast<uint32_t>(c);
			const auto width  = static_cast<uint32_t>(d);
			if (offset > 32u || width > 32u - offset) {
				return false;
			}
			if (width == 0u) {
				result = static_cast<uint32_t>(a);
				return true;
			}
			const auto mask =
			    width == 32u ? UINT32_MAX : ((uint32_t {1} << width) - 1u) << offset;
			result = (static_cast<uint32_t>(a) & ~mask) |
			         ((static_cast<uint32_t>(b) << offset) & mask);
			return true;
		}
		case ValueOpcode::SelectU32:
		case ValueOpcode::SelectU1:
		case ValueOpcode::SelectF32: {
			if (Arg(inst, 0, a, masked)) {
				return Arg(inst, a != 0u ? 1u : 2u, result, masked);
			}
			return false;
		}
		case ValueOpcode::IEqual32:
			if (binary()) {
				result = static_cast<uint32_t>(a) == static_cast<uint32_t>(b);
				return true;
			}
			return false;
		case ValueOpcode::IEqual64:
			if (binary()) {
				result = a == b;
				return true;
			}
			return false;
		case ValueOpcode::INotEqual64:
			if (binary()) {
				result = a != b;
				return true;
			}
			return false;
		case ValueOpcode::SLessThan64:
			if (binary()) {
				result = static_cast<int64_t>(a) < static_cast<int64_t>(b);
				return true;
			}
			return false;
		case ValueOpcode::SLessThanEqual64:
			if (binary()) {
				result = static_cast<int64_t>(a) <= static_cast<int64_t>(b);
				return true;
			}
			return false;
		case ValueOpcode::ULessThan64:
			if (binary()) {
				result = a < b;
				return true;
			}
			return false;
		case ValueOpcode::ULessThanEqual64:
			if (binary()) {
				result = a <= b;
				return true;
			}
			return false;
		case ValueOpcode::UGreaterThan64:
			if (binary()) {
				result = a > b;
				return true;
			}
			return false;
		case ValueOpcode::UGreaterThanEqual64:
			if (binary()) {
				result = a >= b;
				return true;
			}
			return false;
		case ValueOpcode::INotEqual32:
			if (binary()) {
				result = static_cast<uint32_t>(a) != static_cast<uint32_t>(b);
				return true;
			}
			return false;
		case ValueOpcode::ULessThan32:
			if (!Arg(inst, 1, b, masked)) return false;
			if (static_cast<uint32_t>(b) == 0u) {
				result = 0u;
				return true;
			}
			if (!Arg(inst, 0, a, masked)) return false;
			result = static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
			return true;
		case ValueOpcode::ULessThanEqual32:
			if (!Arg(inst, 0, a, masked)) return false;
			if (static_cast<uint32_t>(a) == 0u) {
				result = 1u;
				return true;
			}
			if (!Arg(inst, 1, b, masked)) return false;
			result = static_cast<uint32_t>(a) <= static_cast<uint32_t>(b);
			return true;
		case ValueOpcode::UGreaterThan32:
			if (binary()) {
				result = static_cast<uint32_t>(a) > static_cast<uint32_t>(b);
				return true;
			}
			return false;
		case ValueOpcode::SGreaterThanEqual32:
			if (binary()) {
				result = std::bit_cast<int32_t>(static_cast<uint32_t>(a)) >=
				         std::bit_cast<int32_t>(static_cast<uint32_t>(b));
				return true;
			}
			return false;
		case ValueOpcode::LogicalAnd: {
			const bool left = Arg(inst, 0, a, masked);
			if (left && a == 0u) {
				result = 0u;
				return true;
			}
			const bool right = Arg(inst, 1, b, masked);
			if (right && b == 0u) {
				result = 0u;
				return true;
			}
			if (left && right) {
				result = 1u;
				return true;
			}
			if (!left) {
				// Only lanes satisfying the left EXEC factor can make a conjunction true, so an
				// unknown left side retries the right one under that factor as its mask.
				NativeSrtWalker refined(m_program, m_runtime, m_context, inst.Arg(0));
				uint64_t        refined_b      = 0;
				bool            refined_masked = false;
				if (refined.EvaluateWide(inst.Arg(1), refined_b, refined_masked) &&
				    refined_b == 0u) {
					result = 0u;
					return true;
				}
			}
			return false;
		}
		case ValueOpcode::LogicalOr: {
			const bool left = Arg(inst, 0, a, masked);
			if (left && a != 0u) {
				result = 1u;
				return true;
			}
			if (!Arg(inst, 1, b, masked) || (b == 0u && !left)) return false;
			result = b != 0u;
			return true;
		}
		case ValueOpcode::LogicalXor:
			if (binary()) {
				result = (a != 0u) != (b != 0u);
				return true;
			}
			return false;
		case ValueOpcode::ConditionRef: return Arg(inst, 0, result, masked);
		case ValueOpcode::LogicalNot:
			if (Arg(inst, 0, a, masked)) {
				result = a == 0u;
				return true;
			}
			return false;
		case ValueOpcode::UndefU1:
		case ValueOpcode::UndefU32: return false;
		default: break;
	}
	return false;
}

bool NativeSrtWalker::RefreshFlatBuffer(std::vector<uint32_t>& flat) {
	if (!m_program.srt_plan_complete) return false;
	const auto refresh = [&](uint32_t slot) {
		if (slot >= m_program.srt_reads.size()) return false;
		const auto& read = m_program.srt_reads[slot];
		return read.flat_offset < flat.size() && Evaluate(read.value, flat[read.flat_offset]);
	};
	if (m_program.control_flow.empty()) {
		flat.resize(m_program.srt_reads.size());
		for (uint32_t slot = 0; slot < m_program.srt_reads.size(); ++slot) {
			if (!refresh(slot)) return false;
		}
		return true;
	}
	flat.assign(m_program.srt_reads.size(), 0u);
	auto& visited = m_program.visited_blocks;
	visited.assign(m_program.control_flow.size(), 0u);

	const auto& blocks = m_program.control_flow;
	thread_local std::vector<std::vector<const Inst*>> t_rechecks;
	t_rechecks.resize(blocks.size());
	for (auto& r : t_rechecks) {
		r.clear();
	}
	for (const auto& [phi, preds]: m_program.control_phi_predecessors) {
		for (const auto pred: preds) {
			if (pred < t_rechecks.size()) {
				t_rechecks[pred].push_back(phi);
			}
		}
	}

	// Mirrors WalkerCompiler::Compile: monotonic reachability with a restart only when a
	// newly reached block is a predecessor of an already-evaluated control Phi. Retreating
	// edges resume at their target while keeping memoized values intact.
	const auto capture_mark =
	    m_context.capture_ranges != nullptr ? m_context.capture_ranges->size() : 0u;
	visited[0] = 1u;
	for (bool restart = true; restart;) {
		restart = false;
		m_context.memo->Clear();
		if (m_context.capture_ranges != nullptr) {
			m_context.capture_ranges->resize(capture_mark);
		}
		for (uint32_t index = 0; index < blocks.size();) {
			if (visited[index] == 0u) {
				++index;
				continue;
			}
			uint32_t   jump_target = UINT32_MAX;
			const auto mark        = [&](uint32_t target) {
				if (target >= visited.size() || visited[target] != 0u) return;
				visited[target] = 1u;
				for (const auto* phi: t_rechecks[target]) {
					if (m_context.memo->HasEvaluated(phi)) {
						restart = true;
						return;
					}
				}
				if (target <= index && jump_target == UINT32_MAX) {
					jump_target = target;
				}
			};
			const auto& block     = blocks[index];
			uint32_t    condition = 0;
			if (!block.condition.IsEmpty() && Evaluate(block.condition, condition)) {
				mark(block.successors[condition != 0u ? 0u : 1u]);
			} else {
				for (const auto successor: block.successors) {
					mark(successor);
					if (restart || jump_target != UINT32_MAX) break;
				}
			}
			if (restart) break;
			if (jump_target != UINT32_MAX) {
				index = jump_target;
			} else {
				++index;
			}
		}
	}

	for (uint32_t index = 0; index < m_program.control_flow.size(); ++index) {
		if (visited[index] == 0u) continue;
		for (const auto slot: m_program.control_flow[index].srt_reads) {
			if (!refresh(slot)) return false;
		}
	}
	return true;
}


namespace {

struct CaptureState {
	std::vector<std::pair<uint64_t, uint64_t>>* ranges           = nullptr;
	SrtMemoryReader                             ordinary         = nullptr;
	void*                                       ordinary_userdata = nullptr;
	SrtMemoryReader                             special          = nullptr;
	void*                                       special_userdata  = nullptr;

	static bool ReadOrdinary(void* userdata, uint64_t address, std::span<uint32_t> values) {
		auto& self = *static_cast<CaptureState*>(userdata);
		if (self.ranges != nullptr) self.ranges->emplace_back(address, values.size_bytes());
		if (self.ordinary == nullptr) return false;
		if (!self.ordinary(self.ordinary_userdata, address, values)) {
			std::ranges::fill(values, 0u);
		}
		return true;
	}

	static bool ReadSpecial(void* userdata, uint64_t address, std::span<uint32_t> values) {
		auto& self = *static_cast<CaptureState*>(userdata);
		if (self.ranges != nullptr) self.ranges->emplace_back(address, values.size_bytes());
		if (self.special == nullptr) return false;
		if (!self.special(self.special_userdata, address, values)) {
			std::ranges::fill(values, 0u);
		}
		return true;
	}
};

bool AnyGateActive(const std::vector<uint32_t>& gates, const ResourcePlan& plan,
                   const NativeWalkPlan& walk) {
	const auto* active = reinterpret_cast<const uint8_t*>(plan.srt_scratch.data());
	for (const auto gate: gates) {
		if (gate == UINT32_MAX) return true;
		if (active == nullptr) return true;
		if (active[walk.active_off + gate] != 0u) return true;
	}
	return false;
}

} // namespace

void SetSrtTestMemoryReader(SrtMemoryReader reader, void* userdata) {
	t_test_memory_reader   = reader;
	t_test_memory_userdata = userdata;
}

bool RunNativeWalk(const NativeWalkPlan& walk, const ResourcePlan& plan, const SrtRuntime& runtime,
                   std::vector<uint32_t>& flat,
                   std::vector<std::pair<uint64_t, uint64_t>>* reads) {
	if (runtime.user_data.size() < walk.user_data) {
		return false;
	}
	if (reads != nullptr) {
		reads->reserve(64);
	}
	const bool have_specialization =
	    runtime.read_specialization_memory != nullptr || t_test_memory_reader != nullptr;
	CaptureState capture;
	capture.ranges = reads;
	if (runtime.read_memory != nullptr) {
		capture.ordinary          = runtime.read_memory;
		capture.ordinary_userdata = runtime.userdata;
	} else {
		capture.ordinary          = DefaultReadMemory;
		capture.ordinary_userdata = nullptr;
	}
	if (runtime.read_specialization_memory != nullptr) {
		capture.special          = runtime.read_specialization_memory;
		capture.special_userdata = runtime.userdata;
	} else if (t_test_memory_reader != nullptr) {
		capture.special          = DefaultReadMemory;
		capture.special_userdata = nullptr;
	}

	SrtRuntime runtime_copy                 = runtime;
	runtime_copy.userdata                   = &capture;
	runtime_copy.read_memory                = CaptureState::ReadOrdinary;
	runtime_copy.read_specialization_memory =
	    capture.special != nullptr ? CaptureState::ReadSpecial : nullptr;

	thread_local WalkMemo t_walk_memo;
	t_walk_memo.Reset();

	WalkContext context;
	context.walk                = &walk;
	context.capture_ranges      = reads;
	context.memo                = &t_walk_memo;
	context.have_specialization = have_specialization;
	NativeSrtWalker evaluator(plan, runtime_copy, context);
	if (walk.resources) {
		if (!evaluator.RefreshFlatBuffer(flat)) {
			return false;
		}
	} else {
		// Values-only plans (call targets) have no flat/control-flow refresh, but the x86
		// WalkSrt still sizes flat to the plan's reads and marks all blocks reachable.
		flat.assign(plan.srt_reads.size(), 0u);
		plan.visited_blocks.assign(plan.control_flow.size(), 1u);
	}

	plan.srt_scratch.assign((walk.scratch_bytes + 7u) / 8u + walk.capture_count, 0u);
	auto* scratch = reinterpret_cast<uint8_t*>(plan.srt_scratch.data());

	const auto& sources = plan.descriptor_sources;
	const auto& blocks  = plan.control_flow;
	const auto  images  = plan.info.images.size();
	if (walk.scratch_bytes < walk.active_off + sources.size() + images ||
	    walk.scratch_bytes < walk.output_off + walk.outputs.size() * 8u) {
		std::fprintf(stderr,
		             "shader: native SRT walk scratch layout mismatch "
		             "(scratch=%llu active=%u outputs=%u)\n",
		             static_cast<unsigned long long>(walk.scratch_bytes), walk.active_off,
		             walk.output_off);
		return false;
	}

	for (uint32_t index = 0; index < sources.size() + images; ++index) {
		const bool     image  = index >= sources.size();
		const uint32_t id     = image ? index - static_cast<uint32_t>(sources.size()) : index;
		bool           listed = false;
		bool           active = false;
		for (uint32_t block = 0; block < blocks.size(); ++block) {
			const auto& list = image ? blocks[block].images : blocks[block].sources;
			if (std::ranges::find(list, id) == list.end()) continue;
			listed = true;
			if (plan.visited_blocks.size() > block && plan.visited_blocks[block] != 0u) {
				active = true;
			}
		}
		scratch[walk.active_off + index] = (!listed || active) ? 1u : 0u;
	}


	for (uint32_t source = 0; source < sources.size(); ++source) {
		if (walk.gates[source].empty()) continue;
		auto* dst                  = scratch + source * walk.descriptor_stride;
		dst[8 * sizeof(uint32_t)] = 0u;
		if (!AnyGateActive(walk.gates[source], plan, walk)) continue;
		const auto& words = walk.descriptors[source];
		bool        known = true;
		for (uint32_t word = 0; word < words.size(); ++word) {
			uint32_t value = 0;
			if (!evaluator.Evaluate(words[word], value)) {
				known = false;
				break;
			}
			std::memcpy(dst + word * sizeof(uint32_t), &value, sizeof(value));
		}
		if (known) dst[8 * sizeof(uint32_t)] = 1u;
	}

	for (uint32_t index = 0; index < walk.outputs.size(); ++index) {
		auto* out = reinterpret_cast<uint64_t*>(scratch + walk.output_off + index * 8u);
		*out      = UINT64_MAX;
		if (!AnyGateActive(walk.output_gates[index], plan, walk)) continue;
		uint64_t value = 0;
		if (!evaluator.Evaluate64(walk.outputs[index], value)) continue;
		*out = static_cast<uint32_t>(value);
	}

	return true;
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
