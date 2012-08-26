/******************************************************************************
 * Modified from:
 * 
 * emulate.c
 *
 * Generic x86 (32-bit and 64-bit) instruction decoder and emulator.
 *
 * Copyright (c) 2005 Keir Fraser
 *
 * Linux coding style, mod r/m decoder, segment base fixes, real-mode
 * privileged instructions:
 *
 * Copyright (C) 2006 Qumranet
 *
 *   Avi Kivity <avi@qumranet.com>
 *   Yaniv Kamay <yaniv@qumranet.com>
 *
 * Copyright (C) 2011 Ahyangyi
 *
 *   Yi Yang <ahyangyi@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * From: xen-unstable 10676:af9809f51f81a3c43f276f00c81a52ef558afda4
 */
#include <linux/module.h>
#include "disassem.h"

/* Operand sizes: 8-bit operands or specified/overridden size. */
#define ByteOp      (1<<0)	/* 8-bit operands. */
/* Destination operand type. */
#define ImplicitOps (1<<1)	/* Implicit in opcode. No generic decode. */
#define DstReg      (2<<1)	/* Register operand. */
#define DstMem      (3<<1)	/* Memory operand. */
#define DstAcc      (4<<1)      /* Destination Accumulator */
#define DstDI       (5<<1)	/* Destination is in ES:(E)DI */
#define DstMem64    (6<<1)	/* 64bit memory operand */
#define DstMask     (7<<1)
/* Source operand type. */
#define SrcNone     (0<<4)	/* No source operand. */
#define SrcImplicit (0<<4)	/* Source operand is implicit in the opcode. */
#define SrcReg      (1<<4)	/* Register operand. */
#define SrcMem      (2<<4)	/* Memory operand. */
#define SrcMem16    (3<<4)	/* Memory operand (16-bit). */
#define SrcMem32    (4<<4)	/* Memory operand (32-bit). */
#define SrcImm      (5<<4)	/* Immediate operand. */
#define SrcImmByte  (6<<4)	/* 8-bit sign-extended immediate operand. */
#define SrcOne      (7<<4)	/* Implied '1' */
#define SrcImmUByte (8<<4)      /* 8-bit unsigned immediate operand. */
#define SrcImmU     (9<<4)      /* Immediate operand, unsigned */
#define SrcSI       (0xa<<4)	/* Source is in the DS:RSI */
#define SrcMask     (0xf<<4)
/* Generic ModRM decode. */
#define ModRM       (1<<8)
/* Destination is only written; never read. */
#define Mov         (1<<9)
#define BitOp       (1<<10)
#define MemAbs      (1<<11)      /* Memory operand is absolute displacement */
#define String      (1<<12)     /* String instruction (rep capable) */
#define Stack       (1<<13)     /* Stack instruction (push/pop) */
#define Group       (1<<14)     /* Bits 3:5 of modrm byte extend opcode */
#define GroupDual   (1<<15)     /* Alternate decoding of mod == 3 */
#define GroupMask   0xff        /* Group number stored in bits 0:7 */
/* Misc flags */
#define Lock        (1<<26) /* lock prefix is allowed for the instruction */
#define Priv        (1<<27) /* instruction generates #GP if current CPL != 0 */
#define No64	    (1<<28)
/* Source 2 operand type */
#define Src2None    (0<<29)
#define Src2CL      (1<<29)
#define Src2ImmByte (2<<29)
#define Src2One     (3<<29)
#define Src2Imm16   (4<<29)
#define Src2Mem16   (5<<29) /* Used for Ep encoding. First argument has to be
			       in memory and second argument is located
			       immediately after the first one in memory. */
#define Src2Mask    (7<<29)
#define ReadOnly    ((long)1<<(long)32)
#define Push        ((long)1<<(long)33)
#define Pop         ((long)1<<(long)34)
#define Known       ((long)1<<(long)63)
// Currently dealt with: CMP, TEST

/* Access completed successfully: continue emulation as normal. */
#define X86EMUL_CONTINUE        0
/* Access is unhandleable: bail from emulation and return error to caller. */
#define X86EMUL_UNHANDLEABLE    1
/* Terminate emulation but return success to the caller. */
#define X86EMUL_PROPAGATE_FAULT 2 /* propagate a generated fault to guest */
#define X86EMUL_RETRY_INSTR     3 /* retry the instruction for some reason */
#define X86EMUL_CMPXCHG_FAILED  4 /* cmpxchg did not see expected value */
#define X86EMUL_IO_NEEDED       5 /* IO is needed to complete emulation */

/* Repeat String Operation Prefix */
#define REPE_PREFIX     1
#define REPNE_PREFIX    2

#define offset_in_page(p)    ((unsigned long)(p) & ~PAGE_MASK)

enum {
	Group1_80, Group1_81, Group1_82, Group1_83,
	Group1A, Group3_Byte, Group3, Group4, Group5, Group7,
	Group8, Group9,
};

enum {
	VCPU_SREG_ES,
	VCPU_SREG_CS,
	VCPU_SREG_SS,
	VCPU_SREG_DS,
	VCPU_SREG_FS,
	VCPU_SREG_GS,
	VCPU_SREG_TR,
	VCPU_SREG_LDTR,
};

static u64 opcode_table[256] = {
	/* 0x00 - 0x07 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64 | Push, ImplicitOps | Stack | No64 | Pop,
	/* 0x08 - 0x0F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64 | Push, 0,
	/* 0x10 - 0x17 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64 | Push, ImplicitOps | Stack | No64 | Pop,
	/* 0x18 - 0x1F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	ImplicitOps | Stack | No64 | Push, ImplicitOps | Stack | No64 | Pop,
	/* 0x20 - 0x27 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	DstAcc | SrcImmByte, DstAcc | SrcImm, 0, 0,
	/* 0x28 - 0x2F */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x30 - 0x37 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	0, 0, 0, 0,
	/* 0x38 - 0x3F */
	ByteOp | DstMem | SrcReg | ModRM | ReadOnly, DstMem | SrcReg | ModRM | ReadOnly,
	ByteOp | DstReg | SrcMem | ModRM, DstReg | SrcMem | ModRM,
	ByteOp | DstAcc | SrcImm, DstAcc | SrcImm,
	0, 0,
	/* 0x40 - 0x47 */
	DstReg, DstReg, DstReg, DstReg, DstReg, DstReg, DstReg, DstReg,
	/* 0x48 - 0x4F */
	DstReg, DstReg, DstReg, DstReg,	DstReg, DstReg, DstReg, DstReg,
	/* 0x50 - 0x57 */
	SrcReg | Stack | Push, SrcReg | Stack | Push, SrcReg | Stack | Push, SrcReg | Stack | Push,
	SrcReg | Stack | Push, SrcReg | Stack | Push, SrcReg | Stack | Push, SrcReg | Stack | Push,
	/* 0x58 - 0x5F */
	DstReg | Stack | Pop, DstReg | Stack | Pop, DstReg | Stack | Pop, DstReg | Stack | Pop,
	DstReg | Stack | Pop, DstReg | Stack | Pop, DstReg | Stack | Pop, DstReg | Stack | Pop,
	/* 0x60 - 0x67 */
	ImplicitOps | Stack | No64, ImplicitOps | Stack | No64,
	0, DstReg | SrcMem32 | ModRM | Mov /* movsxd (x86/64) */ ,
	0, 0, 0, 0,
	/* 0x68 - 0x6F */
	SrcImm | Mov | Stack | Push, 0, SrcImmByte | Mov | Stack | Push, 0,
	DstDI | ByteOp | Mov | String, DstDI | Mov | String, /* insb, insw/insd */
	SrcSI | ByteOp | ImplicitOps | String, SrcSI | ImplicitOps | String, /* outsb, outsw/outsd */
	/* 0x70 - 0x77 */
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	/* 0x78 - 0x7F */
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	SrcImmByte, SrcImmByte, SrcImmByte, SrcImmByte,
	/* 0x80 - 0x87 */
	Group | Group1_80, Group | Group1_81,
	Group | Group1_82, Group | Group1_83,
	ByteOp | DstMem | SrcReg | ModRM | ReadOnly, DstMem | SrcReg | ModRM | ReadOnly,
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	/* 0x88 - 0x8F */
	ByteOp | DstMem | SrcReg | ModRM | Mov, DstMem | SrcReg | ModRM | Mov,
	ByteOp | DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstMem | SrcReg | ModRM | Mov, ModRM | DstReg,
	DstReg | SrcMem | ModRM | Mov, Group | Group1A,
	/* 0x90 - 0x97 */
	DstReg, DstReg, DstReg, DstReg,	DstReg, DstReg, DstReg, DstReg,
	/* 0x98 - 0x9F */
	Known, Known, SrcImm | Src2Imm16 | No64, 0,
	ImplicitOps | Stack, ImplicitOps | Stack, 0, 0,
	/* 0xA0 - 0xA7 */
	ByteOp | DstReg | SrcMem | Mov | MemAbs, DstReg | SrcMem | Mov | MemAbs,
	ByteOp | DstMem | SrcReg | Mov | MemAbs, DstMem | SrcReg | Mov | MemAbs,
	ByteOp | SrcSI | DstDI | Mov | String, SrcSI | DstDI | Mov | String,
	ByteOp | SrcSI | DstDI | String, SrcSI | DstDI | String,
	/* 0xA8 - 0xAF */
	0, 0, ByteOp | DstDI | Mov | String, DstDI | Mov | String,
	ByteOp | SrcSI | DstAcc | Mov | String, SrcSI | DstAcc | Mov | String,
	ByteOp | DstDI | String, DstDI | String,
	/* 0xB0 - 0xB7 */
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	ByteOp | DstReg | SrcImm | Mov, ByteOp | DstReg | SrcImm | Mov,
	/* 0xB8 - 0xBF */
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	DstReg | SrcImm | Mov, DstReg | SrcImm | Mov,
	/* 0xC0 - 0xC7 */
	ByteOp | DstMem | SrcImm | ModRM, DstMem | SrcImmByte | ModRM,
	0, ImplicitOps | Stack, 0, 0,
	ByteOp | DstMem | SrcImm | ModRM | Mov, DstMem | SrcImm | ModRM | Mov,
	/* 0xC8 - 0xCF */
	0, 0, 0, ImplicitOps | Stack,
	ImplicitOps, SrcImmByte, ImplicitOps | No64, ImplicitOps,
	/* 0xD0 - 0xD7 */
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	ByteOp | DstMem | SrcImplicit | ModRM, DstMem | SrcImplicit | ModRM,
	0, 0, 0, 0,
	/* 0xD8 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xE7 */
	0, 0, 0, 0,
	ByteOp | SrcImmUByte | DstAcc, SrcImmUByte | DstAcc,
	ByteOp | SrcImmUByte | DstAcc, SrcImmUByte | DstAcc,
	/* 0xE8 - 0xEF */
	SrcImm | Stack, SrcImm | ImplicitOps,
	SrcImmU | Src2Imm16 | No64, SrcImmByte | ImplicitOps,
	SrcNone | ByteOp | DstAcc, SrcNone | DstAcc,
	SrcNone | ByteOp | DstAcc, SrcNone | DstAcc,
	/* 0xF0 - 0xF7 */
	0, 0, 0, 0,
	ImplicitOps | Priv, ImplicitOps, Group | Group3_Byte, Group | Group3,
	/* 0xF8 - 0xFF */
	ImplicitOps, 0, ImplicitOps, ImplicitOps,
	ImplicitOps, ImplicitOps, Group | Group4, Group | Group5,
};

static u64 twobyte_table[256] = {
	/* 0x00 - 0x0F */
	0, Group | GroupDual | Group7, 0, 0,
	0, ImplicitOps, ImplicitOps | Priv, 0,
	ImplicitOps | Priv, ImplicitOps | Priv, 0, 0,
	0, ImplicitOps | ModRM, 0, 0,
	/* 0x10 - 0x1F */
	0, 0, 0, 0, 0, 0, 0, 0, ImplicitOps | ModRM, 0, 0, 0, 0, 0, 0, 0,
	/* 0x20 - 0x2F */
	ModRM | ImplicitOps | Priv, ModRM | Priv,
	ModRM | ImplicitOps | Priv, ModRM | Priv,
	0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x30 - 0x3F */
	ImplicitOps | Priv, 0, ImplicitOps | Priv, 0,
	ImplicitOps, ImplicitOps | Priv, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x40 - 0x47 */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x48 - 0x4F */
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	DstReg | SrcMem | ModRM | Mov, DstReg | SrcMem | ModRM | Mov,
	/* 0x50 - 0x5F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x60 - 0x6F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x70 - 0x7F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0x80 - 0x8F */
	SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm,
	SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm, SrcImm,
	/* 0x90 - 0x9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xA0 - 0xA7 */
	ImplicitOps | Stack | Push, ImplicitOps | Stack | Pop,
	0, DstMem | SrcReg | ModRM | BitOp,
	DstMem | SrcReg | Src2ImmByte | ModRM,
	DstMem | SrcReg | Src2CL | ModRM, 0, 0,
	/* 0xA8 - 0xAF */
	ImplicitOps | Stack | Push, ImplicitOps | Stack | Pop,
	0, DstMem | SrcReg | ModRM | BitOp | Lock,
	DstMem | SrcReg | Src2ImmByte | ModRM,
	DstMem | SrcReg | Src2CL | ModRM,
	ModRM, 0,
	/* 0xB0 - 0xB7 */
	ByteOp | DstMem | SrcReg | ModRM | Lock, DstMem | SrcReg | ModRM | Lock,
	0, DstMem | SrcReg | ModRM | BitOp | Lock,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xB8 - 0xBF */
	0, 0,
	Group | Group8, DstMem | SrcReg | ModRM | BitOp | Lock,
	0, 0, ByteOp | DstReg | SrcMem | ModRM | Mov,
	    DstReg | SrcMem16 | ModRM | Mov,
	/* 0xC0 - 0xCF */
	0, 0, 0, DstMem | SrcReg | ModRM | Mov,
	0, 0, 0, Group | GroupDual | Group9,
	0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xD0 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xE0 - 0xEF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 0xF0 - 0xFF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u64 group_table[] = {
	[Group1_80*8] =
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | Lock,
	ByteOp | DstMem | SrcImm | ModRM | ReadOnly,
	[Group1_81*8] =
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | Lock,
	DstMem | SrcImm | ModRM | ReadOnly,
	[Group1_82*8] =
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | Lock,
	ByteOp | DstMem | SrcImm | ModRM | No64 | ReadOnly,
	[Group1_83*8] =
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | ReadOnly,
	[Group1A*8] =
	DstMem | SrcNone | ModRM | Mov | Stack, 0, 0, 0, 0, 0, 0, 0,
	[Group3_Byte*8] =
	ByteOp | SrcImm | DstMem | ModRM | ReadOnly, 0,
	ByteOp | DstMem | SrcNone | ModRM, ByteOp | DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group3*8] =
	DstMem | SrcImm | ModRM | ReadOnly, 0,
	DstMem | SrcNone | ModRM, DstMem | SrcNone | ModRM,
	0, 0, 0, 0,
	[Group4*8] =
	ByteOp | DstMem | SrcNone | ModRM | Lock, ByteOp | DstMem | SrcNone | ModRM | Lock,
	0, 0, 0, 0, 0, 0,
	[Group5*8] =
	DstMem | SrcNone | ModRM | Lock, DstMem | SrcNone | ModRM | Lock,
	SrcMem | ModRM | Stack, 0,
	SrcMem | ModRM | Stack, SrcMem | ModRM | Src2Mem16 | ImplicitOps,
	SrcMem | ModRM | Stack | Push, 0,
	[Group7*8] =
	0, 0, ModRM | SrcMem | Priv, ModRM | SrcMem | Priv,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov | Priv, SrcMem | ModRM | ByteOp | Priv,
	[Group8*8] =
	0, 0, 0, 0,
	DstMem | SrcImmByte | ModRM, DstMem | SrcImmByte | ModRM | Lock,
	DstMem | SrcImmByte | ModRM | Lock, DstMem | SrcImmByte | ModRM | Lock,
	[Group9*8] =
	0, DstMem64 | ModRM | Lock, 0, 0, 0, 0, 0, 0,
};

static u64 group2_table[] = {
	[Group7*8] =
	SrcNone | ModRM | Priv, 0, 0, SrcNone | ModRM | Priv,
	SrcNone | ModRM | DstMem | Mov, 0,
	SrcMem16 | ModRM | Mov | Priv, 0,
	[Group9*8] =
	0, 0, 0, 0, 0, 0, 0, 0,
};

enum kvm_reg {
        VCPU_REGS_RAX = 0,
        VCPU_REGS_RCX = 1,
        VCPU_REGS_RDX = 2,
        VCPU_REGS_RBX = 3,
        VCPU_REGS_RSP = 4,
        VCPU_REGS_RBP = 5,
        VCPU_REGS_RSI = 6,
        VCPU_REGS_RDI = 7,
        VCPU_REGS_R8 = 8,
        VCPU_REGS_R9 = 9,
        VCPU_REGS_R10 = 10,
        VCPU_REGS_R11 = 11,
        VCPU_REGS_R12 = 12,
        VCPU_REGS_R13 = 13,
        VCPU_REGS_R14 = 14,
        VCPU_REGS_R15 = 15,
        VCPU_REGS_RIP,
        NR_VCPU_REGS
};

/* Type, address-of, and value of an instruction's operand. */
struct operand {
	enum { OP_REG, OP_MEM, OP_IMM, OP_NONE } type;
	unsigned int bytes;
	union {
		unsigned long orig_val;
	    u64 orig_val64;
	};
	unsigned long *ptr;
	union {
	    unsigned long *reg;
	    struct segmented_address {
	        ulong ea;
	        unsigned seg;
	    } mem;
	} addr;
	union {
		unsigned long val;
		u64 val64;
		char valptr[sizeof(unsigned long) + 2];
	};
};

struct decode_cache {
	u8 twobyte;
	u8 b;
	u8 lock_prefix;
	u8 rep_prefix;
	u8 op_bytes;
	u8 ad_bytes;
	u8 rex_prefix;
	struct operand src;
	struct operand src2;
	struct operand dst;
	bool has_seg_override;
	u8 seg_override;
	unsigned long d;
	unsigned long regs[NR_VCPU_REGS];
	unsigned long eip;
	/* modrm */
	u8 modrm;
	u8 modrm_mod;
	u8 modrm_reg;
	u8 modrm_rm;
	u8 use_modrm_ea;
	bool rip_relative;
	unsigned long modrm_ea;
	void *modrm_ptr;
	unsigned long modrm_val;
};

static unsigned yy_seg_base(unsigned seg_override)
{
	return 0;
}

static unsigned seg_override(struct decode_cache *c)
{
	if (!c->has_seg_override)
	    return 0;

	return c->seg_override;
}

static void set_seg_override(struct decode_cache *c, int seg)
{
	c->has_seg_override = true;
	c->seg_override = seg;
}

static void *decode_register(u8 modrm_reg, unsigned long *regs, int highbyte_regs)
{
    void *p;

	p = &regs[modrm_reg];
    if (highbyte_regs && modrm_reg >= 4 && modrm_reg < 8)
	    p = (unsigned char *)&regs[modrm_reg & 3] + 1;
	return p;
}

static void fetch_register_operand(struct operand *op)
{
    switch (op->bytes) {
    case 1:
		op->val = *(u8 *)op->addr.reg;
        break;
    case 2:
        op->val = *(u16 *)op->addr.reg;
        break;
    case 4:
        op->val = *(u32 *)op->addr.reg;
        break;
    case 8:
        op->val = *(u64 *)op->addr.reg;
        break;
    }
}

static void decode_register_operand(struct operand *op, struct decode_cache *c, int inhibit_bytereg)
{
    unsigned reg = c->modrm_reg;
    int highbyte_regs = c->rex_prefix == 0;

    if (!(c->d & ModRM))
	    reg = (c->b & 7) | ((c->rex_prefix & 1) << 3);
    op->type = OP_REG;
    if ((c->d & ByteOp) && !inhibit_bytereg) {
	    op->addr.reg = decode_register(reg, c->regs, highbyte_regs);
        op->bytes = 1;
    } else {
	    op->addr.reg = decode_register(reg, c->regs, 0);
	    op->bytes = c->op_bytes;
	}
    fetch_register_operand(op);
    op->orig_val = op->val;
}

static inline unsigned long ad_mask(struct decode_cache *c)
{
	return (1UL << (c->ad_bytes << 3)) - 1;
}
 
/* Access/update address held in a register, based on addressing mode. */
static inline unsigned long address_mask(struct decode_cache *c, unsigned long reg)
{
	if (c->ad_bytes == sizeof(unsigned long))
	    return reg;
    else
        return reg & ad_mask(c);
}
 
static inline unsigned long register_address(struct decode_cache *c, unsigned long reg)
{
	return address_mask(c, reg);
}

static unsigned long long yy_insn_fetch (int size, struct decode_cache* c)
{
    c->eip += size;
    if (size == 1)
    {
        return *(unsigned char *)(c->eip-1);
    }
    if (size == 2)
    {
        return *(unsigned short *)(c->eip-2);
    }
    if (size == 4)
    {
        return *(unsigned int *)(c->eip-4);
    }
    if (size == 8)
    {
        return *(unsigned long long *)(c->eip-8);
    }
    return 0;
}

void yy_get_memory_access (struct pt_regs * regs, size_t* data_addr, short* data_length, bool* is_write)
{
    struct decode_cache _c;
	struct decode_cache *c = &_c;
    const int def_op_bytes = 4;
    const int def_ad_bytes = 8;
    int group;
    int rc = X86EMUL_CONTINUE;

    memset(&_c, 0, sizeof(_c));

	c->op_bytes = def_op_bytes;
	c->ad_bytes = def_ad_bytes;

    c->regs[0] = regs->ax;
    c->regs[1] = regs->cx;
    c->regs[2] = regs->dx;
    c->regs[3] = regs->bx;
    c->regs[4] = regs->sp;
    c->regs[5] = regs->bp;
    c->regs[6] = regs->si;
    c->regs[7] = regs->di;
    c->regs[8] = regs->r8;
    c->regs[9] = regs->r9;
    c->regs[10] = regs->r10;
    c->regs[11] = regs->r11;
    c->regs[12] = regs->r12;
    c->regs[13] = regs->r13;
    c->regs[14] = regs->r14;
    c->regs[15] = regs->r15;

    c->eip = regs->ip;

    memset(data_length, 0,sizeof(short)  * MEM_PER_INSTR);

	/* Legacy prefixes. */
	for (;;) {
        c->b = yy_insn_fetch(1, c);
   
//        printk("c->b = %02x\n", (int)(unsigned char)c->b);

		switch (c->b) {
		case 0x66:	/* operand-size override */
			/* switch between 2/4 bytes */
			c->op_bytes = def_op_bytes ^ 6;
			break;
		case 0x67:	/* address-size override */
			/* switch between 4/8 bytes */
			c->ad_bytes = def_ad_bytes ^ 12;
			break;
		case 0x26:	/* ES override */
		case 0x2e:	/* CS override */
		case 0x36:	/* SS override */
		case 0x3e:	/* DS override */
			set_seg_override(c, (c->b >> 3) & 3);
			break;
		case 0x64:	/* FS override */
		case 0x65:	/* GS override */
			set_seg_override(c, c->b & 7);
			break;
		case 0x40 ... 0x4f: /* REX */
			c->rex_prefix = c->b;
			continue;
		case 0xf0:	/* LOCK */
			c->lock_prefix = 1;
			break;
		case 0xf2:	/* REPNE/REPNZ */
			c->rep_prefix = REPNE_PREFIX;
			break;
		case 0xf3:	/* REP/REPE/REPZ */
			c->rep_prefix = REPE_PREFIX;
			break;
		default:
			goto done_prefixes;
		}

		/* Any legacy prefix after a REX prefix nullifies its effect. */

		c->rex_prefix = 0;
	}

done_prefixes:

	/* REX prefix. */
	if (c->rex_prefix)
		if (c->rex_prefix & 8)
			c->op_bytes = 8;	/* REX.W */

//    printk ("%d\n", c->op_bytes);

	/* Opcode byte(s). */
	c->d = opcode_table[c->b];
	if (c->d == 0) {
		/* Two-byte opcode? */
		if (c->b == 0x0f) {
			c->twobyte = 1;
			c->b = yy_insn_fetch(1, c);
			c->d = twobyte_table[c->b];
		}
	}

	if (c->d & Group) {
		group = c->d & GroupMask;
		c->modrm = yy_insn_fetch(1, c);
		--c->eip;

		group = (group << 3) + ((c->modrm >> 3) & 7);
		if ((c->d & GroupDual) && (c->modrm >> 6) == 3)
			c->d = group2_table[group];
		else
			c->d = group_table[group];
	}

    if (c->d == Known)
    {
        //We know it's not a memory access.
        return;
    }

	/* Unrecognised? */
	if (c->d == 0) {
		printk("Cannot understand %02x\n", c->b);
		return;
	}

	if (c->d & Stack)
		c->op_bytes = 8;

	/* ModRM and SIB bytes. */
	if (c->d & ModRM)
    {
	    unsigned char sib;
	    int index_reg = 0, base_reg = 0, scale;

	    if (c->rex_prefix) {
		    c->modrm_reg = (c->rex_prefix & 4) << 1;	/* REX.R */
		    index_reg = (c->rex_prefix & 2) << 2; /* REX.X */
		    c->modrm_rm = base_reg = (c->rex_prefix & 1) << 3; /* REG.B */
	    }

	    c->modrm = yy_insn_fetch(1, c);
	    c->modrm_mod |= (c->modrm & 0xc0) >> 6;
	    c->modrm_reg |= (c->modrm & 0x38) >> 3;
	    c->modrm_rm |= (c->modrm & 0x07);

//        printk ("modrm: mod = %d, reg = %d, rm = %d\n", c->modrm_mod, c->modrm_reg, c->modrm_rm);

	    c->modrm_ea = 0;
	    c->use_modrm_ea = 1;

	    if (c->modrm_mod == 3) {
		    c->modrm_ptr = decode_register(c->modrm_rm,
					           c->regs, c->d & ByteOp);
		    c->modrm_val = *(unsigned long *)c->modrm_ptr;
	    }
        else if (c->ad_bytes == 2) {
		    unsigned bx = regs -> bx;
		    unsigned bp = regs -> bp;
		    unsigned si = regs -> si;
		    unsigned di = regs -> di;

		    /* 16-bit ModR/M decode. */
		    switch (c->modrm_mod) {
		    case 0:
			    if (c->modrm_rm == 6)
				    c->modrm_ea += yy_insn_fetch(2, c);
			    break;
		    case 1:
			    c->modrm_ea += yy_insn_fetch(1, c);
			    break;
		    case 2:
			    c->modrm_ea += yy_insn_fetch(2, c);
			    break;
		    }
		    switch (c->modrm_rm) {
		    case 0:
			    c->modrm_ea += bx + si;
			    break;
		    case 1:
			    c->modrm_ea += bx + di;
			    break;
		    case 2:
			    c->modrm_ea += bp + si;
			    break;
		    case 3:
			    c->modrm_ea += bp + di;
			    break;
		    case 4:
			    c->modrm_ea += si;
			    break;
		    case 5:
			    c->modrm_ea += di;
			    break;
		    case 6:
			    if (c->modrm_mod != 0)
				    c->modrm_ea += bp;
			    break;
		    case 7:
			    c->modrm_ea += bx;
			    break;
		    }
		    if (c->modrm_rm == 2 || c->modrm_rm == 3 ||
		        (c->modrm_rm == 6 && c->modrm_mod != 0))
			    if (!c->has_seg_override)
				    set_seg_override(c, VCPU_SREG_SS);
		    c->modrm_ea = (u16)c->modrm_ea;
	    } else {
		    /* 32/64-bit ModR/M decode. */
		    if ((c->modrm_rm & 7) == 4) {
			    sib = yy_insn_fetch(1, c);
			    index_reg |= (sib >> 3) & 7;
			    base_reg |= sib & 7;
			    scale = sib >> 6;

			    if ((base_reg & 7) == 5 && c->modrm_mod == 0)
				    c->modrm_ea += yy_insn_fetch(4, c);
			    else
				    c->modrm_ea += c->regs[base_reg];
			    if (index_reg != 4)
				    c->modrm_ea += c->regs[index_reg] << scale;
		    } else if ((c->modrm_rm & 7) == 5 && c->modrm_mod == 0) {
				c->rip_relative = 1;
            } else
            {
                c->modrm_ea += c->regs[c->modrm_rm];
            }

		    switch (c->modrm_mod) {
		    case 0:
			    if (c->modrm_rm == 5)
				    c->modrm_ea += yy_insn_fetch(4, c);
		    	break;
		    case 1:
			    c->modrm_ea += yy_insn_fetch(1, c);
			    break;
	    	case 2:
		    	c->modrm_ea += yy_insn_fetch(4, c);
			    break;
			};
	    }
    }
	else if (c->d & MemAbs)
    {
	    switch (c->ad_bytes) {
	    case 2:
		    c->modrm_ea = yy_insn_fetch(2, c);
		    break;
	    case 4:
		    c->modrm_ea = yy_insn_fetch(4, c);
		    break;
	    case 8:
		    c->modrm_ea = yy_insn_fetch(8, c);
		    break;
	    }
    }
	if (rc != X86EMUL_CONTINUE)
		goto done;

	if (!c->has_seg_override)
    {
    	c->has_seg_override = true;
    	c->seg_override = VCPU_SREG_DS;
    }

	if (!(!c->twobyte && c->b == 0x8d))
    {
        if (c->has_seg_override)
    		c->modrm_ea += yy_seg_base(c->seg_override);
    }

	if (c->ad_bytes != 8)
		c->modrm_ea = (u32)c->modrm_ea;

	if (c->rip_relative)
		c->modrm_ea += c->eip;

	/*
	 * Decode and fetch the source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & SrcMask) {
	case SrcNone:
		break;
	case SrcReg:
		decode_register_operand(&c->src, c, 0);
		break;
	case SrcMem16:
		c->src.bytes = 2;
		goto srcmem_common;
	case SrcMem32:
		c->src.bytes = 4;
		goto srcmem_common;
	case SrcMem:
		c->src.bytes = (c->d & ByteOp) ? 1 :
							   c->op_bytes;
		/* Don't fetch the address for invlpg: it could be unmapped. */
		if (c->twobyte && c->b == 0x01 && c->modrm_reg == 7)
			break;
	srcmem_common:
		/*
		 * For instructions with a ModR/M byte, switch to register
		 * access if Mod = 3.
		 */
		if ((c->d & ModRM) && c->modrm_mod == 3) {
			c->src.type = OP_REG;
			c->src.val = c->modrm_val;
			c->src.ptr = c->modrm_ptr;
			break;
		}
		c->src.type = OP_MEM;
		c->src.ptr = (unsigned long *)c->modrm_ea;
		c->src.val = 0;
		break;
	case SrcImm:
	case SrcImmU:
		c->src.type = OP_IMM;
		c->src.ptr = (unsigned long *)c->eip;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		if (c->src.bytes == 8)
			c->src.bytes = 4;
		/* NB. Immediates are sign-extended as necessary. */
		switch (c->src.bytes) {
		case 1:
			c->src.val = yy_insn_fetch(1, c);
			break;
		case 2:
			c->src.val = yy_insn_fetch(2, c);
			break;
		case 4:
			c->src.val = yy_insn_fetch(4, c);
			break;
		}
		if ((c->d & SrcMask) == SrcImmU) {
			switch (c->src.bytes) {
			case 1:
				c->src.val &= 0xff;
				break;
			case 2:
				c->src.val &= 0xffff;
				break;
			case 4:
				c->src.val &= 0xffffffff;
				break;
			}
		}
		break;
	case SrcImmByte:
	case SrcImmUByte:
		c->src.type = OP_IMM;
		c->src.ptr = (unsigned long *)c->eip;
		c->src.bytes = 1;
		if ((c->d & SrcMask) == SrcImmByte)
			c->src.val = yy_insn_fetch(1, c);
		else
			c->src.val = yy_insn_fetch(1, c);
		break;
	case SrcOne:
		c->src.bytes = 1;
		c->src.val = 1;
		break;
	case SrcSI:
		c->src.type = OP_MEM;
		c->src.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->src.addr.mem.ea = register_address(c, c->regs[VCPU_REGS_RSI]);
		c->src.addr.mem.seg = seg_override(c);
		c->src.val = 0;
		break;
	}

	/*
	 * Decode and fetch the second source operand: register, memory
	 * or immediate.
	 */
	switch (c->d & Src2Mask) {
	case Src2None:
		break;
	case Src2CL:
		c->src2.bytes = 1;
		c->src2.val = c->regs[VCPU_REGS_RCX] & 0x8;
		break;
	case Src2ImmByte:
		c->src2.type = OP_IMM;
		c->src2.ptr = (unsigned long *)c->eip;
		c->src2.bytes = 1;
		c->src2.val = yy_insn_fetch(1, c);
		break;
	case Src2Imm16:
		c->src2.type = OP_IMM;
		c->src2.ptr = (unsigned long *)c->eip;
		c->src2.bytes = 2;
		c->src2.val = yy_insn_fetch(2, c);
		break;
	case Src2One:
		c->src2.bytes = 1;
		c->src2.val = 1;
		break;
	case Src2Mem16:
		c->src2.type = OP_MEM;
		c->src2.bytes = 2;
		c->src2.ptr = (unsigned long *)(c->modrm_ea + c->src.bytes);
		c->src2.val = 0;
		break;
	}

	/* Decode and fetch the destination operand: register or memory. */
	switch (c->d & DstMask) {
	case ImplicitOps:
    {
        /* Cannot disassemble */
        rc = X86EMUL_UNHANDLEABLE;
        goto done;
    }
	case DstReg:
		decode_register_operand(&c->dst, c,
			 c->twobyte && (c->b == 0xb6 || c->b == 0xb7));
		break;
	case DstMem:
	case DstMem64:
		if ((c->d & ModRM) && c->modrm_mod == 3) {
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
			c->dst.type = OP_REG;
			c->dst.val = c->dst.orig_val = c->modrm_val;
			c->dst.ptr = c->modrm_ptr;
			break;
		}
		c->dst.type = OP_MEM;
		c->dst.ptr = (unsigned long *)c->modrm_ea;
		if ((c->d & DstMask) == DstMem64)
			c->dst.bytes = 8;
		else
			c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.val = 0;
		if (c->d & BitOp) {
			unsigned long mask = ~(c->dst.bytes * 8 - 1);

			c->dst.ptr = (void *)c->dst.ptr +
						   (c->src.val & mask) / 8;
		}
		break;
	case DstAcc:
		c->dst.type = OP_REG;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.ptr = &c->regs[VCPU_REGS_RAX];
		switch (c->dst.bytes) {
			case 1:
				c->dst.val = *(u8 *)c->dst.ptr;
				break;
			case 2:
				c->dst.val = *(u16 *)c->dst.ptr;
				break;
			case 4:
				c->dst.val = *(u32 *)c->dst.ptr;
				break;
			case 8:
				c->dst.val = *(u64 *)c->dst.ptr;
				break;
		}
		c->dst.orig_val = c->dst.val;
		break;
	case DstDI:
		c->dst.type = OP_MEM;
		c->dst.bytes = (c->d & ByteOp) ? 1 : c->op_bytes;
		c->dst.addr.mem.ea = register_address(c, c->regs[VCPU_REGS_RDI]);
		c->dst.addr.mem.seg = VCPU_SREG_ES;
		c->dst.val = 0;
		break;
	}

    if (c->src.type == OP_MEM)
    {
        data_addr[0] = (size_t)c->src.ptr;
        data_length[0] = c->src.bytes;
        is_write[0] = false;
    }
    else if (c->src2.type == OP_MEM)
    {
        data_addr[0] = (size_t)c->src2.ptr;
        data_length[0] = c->src2.bytes;
        is_write[0] = false;
    }
    else if (c->dst.type == OP_MEM)
    {
        data_addr[0] = (size_t)c->dst.ptr;
        data_length[0] = c->dst.bytes;
        is_write[0] = true;
    }

    if (c->d & Stack)
    {
        if (c->d & Push)
        {
            data_length[0] = c->src.bytes;

            data_addr[0] = c->regs[VCPU_REGS_RSP] - *data_length;
            is_write[0] = true;
        }
        else if (c->d & Pop)
        {
            data_length[0] = c->src.bytes;

            data_addr[0] = c->regs[VCPU_REGS_RSP];
            is_write[0] = false;
        }
    }

    if (data_length[0] != 0 && c->d & ReadOnly)
        is_write[0] = false;

done:
    if (rc != X86EMUL_UNHANDLEABLE)
        return;
    
    
}
