#include "graphics/shader/recompiler/ir/passes/SrtWalkerArm64.h"

#include "common/assert.h"
#include "graphics/shader/recompiler/ir/passes/Arm64Emitter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <span>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#include <mach/vm_map.h>
#else
#include <mach/mach_vm.h>
#endif
#include <pthread.h>
#include <sys/mman.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Libs::Graphics::ShaderRecompiler::IR {

namespace {

constexpr uint32_t DescriptorStride = 9u * sizeof(uint32_t);

uint32_t ActiveOffset(const ResourcePlan& plan) {
	return static_cast<uint32_t>(plan.descriptor_sources.size()) * DescriptorStride;
}

uint32_t OutputOffset(const ResourcePlan& plan) {
	const auto active = plan.descriptor_sources.size() + plan.info.images.size();
	return (ActiveOffset(plan) + static_cast<uint32_t>(active) + 7u) & ~7u;
}

bool ImmediateBits(Value value, uint64_t& bits) {
	switch (value.GetType()) {
		case Type::U1: bits = value.U1(); return true;
		case Type::U8: bits = value.U8(); return true;
		case Type::U16: bits = value.U16(); return true;
		case Type::U32: bits = value.U32(); return true;
		case Type::U64: bits = value.U64(); return true;
		case Type::F32: bits = std::bit_cast<uint32_t>(value.F32Value()); return true;
		default: return false;
	}
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

bool IsRuntimeUniformOp(ValueOpcode op) {
	switch (op) {
		case ValueOpcode::ConditionRef:
		case ValueOpcode::BitCastU32F32:
		case ValueOpcode::BitCastF32U32:
		case ValueOpcode::ConvertU32F32:
		case ValueOpcode::ConvertS32F32:
		case ValueOpcode::ConvertF32U32:
		case ValueOpcode::CompositeConstructU64:
		case ValueOpcode::CompositeExtractU64:
		case ValueOpcode::CompositeConstructU32x2:
		case ValueOpcode::CompositeExtractU32x2:
		case ValueOpcode::BitFieldInsert:
		case ValueOpcode::BitFieldUExtract:
		case ValueOpcode::BitFieldSExtract:
		case ValueOpcode::IAdd32:
		case ValueOpcode::IAdd64:
		case ValueOpcode::ISub32:
		case ValueOpcode::ISub64:
		case ValueOpcode::IMul32:
		case ValueOpcode::UMulHi:
		case ValueOpcode::UMin32:
		case ValueOpcode::UMax32:
		case ValueOpcode::SMax32:
		case ValueOpcode::ShiftLeftLogical32:
		case ValueOpcode::ShiftLeftLogical64:
		case ValueOpcode::ShiftRightLogical32:
		case ValueOpcode::ShiftRightLogical64:
		case ValueOpcode::ShiftRightArithmetic32:
		case ValueOpcode::ShiftRightArithmetic64:
		case ValueOpcode::BitwiseAnd32:
		case ValueOpcode::BitwiseAnd64:
		case ValueOpcode::BitwiseOr32:
		case ValueOpcode::BitwiseXor32:
		case ValueOpcode::BitwiseNot32:
		case ValueOpcode::SelectU1:
		case ValueOpcode::SelectU32:
		case ValueOpcode::SelectF32:
		case ValueOpcode::ULessThan32:
		case ValueOpcode::ULessThanEqual32:
		case ValueOpcode::IEqual32:
		case ValueOpcode::IEqual64:
		case ValueOpcode::UGreaterThan32:
		case ValueOpcode::SGreaterThanEqual32:
		case ValueOpcode::INotEqual32:
		case ValueOpcode::INotEqual64:
		case ValueOpcode::LogicalOr:
		case ValueOpcode::LogicalAnd:
		case ValueOpcode::LogicalXor:
		case ValueOpcode::LogicalNot:
		case ValueOpcode::FPOrdLessThanEqual32:
		case ValueOpcode::FPOrdGreaterThanEqual32:
		case ValueOpcode::FPIsNan32:
		case ValueOpcode::FPMul32:
		case ValueOpcode::FPRecipIFlag32:
		case ValueOpcode::FPTrunc32: return true;
		default: return false;
	}
}

// Fallback reader
bool DefaultReadMemory(void*, uint64_t address, std::span<uint32_t> values) {
	if (values.empty()) return true;
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

struct SrtReadContext {
	SrtMemoryReader read_mem     = nullptr;
	void*           read_mem_ud  = nullptr;
	SrtMemoryReader read_spec    = nullptr;
	void*           read_spec_ud = nullptr;
};

extern "C" bool SrtWalkerReadGuestWord(uint64_t address, uint32_t* out_word, void* ctx_ptr, uint32_t is_vector) {
	auto* ctx = static_cast<const SrtReadContext*>(ctx_ptr);
	auto reader = (is_vector != 0) ? ctx->read_spec : ctx->read_mem;
	void* ud    = (is_vector != 0) ? ctx->read_spec_ud : ctx->read_mem_ud;
	if (reader == nullptr) {
		*out_word = 0;
		return false;
	}
	uint32_t val = 0;
	if (!reader(ud, address, {&val, 1})) {
		*out_word = 0;
		return true; // fault treated as zero read
	}
	*out_word = val;
	return true;
}

class Arm64WalkerCompiler {
public:
	explicit Arm64WalkerCompiler(ResourcePlan& plan, Arm64Emitter& emitter)
	    : m_plan(plan), m_code(emitter) {}

	void Compile(std::span<const Value> values);

	uint32_t UserDataCount() const { return m_user_data; }
	uint32_t CaptureCount() const { return m_captures; }

private:
	enum class Kind : uint8_t { Unknown, Constant, UserData, ShaderBase, Node };
	struct Operand {
		Kind     kind  = Kind::Unknown;
		uint64_t value = 0;

		bool operator==(const Operand& other) const = default;
	};
	struct Ref {
		Operand operand;
		bool    masked = false;
	};
	struct Node {
		const Inst*          inst    = nullptr;
		uint32_t             context = 0;
		Operand              result;
		std::vector<Operand> args;
		uint32_t             slot = 0, state = 0;
		Arm64Label           label;
		bool                 pending = true, referenced = false, masked = false;
	};

	bool Thunk(uint32_t index) const {
		const auto& node = m_nodes[index];
		return node.referenced || node.result == Operand {Kind::Node, index};
	}
	static bool MayBeUnknown(const Operand& operand) {
		return operand.kind == Kind::Unknown || operand.kind == Kind::Node;
	}
	static bool EmitsCapture(const Node& node) {
		const auto op = node.inst->GetOpcode();
		if (op != ValueOpcode::LoadAddressU32 && op != ValueOpcode::ReadConstBuffer &&
		    op != ValueOpcode::LoadBufferU32) {
			return false;
		}
		const auto args =
		    node.args.size() - (op == ValueOpcode::LoadBufferU32 && !node.args.empty() ? 1u : 0u);
		return args != 0u;
	}

	uint32_t Context(Value mask) {
		mask = mask.Resolve();
		const auto found = std::ranges::find(m_masks, mask);
		if (found != m_masks.end()) return static_cast<uint32_t>(found - m_masks.begin());
		m_masks.push_back(mask);
		return static_cast<uint32_t>(m_masks.size() - 1u);
	}

	void AddOutput(Value value, uint32_t gate);
	void Consume(std::vector<std::vector<uint32_t>>& gates);
	Ref Evaluate(Value value, uint32_t context);
	Operand Analyze(uint32_t index);
	Operand AnalyzeRead(Node& node, const auto& arg, Operand self);
	void Layout();
	void Gate(std::vector<uint32_t>& gates, Arm64Label& skip);
	void Zero(uint32_t offset, uint32_t size);
	void Branch(const Operand& operand, Arm64Label& unknown);
	void Load(Arm64Reg reg, const Operand& operand);
	void Fetch(const Operand& operand, Arm64Label& unknown, Arm64Reg reg = Arm64Reg::X0);
	void Store(const Node& node);
	void EmitNode(uint32_t index);
	void EmitRead(const Node& node, Arm64Label& thunk_ret);
	void EmitStrict(const Node& node, Arm64Label& thunk_ret);

	struct EmitPlan {
		bool                               resources = false;
		std::vector<std::vector<uint32_t>> gates;
		std::vector<Operand>               conditions;
		std::vector<Operand>               reads;
		std::vector<std::vector<Operand>>  descriptors;
		std::vector<Operand>               outputs;
	};

	ResourcePlan&                                        m_plan;
	Arm64Emitter&                                        m_code;
	EmitPlan                                             m_emit;
	std::vector<Value>                                   m_masks {Value {}};
	std::deque<Node>                                     m_nodes;
	std::map<std::pair<const Inst*, uint32_t>, uint32_t> m_memo;
	std::vector<Value>                                   m_outputs;
	std::vector<std::vector<uint32_t>>                   m_output_gates;
	Arm64Label                                           m_ret;
	uint32_t m_shader_base = 0, m_reach = 0, m_states = 0, m_states_end = 0;
	uint32_t m_user_data = 0, m_captures = 0;
};

void Arm64WalkerCompiler::AddOutput(Value value, uint32_t gate) {
	const auto* inst = value.Resolve().TryInstruction();
	if (inst == nullptr) return;
	auto index = inst->WalkOutput();
	if (index >= m_outputs.size() || m_outputs[index].TryInstruction() != inst) {
		index = static_cast<uint32_t>(m_outputs.size());
		inst->SetWalkOutput(index);
		m_outputs.push_back(value.Resolve());
		m_output_gates.emplace_back();
	}
	m_output_gates[index].push_back(gate);
}

void Arm64WalkerCompiler::Consume(std::vector<std::vector<uint32_t>>& gates) {
	const auto& sources = m_plan.descriptor_sources;
	const auto  consume = [&](uint32_t root) {
		if (root >= sources.size()) return;
		const auto& indirect = sources[root].indirect_descriptor;
		if (!indirect) {
			gates[root].push_back(root);
			return;
		}
		const auto add = [&](uint32_t source) {
			if (source < sources.size()) gates[source].push_back(root);
		};
		if (indirect->sources.empty()) {
			add(indirect->table_source);
			if (indirect->selector) {
				add(indirect->selector->source);
				if (indirect->selector->rows) add(indirect->selector->rows->source);
			}
		}
		for (const auto source: indirect->sources) add(source);
		AddOutput(indirect->key_count, root);
		AddOutput(indirect->selector_first, root);
		AddOutput(indirect->selector_mask, root);
	};
	for (const auto& buffer: m_plan.info.buffers) consume(buffer.source);
	for (const auto& image: m_plan.info.images) consume(image.source);
	for (const auto& sampler: m_plan.info.samplers) {
		const auto* indirect = sampler.source < sources.size() && sources[sampler.source].indirect_descriptor
		    ? &*sources[sampler.source].indirect_descriptor : nullptr;
		if (indirect == nullptr || indirect->sources.empty()) {
			consume(sampler.source);
		} else if (indirect->sources[0] < sources.size()) {
			gates[indirect->sources[0]].push_back(indirect->sources[0]);
		}
	}
	const auto& fill = m_plan.uniform_fill;
	if (fill.fill.words != 0) {
		AddOutput(fill.pattern_size, UINT32_MAX);
		AddOutput(fill.element_count, UINT32_MAX);
		for (const auto value: fill.values) AddOutput(value, UINT32_MAX);
	}
}

Arm64WalkerCompiler::Ref Arm64WalkerCompiler::Evaluate(Value value, uint32_t context) {
	value = value.Resolve();
	if (value.IsEmpty()) return {};
	if (value.IsImmediate()) {
		uint64_t bits = 0;
		return ImmediateBits(value, bits) ? Ref {{Kind::Constant, bits}} : Ref {};
	}
	const auto mask = m_masks[context];
	if (context != 0 && value == mask) return {{Kind::Constant, 1u}, true};
	const auto* inst = value.TryInstruction();
	if (inst == nullptr) return {};
	const auto op = inst->GetOpcode();
	if (context != 0 && IsRuntimeSelect(op) && inst->NumArgs() == 3 && inst->Arg(0).Resolve() == mask) {
		return {Evaluate(inst->Arg(1), context).operand, true};
	}
	switch (op) {
		case ValueOpcode::BitCastU32F32:
		case ValueOpcode::BitCastF32U32:
		case ValueOpcode::ConditionRef: return Evaluate(inst->Arg(0), context);
		case ValueOpcode::CompositeExtractU32x2: {
			const auto* source = inst->Arg(0).ResolveInstruction();
			const auto  index  = inst->Arg(1).Resolve();
			if (source == nullptr || source->GetOpcode() != ValueOpcode::CompositeConstructU32x2 ||
			    !index.IsImmediate() || index.GetType() != Type::U32 || index.U32() >= 2u) return {};
			return Evaluate(source->Arg(index.U32()), context);
		}
		case ValueOpcode::ReadFirstLane:
			return {Evaluate(inst->Arg(0), Context(inst->Arg(1))).operand};
		case ValueOpcode::GetUserData: {
			const auto reg = RegIndex(inst->Arg(0).ScalarRegister());
			if (reg < m_plan.user_data_base || reg - m_plan.user_data_base >= m_plan.user_data_count) return {};
			return {{Kind::UserData, reg - m_plan.user_data_base}};
		}
		case ValueOpcode::GetShaderBase: return {{Kind::ShaderBase}};
		case ValueOpcode::Phi:
			if (!m_plan.control_phi_predecessors.contains(inst)) {
				const auto invariant = ResolveInvariantPhi(m_plan, value);
				return invariant.IsEmpty() ? Ref {} : Evaluate(invariant, context);
			}
			break;
		default: break;
	}
	const auto [found, inserted] = m_memo.try_emplace({inst, context}, static_cast<uint32_t>(m_nodes.size()));
	const auto index = found->second;
	if (!inserted) {
		auto& node = m_nodes[index];
		if (!node.pending) return {node.result, node.masked};
		node.referenced = true;
		return {{Kind::Node, index}, context != 0};
	}
	auto& node   = m_nodes.emplace_back();
	node.inst    = inst;
	node.context = context;
	node.result  = Analyze(index);
	node.pending = false;
	if (context != 0 && !node.masked && !node.referenced) {
		node.result = Evaluate(value, 0).operand;
		return {node.result};
	}
	return {node.result, node.masked};
}

Arm64WalkerCompiler::Operand Arm64WalkerCompiler::Analyze(uint32_t index) {
	auto&       node = m_nodes[index];
	const auto& inst = *node.inst;
	const auto  self = Operand {Kind::Node, index};
	const auto  arg  = [&](Value value) {
		const auto ref = Evaluate(value, node.context);
		node.masked |= ref.masked;
		return ref.operand;
	};
	const auto constant = [](uint64_t value) { return Operand {Kind::Constant, value}; };
	switch (inst.GetOpcode()) {
		case ValueOpcode::Phi: {
			bool known = false;
			for (size_t i = 0; i < inst.NumArgs(); ++i) {
				node.args.push_back(arg(inst.Arg(i)));
				known |= node.args.back().kind != Kind::Unknown;
			}
			return known ? self : Operand {};
		}
		case ValueOpcode::LoadAddressU32:
		case ValueOpcode::ReadConstBuffer:
		case ValueOpcode::LoadBufferU32: return AnalyzeRead(node, arg, self);
		case ValueOpcode::SelectU1:
		case ValueOpcode::SelectU32:
		case ValueOpcode::SelectF32: {
			const auto predicate = arg(inst.Arg(0));
			if (predicate.kind == Kind::Unknown) return {};
			if (predicate.kind == Kind::Constant) return arg(inst.Arg(predicate.value != 0u ? 1 : 2));
			node.args = {predicate, arg(inst.Arg(1)), arg(inst.Arg(2))};
			return node.args[1].kind == Kind::Unknown && node.args[2].kind == Kind::Unknown ? Operand {} : self;
		}
		case ValueOpcode::ULessThan32:
		case ValueOpcode::ULessThanEqual32: {
			const bool less  = inst.GetOpcode() == ValueOpcode::ULessThan32;
			const auto bound = arg(inst.Arg(less ? 1 : 0));
			if (bound.kind == Kind::Unknown) return {};
			if (bound.kind == Kind::Constant && static_cast<uint32_t>(bound.value) == 0u) return constant(!less);
			node.args = {bound, arg(inst.Arg(less ? 0 : 1))};
			return node.args[1].kind == Kind::Unknown && bound.kind == Kind::Constant ? Operand {} : self;
		}
		case ValueOpcode::LogicalOr:
		case ValueOpcode::LogicalAnd: {
			const bool conjunction = inst.GetOpcode() == ValueOpcode::LogicalAnd;
			const auto decides     = [&](const Operand& x) {
				return x.kind == Kind::Constant && (x.value == 0u) == conjunction;
			};
			const auto a = arg(inst.Arg(0));
			if (decides(a)) return constant(!conjunction);
			const auto b = arg(inst.Arg(1));
			if (decides(b)) return constant(!conjunction);
			if (a.kind == Kind::Constant && b.kind == Kind::Constant) return constant(conjunction);
			if (a.kind == Kind::Constant && b.kind == Kind::Unknown) return {};
			Operand refined;
			if (conjunction && MayBeUnknown(a)) {
				refined = Evaluate(inst.Arg(1), Context(inst.Arg(0))).operand;
				if (refined == b || (refined.kind == Kind::Constant && refined.value != 0u)) refined = {};
				if (a.kind == Kind::Unknown && refined.kind == Kind::Constant) return constant(0u);
			}
			if (a.kind == Kind::Unknown && refined.kind == Kind::Unknown &&
			    (b.kind == Kind::Unknown || b.kind == Kind::Constant)) return {};
			node.args = {a, b, refined};
			return self;
		}
		case ValueOpcode::CompositeExtractU64: {
			const auto component = inst.Arg(1).Resolve();
			if (!component.IsImmediate() || component.GetType() != Type::U32 || component.U32() >= 2u) return {};
			[[fallthrough]];
		}
		default:
			if (!IsRuntimeUniformOp(inst.GetOpcode()) || inst.GetOpcode() == ValueOpcode::CompositeConstructU32x2)
				return {};
			for (size_t i = 0; i < inst.NumArgs(); ++i) {
				node.args.push_back(arg(inst.Arg(i)));
				if (node.args.back().kind == Kind::Unknown) return {};
			}
			return self;
	}
}

Arm64WalkerCompiler::Operand Arm64WalkerCompiler::AnalyzeRead(Node& node, const auto& arg, Operand self) {
	const auto& inst = *node.inst;
	const auto* handle = inst.Arg(0).ResolveInstruction();
	if (!IsRawRead(m_plan, inst) || handle == nullptr) return {};
	const bool vector = inst.GetOpcode() == ValueOpcode::LoadBufferU32;
	if (vector) {
		const auto guard = arg(inst.Arg(4));
		if (guard.kind == Kind::Unknown) return {};
		if (guard.kind == Kind::Constant && guard.value == 0u) return {Kind::Constant, 0u};
		node.args.push_back(guard);
	}
	const bool buffer = inst.GetOpcode() != ValueOpcode::LoadAddressU32;
	const auto offset = static_cast<int32_t>(m_plan.memory_info[inst.Flags<MemoryFlags>().index].offset);
	bool failed = buffer && (handle->NumArgs() != 4u || offset < 0);
	for (const auto& word: {handle->Arg(0), handle->Arg(1), inst.Arg(1)}) {
		node.args.push_back(arg(word));
		failed |= node.args.back().kind == Kind::Unknown;
	}
	for (uint32_t word = 2; buffer && !failed && word < 4u; ++word) {
		node.args.push_back(arg(handle->Arg(word)));
		failed |= node.args.back().kind == Kind::Unknown;
	}
	if (!failed) return self;
	if (!vector || node.args[0].kind == Kind::Constant) return {};
	node.args.resize(1);
	return self;
}

void Arm64WalkerCompiler::Layout() {
	uint32_t offset = OutputOffset(m_plan) + static_cast<uint32_t>(m_outputs.size()) * 8u;
	m_shader_base = offset;
	offset += 8u;
	for (uint32_t index = 0; index < m_nodes.size(); ++index) {
		if (!Thunk(index)) continue;
		m_nodes[index].slot = offset;
		offset += 8u;
	}
	m_reach = offset;
	offset += static_cast<uint32_t>(m_plan.control_flow.size());
	m_states = offset;
	for (uint32_t index = 0; index < m_nodes.size(); ++index) {
		if (Thunk(index)) m_nodes[index].state = offset++;
	}
	m_states_end = offset;
	m_plan.srt_captures = (offset + 7u) & ~7u;
}

void Arm64WalkerCompiler::Gate(std::vector<uint32_t>& gates, Arm64Label& skip) {
	if (gates.empty()) {
		m_code.B(skip);
		return;
	}
	std::ranges::sort(gates);
	if (gates.back() == UINT32_MAX) return;
	gates.erase(std::unique(gates.begin(), gates.end()), gates.end());
	Arm64Label active;
	for (size_t i = 0; i < gates.size(); ++i) {
		m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, ActiveOffset(m_plan) + gates[i], Arm64Reg::X10);
		if (i + 1u < gates.size()) {
			m_code.CBNZ(Arm64Reg::X9, active, false);
		} else {
			m_code.CBZ(Arm64Reg::X9, skip, false);
		}
	}
	m_code.Bind(active);
}

void Arm64WalkerCompiler::Zero(uint32_t offset, uint32_t size) {
	if (size == 0) return;
	Arm64Label loop, done;
	m_code.MOV_imm32(Arm64Reg::X9, offset);
	m_code.ADD(Arm64Reg::X9, Arm64Reg::X19, Arm64Reg::X9);
	m_code.MOV_imm32(Arm64Reg::X10, size);
	m_code.Bind(loop);
	m_code.CBZ(Arm64Reg::X10, done, false);
	m_code.STRB(Arm64Reg::XZR, Arm64Reg::X9, 0);
	m_code.ADD_imm(Arm64Reg::X9, Arm64Reg::X9, 1);
	m_code.SUB_imm(Arm64Reg::X10, Arm64Reg::X10, 1, false);
	m_code.B(loop);
	m_code.Bind(done);
}

void Arm64WalkerCompiler::Branch(const Operand& operand, Arm64Label& unknown) {
	if (operand.kind == Kind::Unknown) {
		m_code.B(unknown);
	} else if (operand.kind == Kind::Node) {
		auto& node = m_nodes[operand.value];
		m_code.BL(node.label);
		m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X10);
		m_code.CMP_imm(Arm64Reg::X9, 2, false);
		m_code.B(Arm64Cond::NE, unknown);
	}
}

void Arm64WalkerCompiler::Load(Arm64Reg reg, const Operand& operand) {
	switch (operand.kind) {
		case Kind::Constant:
			m_code.MOV_imm64(reg, operand.value);
			break;
		case Kind::UserData:
			m_user_data = std::max(m_user_data, static_cast<uint32_t>(operand.value) + 1u);
			m_code.LDR_safe(reg, Arm64Reg::X20, static_cast<uint32_t>(operand.value) * sizeof(uint32_t), Arm64Reg::X10, false);
			break;
		case Kind::ShaderBase:
			m_code.LDR_safe(reg, Arm64Reg::X19, m_shader_base, Arm64Reg::X10, true);
			break;
		case Kind::Node:
			m_code.LDR_safe(reg, Arm64Reg::X19, m_nodes[operand.value].slot, Arm64Reg::X10, true);
			break;
		case Kind::Unknown:
			break;
	}
}

void Arm64WalkerCompiler::Fetch(const Operand& operand, Arm64Label& unknown, Arm64Reg reg) {
	Branch(operand, unknown);
	Load(reg, operand);
}

void Arm64WalkerCompiler::Store(const Node& node) {
	m_code.STR_safe(Arm64Reg::X0, Arm64Reg::X19, node.slot, Arm64Reg::X9, true);
	m_code.MOV_imm32(Arm64Reg::X10, 2);
	m_code.STRB_safe(Arm64Reg::X10, Arm64Reg::X19, node.state, Arm64Reg::X9);
	m_code.LDP_post(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, 16);
	m_code.RET();
}

void Arm64WalkerCompiler::EmitNode(uint32_t index) {
	auto&      node = m_nodes[index];
	Arm64Label thunk_ret;
	m_code.Bind(node.label);

	m_code.STP_pre(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, -16);
	m_code.MOV(Arm64Reg::X29, Arm64Reg::SP);

	m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X10);
	m_code.CBNZ(Arm64Reg::X9, thunk_ret, false);
	m_code.MOV_imm32(Arm64Reg::X9, 1);
	m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X10);

	if (node.result != Operand {Kind::Node, index}) {
		Fetch(node.result, thunk_ret, Arm64Reg::X0);
		Store(node);
		m_code.Bind(thunk_ret);
		m_code.LDP_post(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, 16);
		m_code.RET();
		return;
	}

	switch (node.inst->GetOpcode()) {
		case ValueOpcode::Phi: {
			const auto& blocks = m_plan.control_phi_predecessors.at(node.inst);
			for (size_t i = 0; i < node.args.size(); ++i) {
				Arm64Label next, first;
				m_code.LDRB_safe(Arm64Reg::X10, Arm64Reg::X19, m_reach + blocks[i], Arm64Reg::X11);
				m_code.CBZ(Arm64Reg::X10, next, false);
				Fetch(node.args[i], thunk_ret, Arm64Reg::X0);
				m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X11);
				m_code.CMP_imm(Arm64Reg::X9, 1, false);
				m_code.B(Arm64Cond::EQ, first);
				m_code.LDR_safe(Arm64Reg::X10, Arm64Reg::X19, node.slot, Arm64Reg::X11, true);
				m_code.CMP(Arm64Reg::X0, Arm64Reg::X10, true);
				m_code.B(Arm64Cond::NE, thunk_ret);
				m_code.B(next);
				m_code.Bind(first);
				m_code.STR_safe(Arm64Reg::X0, Arm64Reg::X19, node.slot, Arm64Reg::X11, true);
				m_code.MOV_imm32(Arm64Reg::X9, 3);
				m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X11);
				m_code.Bind(next);
			}
			m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X11);
			m_code.CMP_imm(Arm64Reg::X9, 3, false);
			m_code.B(Arm64Cond::NE, thunk_ret);
			m_code.MOV_imm32(Arm64Reg::X9, 2);
			m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, node.state, Arm64Reg::X11);
			m_code.LDP_post(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, 16);
			m_code.RET();
			break;
		}
		case ValueOpcode::LoadAddressU32:
		case ValueOpcode::ReadConstBuffer:
		case ValueOpcode::LoadBufferU32:
			EmitRead(node, thunk_ret);
			break;
		case ValueOpcode::SelectU1:
		case ValueOpcode::SelectU32:
		case ValueOpcode::SelectF32: {
			Arm64Label other;
			Fetch(node.args[0], thunk_ret, Arm64Reg::X0);
			m_code.CBZ(Arm64Reg::X0, other, false);
			Fetch(node.args[1], thunk_ret, Arm64Reg::X0);
			Store(node);
			m_code.Bind(other);
			Fetch(node.args[2], thunk_ret, Arm64Reg::X0);
			Store(node);
			break;
		}
		case ValueOpcode::ULessThan32:
		case ValueOpcode::ULessThanEqual32: {
			const bool less = node.inst->GetOpcode() == ValueOpcode::ULessThan32;
			Arm64Label bound_zero;
			Fetch(node.args[0], thunk_ret, Arm64Reg::X0);
			m_code.CBZ(Arm64Reg::X0, bound_zero, false);
			Branch(node.args[1], thunk_ret);
			Load(Arm64Reg::X0, node.args[0]);
			Load(Arm64Reg::X1, node.args[1]);
			m_code.CMP(Arm64Reg::X1, Arm64Reg::X0, false);
			m_code.CSET(Arm64Reg::X0, less ? Arm64Cond::CC : Arm64Cond::CS);
			Store(node);
			m_code.Bind(bound_zero);
			m_code.MOV_imm32(Arm64Reg::X0, !less ? 1u : 0u);
			Store(node);
			break;
		}
		case ValueOpcode::LogicalOr:
		case ValueOpcode::LogicalAnd: {
			const bool conjunction = node.inst->GetOpcode() == ValueOpcode::LogicalAnd;
			Arm64Label unknown_left, refine, decided;
			const auto decide = [&] {
				if (conjunction) {
					m_code.CBZ(Arm64Reg::X0, decided, false);
				} else {
					m_code.CBNZ(Arm64Reg::X0, decided, false);
				}
			};
			Branch(node.args[0], unknown_left);
			if (node.args[0].kind != Kind::Unknown) {
				Load(Arm64Reg::X0, node.args[0]);
				decide();
				Fetch(node.args[1], thunk_ret, Arm64Reg::X0);
				m_code.CMP(Arm64Reg::X0, Arm64Reg::XZR, true);
				m_code.CSET(Arm64Reg::X0, Arm64Cond::NE);
				Store(node);
			}
			m_code.Bind(unknown_left);
			if (MayBeUnknown(node.args[0])) {
				if (node.args[1].kind != Kind::Unknown) {
					Fetch(node.args[1], refine, Arm64Reg::X0);
					decide();
				}
				m_code.Bind(refine);
				Fetch(node.args[2], thunk_ret, Arm64Reg::X0);
				m_code.CBNZ(Arm64Reg::X0, thunk_ret, false);
			}
			m_code.Bind(decided);
			m_code.MOV_imm32(Arm64Reg::X0, !conjunction ? 1u : 0u);
			Store(node);
			break;
		}
		default:
			EmitStrict(node, thunk_ret);
			break;
	}

	m_code.Bind(thunk_ret);
	m_code.LDP_post(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, 16);
	m_code.RET();
}

void Arm64WalkerCompiler::EmitRead(const Node& node, Arm64Label& thunk_ret) {
	const auto& inst   = *node.inst;
	const auto  op     = inst.GetOpcode();
	const bool  vector = op == ValueOpcode::LoadBufferU32;
	auto        args   = std::span(node.args);
	Arm64Label  zero;

	if (vector) {
		Fetch(args[0], thunk_ret, Arm64Reg::X0);
		m_code.CBZ(Arm64Reg::X0, zero, false);
		args = args.subspan(1);
		if (args.empty()) {
			m_code.B(thunk_ret);
		}
	}

	if (!args.empty()) {
		for (const auto& arg: args) Branch(arg, thunk_ret);
		const auto immediate =
		    static_cast<int64_t>(static_cast<int32_t>(m_plan.memory_info[inst.Flags<MemoryFlags>().index].offset)) &
		    ~int64_t {3};

		// High 16 bits of 48-bit base into X1 (masked from args[1]), Low 32 bits into X0
		Load(Arm64Reg::X1, args[1]);
		m_code.UBFX(Arm64Reg::X1, Arm64Reg::X1, 0, 16);
		m_code.LSL_imm(Arm64Reg::X1, Arm64Reg::X1, 32, true);
		Load(Arm64Reg::X0, args[0]);
		m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 2, 30); // clear low 2 bits
		m_code.LSL_imm(Arm64Reg::X0, Arm64Reg::X0, 2, true);
		m_code.ORR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); // base 48-bit

		// Offset
		Load(Arm64Reg::X2, args[2]);
		m_code.UBFX(Arm64Reg::X2, Arm64Reg::X2, 2, 30);
		m_code.LSL_imm(Arm64Reg::X2, Arm64Reg::X2, 2, true);

		if (op == ValueOpcode::LoadAddressU32) {
			m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X2, true);
			if (immediate != 0) {
				m_code.MOV_imm64(Arm64Reg::X9, static_cast<uint64_t>(immediate));
				m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, true);
			}
			m_code.LSR_imm(Arm64Reg::X9, Arm64Reg::X0, 48, true);
			m_code.CBNZ(Arm64Reg::X9, thunk_ret, true); // address > 48-bit
		} else {
			// Buffer read: records & stride
			Load(Arm64Reg::X11, args[1]); // high word
			m_code.LSR_imm(Arm64Reg::X11, Arm64Reg::X11, 16, false);
			if (vector) {
				m_code.MOV_imm32(Arm64Reg::X9, 1u << 15u);
				m_code.TST(Arm64Reg::X11, Arm64Reg::X9, false);
				m_code.B(Arm64Cond::NE, thunk_ret);

				Load(Arm64Reg::X10, args[4]); // word3
				m_code.MOV_imm32(Arm64Reg::X9, (1u << 23u) | 0xf0000000u);
				m_code.TST(Arm64Reg::X10, Arm64Reg::X9, false);
				m_code.B(Arm64Cond::NE, thunk_ret);

				m_code.MOV_imm32(Arm64Reg::X9, 0x3fffu);
				m_code.TST(Arm64Reg::X11, Arm64Reg::X9, false);
				m_code.B(Arm64Cond::EQ, zero);

				m_code.MOV_imm32(Arm64Reg::X9, 0x7fu << 12u);
				m_code.TST(Arm64Reg::X10, Arm64Reg::X9, false);
				m_code.B(Arm64Cond::EQ, zero);
			}
			m_code.MOV_imm32(Arm64Reg::X9, 0x3fffu);
			m_code.AND(Arm64Reg::X11, Arm64Reg::X11, Arm64Reg::X9, false); // stride

			Load(Arm64Reg::X10, args[3]); // records
			if (vector) {
				m_code.CBZ(Arm64Reg::X10, zero, false);
			}
			Arm64Label sized;
			m_code.CBZ(Arm64Reg::X11, sized, false);
			m_code.MUL(Arm64Reg::X10, Arm64Reg::X10, Arm64Reg::X11, true); // size = stride * records
			m_code.Bind(sized);

			m_code.ADD(Arm64Reg::X9, Arm64Reg::X2, Arm64Reg::XZR, true);
			if (immediate != 0) {
				m_code.MOV_imm64(Arm64Reg::X12, static_cast<uint64_t>(immediate + 4));
				m_code.ADD(Arm64Reg::X9, Arm64Reg::X9, Arm64Reg::X12, true);
			} else {
				m_code.ADD_imm(Arm64Reg::X9, Arm64Reg::X9, 4, true);
			}
			m_code.CMP(Arm64Reg::X9, Arm64Reg::X10, true);
			m_code.B(Arm64Cond::HI, zero); // offset + 4 > size -> zero read

			m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X2, true);
			if (immediate != 0) {
				m_code.MOV_imm64(Arm64Reg::X12, static_cast<uint64_t>(immediate));
				m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X12, true);
			}
		}

		// Perform guest read via helper:
		m_code.SUB_imm(Arm64Reg::SP, Arm64Reg::SP, 16, true);
		m_code.STR(Arm64Reg::X0, Arm64Reg::SP, 8, true);       // preserve address across call
		m_code.ADD_imm(Arm64Reg::X1, Arm64Reg::SP, 0, true);   // &out_word
		m_code.MOV(Arm64Reg::X2, Arm64Reg::X24, true);         // read_ctx
		m_code.MOV_imm32(Arm64Reg::X3, vector ? 1u : 0u);      // is_vector
		m_code.BLR(Arm64Reg::X23);                             // call SrtWalkerReadGuestWord
		Arm64Label read_ok;
		m_code.CBNZ(Arm64Reg::X0, read_ok, false);
		m_code.ADD_imm(Arm64Reg::SP, Arm64Reg::SP, 16, true);
		m_code.B(thunk_ret);

		m_code.Bind(read_ok);
		m_code.LDR(Arm64Reg::X0, Arm64Reg::SP, 0, false);      // X0 = out_word
		m_code.LDR(Arm64Reg::X15, Arm64Reg::SP, 8, true);     // restore address
		m_code.ADD_imm(Arm64Reg::SP, Arm64Reg::SP, 16, true);

		// Record capture: [X22] = address, X22 += 8
		++m_captures;
		m_code.STR(Arm64Reg::X15, Arm64Reg::X22, 0, true);
		m_code.ADD_imm(Arm64Reg::X22, Arm64Reg::X22, 8, true);

		Store(node);
	}

	m_code.Bind(zero);
	m_code.MOV_imm32(Arm64Reg::X0, 0);
	Store(node);
}

void Arm64WalkerCompiler::EmitStrict(const Node& node, Arm64Label& thunk_ret) {
	for (const auto& arg: node.args) Branch(arg, thunk_ret);

	const std::array regs {Arm64Reg::X0, Arm64Reg::X1, Arm64Reg::X2, Arm64Reg::X3};
	for (size_t i = 0; i < node.args.size() && i < regs.size(); ++i) {
		Load(regs[i], node.args[i]);
	}

	const auto check_field = [&](Arm64Reg offset, Arm64Reg width) {
		m_code.ADD(Arm64Reg::X9, offset, width, false);
		m_code.CMP_imm(Arm64Reg::X9, 32, false);
		m_code.B(Arm64Cond::HI, thunk_ret);
	};

	const auto& inst = *node.inst;
	switch (inst.GetOpcode()) {
		case ValueOpcode::CompositeConstructU64:
			m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
			m_code.LSL_imm(Arm64Reg::X1, Arm64Reg::X1, 32, true);
			m_code.ORR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true);
			break;
		case ValueOpcode::CompositeExtractU64:
			if (inst.Arg(1).Resolve().U32() != 0u) {
				m_code.LSR_imm(Arm64Reg::X0, Arm64Reg::X0, 32, true);
			}
			m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
			break;
		case ValueOpcode::IAdd32: m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::IAdd64: m_code.ADD(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::ISub32: m_code.SUB(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::ISub64: m_code.SUB(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::IMul32: m_code.MUL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::UMulHi:
			m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
			m_code.UBFX(Arm64Reg::X1, Arm64Reg::X1, 0, 32);
			m_code.MUL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true);
			m_code.LSR_imm(Arm64Reg::X0, Arm64Reg::X0, 32, true);
			break;
		case ValueOpcode::UMin32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSEL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, Arm64Cond::LS, false);
			break;
		case ValueOpcode::UMax32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSEL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, Arm64Cond::HI, false);
			break;
		case ValueOpcode::SMax32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSEL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, Arm64Cond::GE, false);
			break;
		case ValueOpcode::ConvertF32U32:
			m_code.UCVTF(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FMOV_to_gpr(Arm64Reg::X0, Arm64FpReg::S0);
			break;
		case ValueOpcode::ConvertU32F32:
			m_code.FMOV_from_gpr(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FCVTZU(Arm64Reg::X0, Arm64FpReg::S0);
			m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
			break;
		case ValueOpcode::ConvertS32F32:
			m_code.FMOV_from_gpr(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FCVTZS(Arm64Reg::X0, Arm64FpReg::S0);
			m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
			break;
		case ValueOpcode::FPMul32:
			m_code.FMOV_from_gpr(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FMOV_from_gpr(Arm64FpReg::S1, Arm64Reg::X1);
			m_code.FMUL(Arm64FpReg::S0, Arm64FpReg::S0, Arm64FpReg::S1);
			m_code.FMOV_to_gpr(Arm64Reg::X0, Arm64FpReg::S0);
			break;
		case ValueOpcode::FPTrunc32:
			m_code.FMOV_from_gpr(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FRINTZ(Arm64FpReg::S0, Arm64FpReg::S0);
			m_code.FMOV_to_gpr(Arm64Reg::X0, Arm64FpReg::S0);
			break;
		case ValueOpcode::FPRecipIFlag32: {
			m_code.MOV_imm32(Arm64Reg::X9, 0x807fffffu);
			m_code.TST(Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.B(Arm64Cond::NE, thunk_ret);
			m_code.LSR_imm(Arm64Reg::X0, Arm64Reg::X0, 23, false);
			m_code.CBZ(Arm64Reg::X0, thunk_ret, false);
			m_code.CMP_imm(Arm64Reg::X0, 254, false);
			m_code.B(Arm64Cond::CS, thunk_ret);
			m_code.NEG(Arm64Reg::X0, Arm64Reg::X0, false);
			m_code.ADD_imm(Arm64Reg::X0, Arm64Reg::X0, 254, false);
			m_code.LSL_imm(Arm64Reg::X0, Arm64Reg::X0, 23, false);
			break;
		}
		case ValueOpcode::FPIsNan32:
			m_code.MOV_imm32(Arm64Reg::X9, 0x7fffffffu);
			m_code.AND(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.MOV_imm32(Arm64Reg::X9, 0x7f800000u);
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::HI);
			break;
		case ValueOpcode::FPOrdLessThanEqual32:
		case ValueOpcode::FPOrdGreaterThanEqual32: {
			if (inst.Flags<FPCompareFlags>().flush_input_denorms) {
				for (const auto& reg: {Arm64Reg::X0, Arm64Reg::X1}) {
					Arm64Label normal;
					m_code.MOV_imm32(Arm64Reg::X9, 0x7fffffffu);
					m_code.AND(Arm64Reg::X9, reg, Arm64Reg::X9, false);
					m_code.MOV_imm32(Arm64Reg::X10, 0x00800000u);
					m_code.CMP(Arm64Reg::X9, Arm64Reg::X10, false);
					m_code.B(Arm64Cond::CS, normal);
					m_code.MOV_imm32(Arm64Reg::X9, 0x80000000u);
					m_code.AND(reg, reg, Arm64Reg::X9, false);
					m_code.Bind(normal);
				}
			}
			m_code.FMOV_from_gpr(Arm64FpReg::S0, Arm64Reg::X0);
			m_code.FMOV_from_gpr(Arm64FpReg::S1, Arm64Reg::X1);
			m_code.FCMP(Arm64FpReg::S0, Arm64FpReg::S1);
			if (inst.GetOpcode() == ValueOpcode::FPOrdLessThanEqual32) {
				m_code.CSET(Arm64Reg::X0, Arm64Cond::LS);
			} else {
				m_code.CSET(Arm64Reg::X0, Arm64Cond::GE);
			}
			break;
		}
		case ValueOpcode::BitwiseAnd32: m_code.AND(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::BitwiseAnd64: m_code.AND(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::BitwiseOr32:  m_code.ORR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::BitwiseXor32: m_code.EOR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::BitwiseNot32: m_code.MVN(Arm64Reg::X0, Arm64Reg::X0, false); break;
		case ValueOpcode::ShiftLeftLogical32: m_code.LSL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::ShiftLeftLogical64: m_code.LSL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::ShiftRightLogical32: m_code.LSR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::ShiftRightLogical64: m_code.LSR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::ShiftRightArithmetic32: m_code.ASR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false); break;
		case ValueOpcode::ShiftRightArithmetic64: m_code.ASR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, true); break;
		case ValueOpcode::BitFieldUExtract: {
			check_field(Arm64Reg::X1, Arm64Reg::X2);
			Arm64Label done, zero;
			m_code.CBZ(Arm64Reg::X2, zero, false);
			m_code.LSR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.MOV_imm64(Arm64Reg::X9, 1);
			m_code.LSL(Arm64Reg::X9, Arm64Reg::X9, Arm64Reg::X2, true);
			m_code.SUB_imm(Arm64Reg::X9, Arm64Reg::X9, 1, false);
			m_code.AND(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.B(done);
			m_code.Bind(zero);
			m_code.MOV_imm32(Arm64Reg::X0, 0);
			m_code.Bind(done);
			break;
		}
		case ValueOpcode::BitFieldSExtract: {
			check_field(Arm64Reg::X1, Arm64Reg::X2);
			Arm64Label done, zero;
			m_code.CBZ(Arm64Reg::X2, zero, false);
			m_code.LSR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.MOV_imm32(Arm64Reg::X9, 32);
			m_code.SUB(Arm64Reg::X9, Arm64Reg::X9, Arm64Reg::X2, false); // 32 - width
			m_code.LSL(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.ASR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, false);
			m_code.B(done);
			m_code.Bind(zero);
			m_code.MOV_imm32(Arm64Reg::X0, 0);
			m_code.Bind(done);
			break;
		}
		case ValueOpcode::BitFieldInsert: {
			check_field(Arm64Reg::X2, Arm64Reg::X3);
			Arm64Label done;
			m_code.CBZ(Arm64Reg::X3, done, false);
			m_code.MOV_imm64(Arm64Reg::X9, 1);
			m_code.LSL(Arm64Reg::X9, Arm64Reg::X9, Arm64Reg::X3, true);
			m_code.SUB_imm(Arm64Reg::X9, Arm64Reg::X9, 1, false);
			m_code.LSL(Arm64Reg::X9, Arm64Reg::X9, Arm64Reg::X2, false); // mask
			m_code.BIC(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X9, false); // base & ~mask
			m_code.LSL(Arm64Reg::X1, Arm64Reg::X1, Arm64Reg::X2, false);
			m_code.AND(Arm64Reg::X1, Arm64Reg::X1, Arm64Reg::X9, false); // insert & mask
			m_code.ORR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.Bind(done);
			break;
		}
		case ValueOpcode::IEqual32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::EQ);
			break;
		case ValueOpcode::INotEqual32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::NE);
			break;
		case ValueOpcode::IEqual64:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, true);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::EQ);
			break;
		case ValueOpcode::INotEqual64:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, true);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::NE);
			break;
		case ValueOpcode::UGreaterThan32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::HI);
			break;
		case ValueOpcode::SGreaterThanEqual32:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::X1, false);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::GE);
			break;
		case ValueOpcode::LogicalXor:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::XZR, true);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::NE);
			m_code.CMP(Arm64Reg::X1, Arm64Reg::XZR, true);
			m_code.CSET(Arm64Reg::X1, Arm64Cond::NE);
			m_code.EOR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1, false);
			break;
		case ValueOpcode::LogicalNot:
			m_code.CMP(Arm64Reg::X0, Arm64Reg::XZR, true);
			m_code.CSET(Arm64Reg::X0, Arm64Cond::EQ);
			break;
		default:
			EXIT("unexpected ARM64 SRT walker opcode\n");
	}
	Store(node);
}

void Arm64WalkerCompiler::Compile(std::span<const Value> values) {
	const auto& sources   = m_plan.descriptor_sources;
	const auto& blocks    = m_plan.control_flow;
	const bool  resources = values.empty();

	m_emit = {};
	m_emit.resources = resources;
	m_emit.gates.assign(sources.size(), {});
	m_emit.descriptors.resize(sources.size());

	if (resources) Consume(m_emit.gates);
	for (const auto value: values) AddOutput(value, UINT32_MAX);

	if (resources) {
		for (const auto& block: blocks) {
			m_emit.conditions.push_back(Evaluate(block.condition, 0).operand);
		}
		for (const auto& read: m_plan.srt_reads) {
			m_emit.reads.push_back(Evaluate(read.value, 0).operand);
		}
		for (uint32_t source = 0; source < sources.size(); ++source) {
			if (m_emit.gates[source].empty()) continue;
			for (uint32_t word = 0; word < sources[source].dword_count; ++word) {
				const auto& value = sources[source].dwords[word];
				m_emit.descriptors[source].push_back(Evaluate(value, 0).operand);
			}
		}
	}
	for (const auto value: m_outputs) {
		m_emit.outputs.push_back(Evaluate(value, 0).operand);
	}
	Layout();

	auto& gates       = m_emit.gates;
	auto& conditions  = m_emit.conditions;
	auto& reads       = m_emit.reads;
	auto& descriptors = m_emit.descriptors;
	auto& outputs     = m_emit.outputs;

	// Prologue
	m_code.STP_pre(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, -16);
	m_code.MOV(Arm64Reg::X29, Arm64Reg::SP);
	m_code.STP_pre(Arm64Reg::X19, Arm64Reg::X20, Arm64Reg::SP, -16);
	m_code.STP_pre(Arm64Reg::X21, Arm64Reg::X22, Arm64Reg::SP, -16);
	m_code.STP_pre(Arm64Reg::X23, Arm64Reg::X24, Arm64Reg::SP, -16);

	m_code.MOV(Arm64Reg::X20, Arm64Reg::X0, true); // user_data
	m_code.MOV(Arm64Reg::X21, Arm64Reg::X1, true); // flat
	m_code.MOV(Arm64Reg::X19, Arm64Reg::X2, true); // scratch
	m_code.STR_safe(Arm64Reg::X3, Arm64Reg::X19, m_shader_base, Arm64Reg::X9, true); // shader_base
	m_code.MOV(Arm64Reg::X23, Arm64Reg::X4, true); // read_helper_fn
	m_code.MOV(Arm64Reg::X24, Arm64Reg::X5, true); // read_ctx

	// Init captures pointer in X22: scratch + srt_captures
	m_code.MOV_imm32(Arm64Reg::X9, m_plan.srt_captures);
	m_code.ADD(Arm64Reg::X22, Arm64Reg::X19, Arm64Reg::X9, true);

	Arm64Label pass, fail, reset, main_epilogue;
	Zero(m_reach, m_states_end - m_reach);

	if (!resources) {
		for (uint32_t block = 0; block < blocks.size(); ++block) {
			m_code.MOV_imm32(Arm64Reg::X9, 1);
			m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach + block, Arm64Reg::X10);
		}
	} else if (!blocks.empty()) {
		std::vector<std::vector<uint32_t>> rechecks(blocks.size());
		for (uint32_t index = 0; index < m_nodes.size(); ++index) {
			const auto& node = m_nodes[index];
			if (node.inst->GetOpcode() != ValueOpcode::Phi || node.result != Operand {Kind::Node, index}) continue;
			for (const auto block: m_plan.control_phi_predecessors.at(node.inst)) rechecks[block].push_back(node.state);
		}
		std::vector<Arm64Label> starts(blocks.size());
		m_code.MOV_imm32(Arm64Reg::X9, 1);
		m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach, Arm64Reg::X10);

		m_code.Bind(pass);
		for (uint32_t block = 0; block < blocks.size(); ++block) {
			Arm64Label next;
			const auto mark = [&](uint32_t target) {
				Arm64Label reached;
				m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach + target, Arm64Reg::X10);
				m_code.CBNZ(Arm64Reg::X9, reached, false);
				m_code.MOV_imm32(Arm64Reg::X9, 1);
				m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach + target, Arm64Reg::X10);
				for (const auto state: rechecks[target]) {
					m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, state, Arm64Reg::X10);
					m_code.CBNZ(Arm64Reg::X9, reset, false);
				}
				if (target <= block) m_code.B(starts[target]);
				m_code.Bind(reached);
			};

			const auto& successors = blocks[block].successors;
			const auto  condition  = conditions[block];
			m_code.Bind(starts[block]);
			m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach + block, Arm64Reg::X10);
			m_code.CBZ(Arm64Reg::X9, next, false);
			if (condition.kind == Kind::Constant) {
				mark(successors[condition.value != 0u ? 0 : 1]);
			} else {
				if (condition.kind != Kind::Unknown) {
					Arm64Label both, other;
					Fetch(condition, both, Arm64Reg::X0);
					m_code.CBZ(Arm64Reg::X0, other, false);
					mark(successors[0]);
					m_code.B(next);
					m_code.Bind(other);
					mark(successors[1]);
					m_code.B(next);
					m_code.Bind(both);
				}
				for (const auto successor: successors) mark(successor);
			}
			m_code.Bind(next);
		}
	}

	if (resources) {
		const auto flat = [&](uint32_t slot) {
			const auto offset = slot < reads.size() ? m_plan.srt_reads[slot].flat_offset : UINT32_MAX;
			if (offset >= reads.size()) {
				m_code.B(fail);
				return;
			}
			Fetch(reads[slot], fail, Arm64Reg::X0);
			m_code.STR_safe(Arm64Reg::X0, Arm64Reg::X21, offset * sizeof(uint32_t), Arm64Reg::X9, false);
		};
		if (blocks.empty()) {
			for (uint32_t slot = 0; slot < reads.size(); ++slot) flat(slot);
		}
		for (uint32_t block = 0; block < blocks.size(); ++block) {
			if (blocks[block].srt_reads.empty()) continue;
			Arm64Label skip;
			m_code.LDRB_safe(Arm64Reg::X9, Arm64Reg::X19, m_reach + block, Arm64Reg::X10);
			m_code.CBZ(Arm64Reg::X9, skip, false);
			for (const auto slot: blocks[block].srt_reads) flat(slot);
			m_code.Bind(skip);
		}

		for (uint32_t index = 0; index < sources.size() + m_plan.info.images.size(); ++index) {
			const bool     image = index >= sources.size();
			const uint32_t id    = image ? index - static_cast<uint32_t>(sources.size()) : index;
			bool listed = false;
			for (uint32_t block = 0; block < blocks.size(); ++block) {
				const auto& list = image ? blocks[block].images : blocks[block].sources;
				if (std::ranges::find(list, id) == list.end()) continue;
				if (listed) {
					m_code.LDRB_safe(Arm64Reg::X10, Arm64Reg::X19, m_reach + block, Arm64Reg::X11);
					m_code.ORR(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X10, false);
				} else {
					m_code.LDRB_safe(Arm64Reg::X0, Arm64Reg::X19, m_reach + block, Arm64Reg::X11);
				}
				listed = true;
			}
			if (listed) {
				m_code.STRB_safe(Arm64Reg::X0, Arm64Reg::X19, ActiveOffset(m_plan) + index, Arm64Reg::X9);
			} else {
				m_code.MOV_imm32(Arm64Reg::X9, 1);
				m_code.STRB_safe(Arm64Reg::X9, Arm64Reg::X19, ActiveOffset(m_plan) + index, Arm64Reg::X10);
			}
		}

		for (uint32_t source = 0; source < sources.size(); ++source) {
			if (gates[source].empty()) continue;
			Arm64Label skip;
			const auto known_offset = source * DescriptorStride + 8u * sizeof(uint32_t);
			m_code.STR_safe(Arm64Reg::XZR, Arm64Reg::X19, known_offset, Arm64Reg::X9, false);
			Gate(gates[source], skip);
			for (uint32_t word = 0; word < descriptors[source].size(); ++word) {
				Fetch(descriptors[source][word], skip, Arm64Reg::X0);
				m_code.STR_safe(Arm64Reg::X0, Arm64Reg::X19,
				                source * DescriptorStride + word * sizeof(uint32_t), Arm64Reg::X9, false);
			}
			m_code.MOV_imm32(Arm64Reg::X9, 1);
			m_code.STR_safe(Arm64Reg::X9, Arm64Reg::X19, known_offset, Arm64Reg::X10, false);
			m_code.Bind(skip);
		}
	}

	for (uint32_t index = 0; index < outputs.size(); ++index) {
		Arm64Label skip;
		const auto output_offset = OutputOffset(m_plan) + index * 8u;
		m_code.MOV_imm64(Arm64Reg::X9, UINT64_MAX);
		m_code.STR_safe(Arm64Reg::X9, Arm64Reg::X19, output_offset, Arm64Reg::X10, true);
		Gate(m_output_gates[index], skip);
		Fetch(outputs[index], skip, Arm64Reg::X0);
		m_code.UBFX(Arm64Reg::X0, Arm64Reg::X0, 0, 32);
		m_code.STR_safe(Arm64Reg::X0, Arm64Reg::X19, output_offset, Arm64Reg::X9, true);
		m_code.Bind(skip);
	}

	// Success: return captures pointer in X0
	m_code.MOV(Arm64Reg::X0, Arm64Reg::X22, true);
	m_code.B(main_epilogue);

	m_code.Bind(fail);
	m_code.MOV(Arm64Reg::X0, Arm64Reg::XZR, true); // return nullptr

	m_code.Bind(main_epilogue);
	m_code.LDP_post(Arm64Reg::X23, Arm64Reg::X24, Arm64Reg::SP, 16);
	m_code.LDP_post(Arm64Reg::X21, Arm64Reg::X22, Arm64Reg::SP, 16);
	m_code.LDP_post(Arm64Reg::X19, Arm64Reg::X20, Arm64Reg::SP, 16);
	m_code.LDP_post(Arm64Reg::X29, Arm64Reg::X30, Arm64Reg::SP, 16);
	m_code.RET();

	if (resources && !blocks.empty()) {
		m_code.Bind(reset);
		Zero(m_states, m_states_end - m_states);
		m_code.MOV_imm32(Arm64Reg::X9, m_plan.srt_captures);
		m_code.ADD(Arm64Reg::X22, Arm64Reg::X19, Arm64Reg::X9, true);
		m_code.B(pass);
	}

	for (uint32_t index = 0; index < m_nodes.size(); ++index) {
		if (Thunk(index)) EmitNode(index);
	}
}

} // namespace

Arm64WalkCode::~Arm64WalkCode() {
	if (rx_code != nullptr && alloc_size > 0) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
		munmap(rx_code, alloc_size);
#endif
		rx_code = nullptr;
		rw_code = nullptr;
		alloc_size = 0;
	}
}

Arm64WalkCode::Arm64WalkCode(Arm64WalkCode&& other) noexcept
    : rx_code(std::exchange(other.rx_code, nullptr)),
      rw_code(std::exchange(other.rw_code, nullptr)),
      alloc_size(std::exchange(other.alloc_size, 0)),
      user_data_count(std::exchange(other.user_data_count, 0)),
      capture_count(std::exchange(other.capture_count, 0)),
      scratch_bytes(std::exchange(other.scratch_bytes, 0)) {}

Arm64WalkCode& Arm64WalkCode::operator=(Arm64WalkCode&& other) noexcept {
	if (this != &other) {
		if (rx_code != nullptr && alloc_size > 0) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
			munmap(rx_code, alloc_size);
#endif
		}
		rx_code         = std::exchange(other.rx_code, nullptr);
		rw_code         = std::exchange(other.rw_code, nullptr);
		alloc_size      = std::exchange(other.alloc_size, 0);
		user_data_count = std::exchange(other.user_data_count, 0);
		capture_count   = std::exchange(other.capture_count, 0);
		scratch_bytes   = std::exchange(other.scratch_bytes, 0);
	}
	return *this;
}

std::shared_ptr<Arm64WalkCode> CompileArm64SrtWalker(ResourcePlan& plan, std::span<const Value> values) {
	Arm64Emitter emitter;
	Arm64WalkerCompiler compiler(plan, emitter);
	compiler.Compile(values);

	const size_t code_bytes = emitter.ByteSize();
	if (code_bytes == 0) return nullptr;

	constexpr size_t PageSize = 16384;
	const size_t     alloc_size = (code_bytes + PageSize - 1) & ~(PageSize - 1);

#if defined(__APPLE__) && TARGET_OS_IPHONE
	// pthread_jit_write_protect_np is unavailable on iOS. Use RW then mprotect to RX.
	// If the process lacks JIT entitlement, mprotect fails and we fall back (nullptr).
	void* rx_code = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (rx_code == MAP_FAILED) return nullptr;
	std::memcpy(rx_code, emitter.Data(), code_bytes);
	if (mprotect(rx_code, alloc_size, PROT_READ | PROT_EXEC) != 0)
	{
		munmap(rx_code, alloc_size);
		return nullptr;
	}
	sys_icache_invalidate(rx_code, code_bytes);
#elif defined(__APPLE__)
	void* rx_code = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                     MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
	if (rx_code == MAP_FAILED) return nullptr;

	pthread_jit_write_protect_np(0);
	std::memcpy(rx_code, emitter.Data(), code_bytes);
	pthread_jit_write_protect_np(1);
	sys_icache_invalidate(rx_code, code_bytes);
#elif defined(__linux__) || defined(__ANDROID__)
	void* rx_code = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
	                     MAP_ANON | MAP_PRIVATE, -1, 0);
	if (rx_code == MAP_FAILED) return nullptr;
	std::memcpy(rx_code, emitter.Data(), code_bytes);
	mprotect(rx_code, alloc_size, PROT_READ | PROT_EXEC);
	__builtin___clear_cache(static_cast<char*>(rx_code),
	                        static_cast<char*>(rx_code) + code_bytes);
#else
	return nullptr;
#endif

	auto code             = std::make_shared<Arm64WalkCode>();
	code->rx_code         = rx_code;
	code->rw_code         = rx_code;
	code->alloc_size      = alloc_size;
	code->user_data_count = compiler.UserDataCount();
	code->capture_count   = compiler.CaptureCount();
	code->scratch_bytes   = plan.srt_captures;

	plan.srt_scratch.assign(plan.srt_captures / 8u + code->capture_count, 0u);
	plan.srt_user_data = std::max(plan.srt_user_data, code->user_data_count);

	return code;
}

bool RunArm64SrtWalk(const Arm64WalkCode& code, const ResourcePlan& plan, const SrtRuntime& runtime,
                     std::vector<uint32_t>& flat, std::vector<std::pair<uint64_t, uint64_t>>* reads) {
	if (!code.IsValid() || runtime.user_data.size() < code.user_data_count) {
		return false;
	}
	flat.assign(plan.srt_reads.size(), 0u);
	if (reads != nullptr) {
		reads->reserve(64);
	}

	plan.srt_scratch.assign(code.scratch_bytes / 8u + code.capture_count, 0u);

	SrtReadContext ctx;
	ctx.read_mem     = runtime.read_memory != nullptr ? runtime.read_memory : DefaultReadMemory;
	ctx.read_mem_ud  = runtime.read_memory != nullptr ? runtime.userdata : nullptr;
	ctx.read_spec    = runtime.read_specialization_memory != nullptr ? runtime.read_specialization_memory
	                                                                 : ctx.read_mem;
	ctx.read_spec_ud = runtime.userdata;

	using JITFn = const uint64_t* (*)(const uint32_t* user_data,
	                                  uint32_t*       flat,
	                                  uint8_t*        scratch,
	                                  uint64_t        shader_base,
	                                  void*           read_helper,
	                                  void*           read_ctx);

	auto* fn = reinterpret_cast<JITFn>(code.rx_code);
	const auto* captures_end = fn(runtime.user_data.data(),
	                              flat.data(),
	                              reinterpret_cast<uint8_t*>(plan.srt_scratch.data()),
	                              runtime.shader_base,
	                              reinterpret_cast<void*>(&SrtWalkerReadGuestWord),
	                              &ctx);

	if (captures_end == nullptr) {
		return false;
	}

	if (reads != nullptr) {
		const auto* start = plan.srt_scratch.data() + plan.srt_captures / 8u;
		for (const auto* read = start; read != captures_end; ++read) {
			reads->emplace_back(*read, sizeof(uint32_t));
		}
	}
	return true;
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
