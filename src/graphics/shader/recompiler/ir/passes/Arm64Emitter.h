#ifndef EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_ARM64EMITTER_H_
#define EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_ARM64EMITTER_H_

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>

namespace Libs::Graphics::ShaderRecompiler::IR {

enum class Arm64Reg : uint8_t {
	X0 = 0, X1, X2, X3, X4, X5, X6, X7,
	X8, X9, X10, X11, X12, X13, X14, X15,
	X16, X17, X18, X19, X20, X21, X22, X23,
	X24, X25, X26, X27, X28, X29, X30,
	SP = 31, XZR = 31
};

enum class Arm64Cond : uint8_t {
	EQ = 0, NE = 1, CS = 2, CC = 3,
	MI = 4, PL = 5, VS = 6, VC = 7,
	HI = 8, LS = 9, GE = 10, LT = 11,
	GT = 12, LE = 13, AL = 14, NV = 15,
	HS = CS, LO = CC
};

enum class Arm64FpReg : uint8_t {
	S0 = 0, S1, S2, S3, S4, S5, S6, S7
};

struct Arm64Label {
	int32_t             target_offset = -1;
	std::vector<size_t> branch_indices;
};

class Arm64Emitter {
public:
	Arm64Emitter() = default;

	[[nodiscard]] size_t CurrentIndex() const { return m_code.size(); }
	[[nodiscard]] size_t ByteSize() const { return m_code.size() * sizeof(uint32_t); }
	[[nodiscard]] const uint32_t* Data() const { return m_code.data(); }
	[[nodiscard]] const std::vector<uint32_t>& Instructions() const { return m_code; }

	void Emit(uint32_t word) {
		m_code.push_back(word);
	}

	void Bind(Arm64Label& label) {
		label.target_offset = static_cast<int32_t>(m_code.size());
		for (const auto idx : label.branch_indices) {
			assert(idx < m_code.size());
			const int32_t diff = label.target_offset - static_cast<int32_t>(idx);
			uint32_t& insn = m_code[idx];
			if ((insn & 0xfc000000u) == 0x14000000u || (insn & 0xfc000000u) == 0x94000000u) {
				// Unconditional branch B or BL (26-bit immediate)
				insn = (insn & 0xfc000000u) | (static_cast<uint32_t>(diff) & 0x03ffffffu);
			} else if ((insn & 0xff000000u) == 0x54000000u) {
				// Conditional branch B.cond (19-bit immediate at [23:5])
				insn = (insn & 0xff00001fu) | ((static_cast<uint32_t>(diff) & 0x7ffffu) << 5u);
			} else if ((insn & 0x7e000000u) == 0x34000000u) {
				// Compare and branch CBZ / CBNZ (19-bit immediate at [23:5])
				insn = (insn & 0xff00001fu) | ((static_cast<uint32_t>(diff) & 0x7ffffu) << 5u);
			}
		}
		label.branch_indices.clear();
	}

	void B(Arm64Label& label) {
		if (label.target_offset >= 0) {
			const int32_t diff = label.target_offset - static_cast<int32_t>(m_code.size());
			Emit(0x14000000u | (static_cast<uint32_t>(diff) & 0x03ffffffu));
		} else {
			label.branch_indices.push_back(m_code.size());
			Emit(0x14000000u);
		}
	}

	void BL(Arm64Label& label) {
		if (label.target_offset >= 0) {
			const int32_t diff = label.target_offset - static_cast<int32_t>(m_code.size());
			Emit(0x94000000u | (static_cast<uint32_t>(diff) & 0x03ffffffu));
		} else {
			label.branch_indices.push_back(m_code.size());
			Emit(0x94000000u);
		}
	}

	void B(Arm64Cond cond, Arm64Label& label) {
		if (label.target_offset >= 0) {
			const int32_t diff = label.target_offset - static_cast<int32_t>(m_code.size());
			Emit(0x54000000u | ((static_cast<uint32_t>(diff) & 0x7ffffu) << 5u) | static_cast<uint8_t>(cond));
		} else {
			label.branch_indices.push_back(m_code.size());
			Emit(0x54000000u | static_cast<uint8_t>(cond));
		}
	}

	void CBZ(Arm64Reg reg, Arm64Label& label, bool is64 = true) {
		const uint32_t base = is64 ? 0xb4000000u : 0x34000000u;
		const uint32_t r = static_cast<uint8_t>(reg) & 0x1fu;
		if (label.target_offset >= 0) {
			const int32_t diff = label.target_offset - static_cast<int32_t>(m_code.size());
			Emit(base | ((static_cast<uint32_t>(diff) & 0x7ffffu) << 5u) | r);
		} else {
			label.branch_indices.push_back(m_code.size());
			Emit(base | r);
		}
	}

	void CBNZ(Arm64Reg reg, Arm64Label& label, bool is64 = true) {
		const uint32_t base = is64 ? 0xb5000000u : 0x35000000u;
		const uint32_t r = static_cast<uint8_t>(reg) & 0x1fu;
		if (label.target_offset >= 0) {
			const int32_t diff = label.target_offset - static_cast<int32_t>(m_code.size());
			Emit(base | ((static_cast<uint32_t>(diff) & 0x7ffffu) << 5u) | r);
		} else {
			label.branch_indices.push_back(m_code.size());
			Emit(base | r);
		}
	}

	void RET(Arm64Reg reg = Arm64Reg::X30) {
		Emit(0xd65f0000u | (R(reg) << 5u));
	}

	void BLR(Arm64Reg reg) {
		Emit(0xd63f0000u | (R(reg) << 5u));
	}

	void STP_pre(Arm64Reg r1, Arm64Reg r2, Arm64Reg base, int32_t imm7_scaled) {
		Emit(0xa9800000u | ((static_cast<uint32_t>(imm7_scaled) & 0x7fu) << 15u) | (R(r2) << 10u) | (R(base) << 5u) | R(r1));
	}

	void LDP_post(Arm64Reg r1, Arm64Reg r2, Arm64Reg base, int32_t imm7_scaled) {
		Emit(0xa8c00000u | ((static_cast<uint32_t>(imm7_scaled) & 0x7fu) << 15u) | (R(r2) << 10u) | (R(base) << 5u) | R(r1));
	}

	void MOV(Arm64Reg dst, Arm64Reg src, bool is64 = true) {
		Emit((is64 ? 0xaa0003e0u : 0x2a0003e0u) | (R(src) << 16u) | R(dst));
	}

	void MOV_imm32(Arm64Reg dst, uint32_t imm) {
		Emit(0x52800000u | ((imm & 0xffffu) << 5u) | R(dst));
		if ((imm >> 16u) != 0) {
			Emit(0x72a00000u | (((imm >> 16u) & 0xffffu) << 5u) | R(dst));
		}
	}

	void MOV_imm64(Arm64Reg dst, uint64_t imm) {
		if (imm <= 0xffffffffull) {
			MOV_imm32(dst, static_cast<uint32_t>(imm));
			return;
		}
		bool first = true;
		for (uint32_t shift = 0; shift < 4; ++shift) {
			const uint16_t chunk = static_cast<uint16_t>((imm >> (shift * 16u)) & 0xffffu);
			if (chunk != 0 || (first && shift == 0 && imm == 0)) {
				if (first) {
					Emit(0xd2800000u | (shift << 21u) | (static_cast<uint32_t>(chunk) << 5u) | R(dst));
					first = false;
				} else {
					Emit(0xf2800000u | (shift << 21u) | (static_cast<uint32_t>(chunk) << 5u) | R(dst));
				}
			}
		}
	}

	void ADD(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0x8b000000u : 0x0b000000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void ADD_imm(Arm64Reg dst, Arm64Reg src, uint32_t imm12, bool is64 = true) {
		assert(imm12 <= 0xfffu);
		Emit((is64 ? 0x91000000u : 0x11000000u) | (imm12 << 10u) | (R(src) << 5u) | R(dst));
	}

	void SUB(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0xcb000000u : 0x4b000000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void SUB_imm(Arm64Reg dst, Arm64Reg src, uint32_t imm12, bool is64 = true) {
		assert(imm12 <= 0xfffu);
		Emit((is64 ? 0xd1000000u : 0x51000000u) | (imm12 << 10u) | (R(src) << 5u) | R(dst));
	}

	void MUL(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = false) {
		Emit((is64 ? 0x9b007c00u : 0x1b007c00u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void UMULH(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2) {
		Emit(0x9ba07c00u | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void AND(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0x8a000000u : 0x0a000000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void ORR(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0xaa000000u : 0x2a000000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void EOR(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0xca000000u : 0x4a000000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void BIC(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0x8a200000u : 0x0a200000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void MVN(Arm64Reg dst, Arm64Reg src, bool is64 = true) {
		Emit((is64 ? 0xaa2003e0u : 0x2a2003e0u) | (R(src) << 16u) | R(dst));
	}

	void NEG(Arm64Reg dst, Arm64Reg src, bool is64 = true) {
		SUB(dst, Arm64Reg::XZR, src, is64);
	}

	void LSL(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = false) {
		Emit((is64 ? 0x9ac02000u : 0x1ac02000u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void LSL_imm(Arm64Reg dst, Arm64Reg src, uint32_t shift, bool is64 = false) {
		if (is64) {
			assert(shift < 64u);
			Emit(0xd3400000u | (((64u - shift) & 63u) << 16u) | ((63u - shift) << 10u) | (R(src) << 5u) | R(dst));
		} else {
			assert(shift < 32u);
			Emit(0x53000000u | (((32u - shift) & 31u) << 16u) | ((31u - shift) << 10u) | (R(src) << 5u) | R(dst));
		}
	}

	void LSR(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = false) {
		Emit((is64 ? 0x9ac02400u : 0x1ac02400u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void LSR_imm(Arm64Reg dst, Arm64Reg src, uint32_t shift, bool is64 = false) {
		if (is64) {
			assert(shift < 64u);
			Emit(0xd340fc00u | (shift << 16u) | (R(src) << 5u) | R(dst));
		} else {
			assert(shift < 32u);
			Emit(0x53007c00u | (shift << 16u) | (R(src) << 5u) | R(dst));
		}
	}

	void ASR(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, bool is64 = false) {
		Emit((is64 ? 0x9ac02800u : 0x1ac02800u) | (R(src2) << 16u) | (R(src1) << 5u) | R(dst));
	}

	void ASR_imm(Arm64Reg dst, Arm64Reg src, uint32_t shift, bool is64 = false) {
		if (is64) {
			assert(shift < 64u);
			Emit(0x9340fc00u | (shift << 16u) | (R(src) << 5u) | R(dst));
		} else {
			assert(shift < 32u);
			Emit(0x13007c00u | (shift << 16u) | (R(src) << 5u) | R(dst));
		}
	}

	void UBFX(Arm64Reg dst, Arm64Reg src, uint32_t lsb, uint32_t width) {
		assert(lsb < 32u && width > 0u && lsb + width <= 32u);
		Emit(0x53000000u | (lsb << 16u) | ((lsb + width - 1u) << 10u) | (R(src) << 5u) | R(dst));
	}

	void SBFX(Arm64Reg dst, Arm64Reg src, uint32_t lsb, uint32_t width) {
		assert(lsb < 32u && width > 0u && lsb + width <= 32u);
		Emit(0x13000000u | (lsb << 16u) | ((lsb + width - 1u) << 10u) | (R(src) << 5u) | R(dst));
	}

	void BFI(Arm64Reg dst, Arm64Reg src, uint32_t lsb, uint32_t width) {
		assert(lsb < 32u && width > 0u && lsb + width <= 32u);
		Emit(0x33000000u | (((32u - lsb) & 31u) << 16u) | ((width - 1u) << 10u) | (R(src) << 5u) | R(dst));
	}

	void CMP(Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0xeb00001fu : 0x6b00001fu) | (R(src2) << 16u) | (R(src1) << 5u));
	}

	void CMP_imm(Arm64Reg src, uint32_t imm12, bool is64 = true) {
		assert(imm12 <= 0xfffu);
		Emit((is64 ? 0xf100001fu : 0x7100001fu) | (imm12 << 10u) | (R(src) << 5u));
	}

	void TST(Arm64Reg src1, Arm64Reg src2, bool is64 = true) {
		Emit((is64 ? 0xea00001fu : 0x6a00001fu) | (R(src2) << 16u) | (R(src1) << 5u));
	}

	void CSET(Arm64Reg dst, Arm64Cond cond) {
		Emit(0x1a9f07e0u | ((static_cast<uint32_t>(cond) ^ 1u) << 12u) | R(dst));
	}

	void CSEL(Arm64Reg dst, Arm64Reg src1, Arm64Reg src2, Arm64Cond cond, bool is64 = true) {
		Emit((is64 ? 0x9a800000u : 0x1a800000u) | (R(src2) << 16u) | (static_cast<uint32_t>(cond) << 12u) | (R(src1) << 5u) | R(dst));
	}

	void LDR(Arm64Reg dst, Arm64Reg base, uint32_t byte_offset, bool is64 = true) {
		if (is64) {
			assert((byte_offset & 7u) == 0 && (byte_offset >> 3u) <= 0xfffu);
			Emit(0xf9400000u | ((byte_offset >> 3u) << 10u) | (R(base) << 5u) | R(dst));
		} else {
			assert((byte_offset & 3u) == 0 && (byte_offset >> 2u) <= 0xfffu);
			Emit(0xb9400000u | ((byte_offset >> 2u) << 10u) | (R(base) << 5u) | R(dst));
		}
	}

	void STR(Arm64Reg src, Arm64Reg base, uint32_t byte_offset, bool is64 = true) {
		if (is64) {
			assert((byte_offset & 7u) == 0 && (byte_offset >> 3u) <= 0xfffu);
			Emit(0xf9000000u | ((byte_offset >> 3u) << 10u) | (R(base) << 5u) | R(src));
		} else {
			assert((byte_offset & 3u) == 0 && (byte_offset >> 2u) <= 0xfffu);
			Emit(0xb9000000u | ((byte_offset >> 2u) << 10u) | (R(base) << 5u) | R(src));
		}
	}

	void LDRB(Arm64Reg dst, Arm64Reg base, uint32_t byte_offset) {
		assert(byte_offset <= 0xfffu);
		Emit(0x39400000u | (byte_offset << 10u) | (R(base) << 5u) | R(dst));
	}

	void STRB(Arm64Reg src, Arm64Reg base, uint32_t byte_offset) {
		assert(byte_offset <= 0xfffu);
		Emit(0x39000000u | (byte_offset << 10u) | (R(base) << 5u) | R(src));
	}

	void LDR_reg(Arm64Reg dst, Arm64Reg base, Arm64Reg offset_reg, bool is64 = true) {
		Emit((is64 ? 0xf8606800u : 0xb8606800u) | (R(offset_reg) << 16u) | (R(base) << 5u) | R(dst));
	}

	void STR_reg(Arm64Reg src, Arm64Reg base, Arm64Reg offset_reg, bool is64 = true) {
		Emit((is64 ? 0xf8206800u : 0xb8206800u) | (R(offset_reg) << 16u) | (R(base) << 5u) | R(src));
	}

	void LDRB_reg(Arm64Reg dst, Arm64Reg base, Arm64Reg offset_reg) {
		Emit(0x38606800u | (R(offset_reg) << 16u) | (R(base) << 5u) | R(dst));
	}

	void STRB_reg(Arm64Reg src, Arm64Reg base, Arm64Reg offset_reg) {
		Emit(0x38206800u | (R(offset_reg) << 16u) | (R(base) << 5u) | R(src));
	}

	void LDR_safe(Arm64Reg dst, Arm64Reg base, uint32_t byte_offset, Arm64Reg tmp, bool is64 = true) {
		if (is64 && (byte_offset & 7u) == 0 && (byte_offset >> 3u) <= 0xfffu) {
			LDR(dst, base, byte_offset, true);
		} else if (!is64 && (byte_offset & 3u) == 0 && (byte_offset >> 2u) <= 0xfffu) {
			LDR(dst, base, byte_offset, false);
		} else {
			MOV_imm32(tmp, byte_offset);
			LDR_reg(dst, base, tmp, is64);
		}
	}

	void STR_safe(Arm64Reg src, Arm64Reg base, uint32_t byte_offset, Arm64Reg tmp, bool is64 = true) {
		if (is64 && (byte_offset & 7u) == 0 && (byte_offset >> 3u) <= 0xfffu) {
			STR(src, base, byte_offset, true);
		} else if (!is64 && (byte_offset & 3u) == 0 && (byte_offset >> 2u) <= 0xfffu) {
			STR(src, base, byte_offset, false);
		} else {
			MOV_imm32(tmp, byte_offset);
			STR_reg(src, base, tmp, is64);
		}
	}

	void LDRB_safe(Arm64Reg dst, Arm64Reg base, uint32_t byte_offset, Arm64Reg tmp) {
		if (byte_offset <= 0xfffu) {
			LDRB(dst, base, byte_offset);
		} else {
			MOV_imm32(tmp, byte_offset);
			LDRB_reg(dst, base, tmp);
		}
	}

	void STRB_safe(Arm64Reg src, Arm64Reg base, uint32_t byte_offset, Arm64Reg tmp) {
		if (byte_offset <= 0xfffu) {
			STRB(src, base, byte_offset);
		} else {
			MOV_imm32(tmp, byte_offset);
			STRB_reg(src, base, tmp);
		}
	}

	// Floating point 32-bit ops
	void FMOV_from_gpr(Arm64FpReg dst, Arm64Reg src) {
		Emit(0x1e270000u | (R(src) << 5u) | FP(dst));
	}

	void FMOV_to_gpr(Arm64Reg dst, Arm64FpReg src) {
		Emit(0x1e260000u | (FP(src) << 5u) | R(dst));
	}

	void FMUL(Arm64FpReg dst, Arm64FpReg src1, Arm64FpReg src2) {
		Emit(0x1e200800u | (FP(src2) << 16u) | (FP(src1) << 5u) | FP(dst));
	}

	void FRINTZ(Arm64FpReg dst, Arm64FpReg src) {
		Emit(0x1e25c000u | (FP(src) << 5u) | FP(dst));
	}

	void FCMP(Arm64FpReg src1, Arm64FpReg src2) {
		Emit(0x1e202000u | (FP(src2) << 16u) | (FP(src1) << 5u));
	}

	void FCVTZS(Arm64Reg dst, Arm64FpReg src) {
		Emit(0x1e380000u | (FP(src) << 5u) | R(dst));
	}

	void FCVTZU(Arm64Reg dst, Arm64FpReg src) {
		Emit(0x1e390000u | (FP(src) << 5u) | R(dst));
	}

	void SCVTF(Arm64FpReg dst, Arm64Reg src) {
		Emit(0x1e220000u | (R(src) << 5u) | FP(dst));
	}

	void UCVTF(Arm64FpReg dst, Arm64Reg src) {
		Emit(0x1e230000u | (R(src) << 5u) | FP(dst));
	}

private:
	static uint32_t R(Arm64Reg reg) { return static_cast<uint32_t>(reg) & 0x1fu; }
	static uint32_t FP(Arm64FpReg reg) { return static_cast<uint32_t>(reg) & 0x1fu; }

	std::vector<uint32_t> m_code;
};

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif // EMULATOR_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_ARM64EMITTER_H_
