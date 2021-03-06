// license:BSD-3-Clause
// copyright-holders:Andreas Naive,David Haywood
#include "emu.h"
#include "igs036crypt.h"

/****************************************************************************
IGS036 encryption emulation

The encryption used by the IGS036 seems to be another iteration over
previous IGS encryption schemes. Basically, it consists on a rotation-based
non-trivial obfuscation layered upon a simple address-based XOR encryption
(similar to the ones found in previous IGS circuits).

The scheme works on 16-bits words and is probably designed to depend on 24 bits of
(word-) address; in what follows, we will refer to the 8 lowest ones simply as the
lowest bits of the address, and the other 16 as the highest bits the address.

The address-based XOR is thought to be comprised of 16 one-bit XOR controlled
by a certain combination of one or two of the highest address bits (but only
15 of them are observed, probably due to the fact that no game uses more than
2^22 of the 2^24 address space). Every one of the one-bit XORs affects a different bit
of the word. The game key acts by masking on/off those XORs for every combination
of the lowest address bits. Thus, a complete key can be described by 256 16-bits values.
This use of the key is pretty similar to the one found in previous instantiations of
IGS circuits.

What is new in the IGS036 is the use of an obfuscation working this way:

1) The highest bits of the address are split in 4 groups, every of which controls
a rotation by a shift of (plus or minus) 9, 1, 2 and 4 respectively.
2) For every address, the highest bit of the group set in the address controls
the activation/deactivation of the rotation for his group, using an associated
(and fixed) boolean function depending on the lowest bits of the address.
3) If the group rotation is to be activated according to 2), then another
fixed group-level boolean functions (again, depending on the lowest bits of the
address) control the direction (left or right) of the rotation.
4) One of the groups (the associated with the shift by 9) interacts with the other
three by inverting (when active itself) the activation/deactivation patterns of
the other three.
5) The lowest bits of the address control a further rotation(independent
on the highest bits of the address).
6) Besides, a global bitswap is present (which can be considered either part of the
obfuscation or of the encryption, as is done in the interface between both).

All the associated boolean functions are clearly of low complexity, so it should
be expected that the hardware is calculating them that way rather than using
lookup tables or otherwise. It must be stressed that this obfuscation is done
system-wide without dependence on the game keys.

On a different note, the unused tail of the ROMs are pattern-filled and, more interestingly,
that region appears to be hiding 20-bytes values (SHA-1 hashes?) located at
positions which vary per set. See the table below.

driver      20-bytes value position in the ROM
---------   ----------------------------------
orleg2o     $763984-$763997
orleg2      $76C77C-$76C78F
kov3        $718040-$718053

TO-DO: complete the table with the 20-bytes values

*****************************************************************************/

igs036_decryptor::igs036_decryptor(const uint16_t* game_key)
	: key(game_key)
{
}

void igs036_decryptor::decrypter_rom(memory_region* region)
{
	int size = region->bytes();
	uint16_t* rom = (uint16_t*)region->base();
	for (int i = 0; i < size / 2; i++)
	{
		rom[i] = decrypt(rom[i], i);
	}
}

uint16_t igs036_decryptor::decrypt(uint16_t cipherword, int word_address)const
{
	// key-independent manipulation
	int aux = deobfuscate(cipherword, word_address);

	// key-dependent manipulation
	for (int i=0; i<16; ++i)
	{
		if (BIT(key[word_address&0xff],i))
		{
			if (((word_address>>8)&triggers[i][0]) == triggers[i][1])
				aux ^= (1<<i);
		}
	}

	return aux^0x1a3a;
}

uint16_t igs036_decryptor::deobfuscate(uint16_t cipherword, int word_address)const
{
	// key-independent manipulation
	int shift = rotation(word_address);
	int aux = rol(cipherword, shift);
	aux = BITSWAP16(aux, 10,9,8,7,0,15,6,5,   14,13,4,3,12,11,2,1);

	return aux;
}

int igs036_decryptor::rotation(int address)const
{
	const int group15[] = {15,11, 7, 5};  // 15 is a guess
	const int group14[] = {14, 9, 3, 2};  // 14 is a guess
	const int group13[] = {13,10, 6, 1};
	const int group12[] = {12, 8, 4, 0};

	// rotation depending on all the address bits
	int enabled0 = rot_enabled(address, group15);
	int rot = enabled0 * rot_group(address, group15) * 9;

	int enabled1 = enabled0 ^ rot_enabled(address, group14);
	rot += enabled1 * rot_group(address, group14) * 1;

	int enabled2 = enabled0 ^ rot_enabled(address, group13);
	rot += enabled2 * rot_group(address, group13) * 2;

	int enabled3 = enabled0 ^ rot_enabled(address, group12);
	rot += enabled3 * rot_group(address, group12) * 4;

	// block-independent rotation (just depending on the lowest 8 bits)
	int rot2  = 4*BIT(address,0);
	rot2 += 1*BIT(address,4)*(BIT(address,0)*2-1);
	rot2 += 4*BIT(address,3)*(BIT(address,0)*2-1);
	rot2 *= (BIT(address,7)|(BIT(address,0)^BIT(address,1)^1))*2-1;
	rot2 += 2*((BIT(address,0)^BIT(address,1))&(BIT(address,7)^1));

	return (rot+rot2)&0xf;
}

int igs036_decryptor::rot_enabled(int address, const int* group)const
{
	int enabled = 0;
	for (int j=0; j<4; ++j)
	{
		if (BIT(address,8+group[j]))
		{
			int aux = address ^ (0x1b*BIT(address,2));
			enabled = rot_enabling[group[j]][aux&3](aux);
			break;
		}
	}

	return enabled;
}

int igs036_decryptor::rot_group(int address, const int* group)const
{
	int aux = rot_direction[group[0]&3][address&7](address);
	return (aux*2)-1;
}

uint16_t igs036_decryptor::rol(uint16_t num, int shift)const
{
	uint16_t r = num<<shift;
	uint16_t l = num>>(16-shift);

	return r|l;
}

// the triggers describe under what conditions are every one of the 16 XORs activated

const uint16_t igs036_decryptor::triggers[16][2] = {
	{0x0001, 0x0000}, {0x0008, 0x0008}, {0x0002, 0x0000}, {0x0004, 0x0004},
	{0x0100, 0x0000}, {0x0200, 0x0000}, {0x0400, 0x0000}, {0x0800, 0x0800},
	{0x1001, 0x0001}, {0x2002, 0x2000}, {0x4004, 0x4000}, {0x8008, 0x0000},  // (the one at index #10 is a guess; his effect is not observed in any game)
	{0x0010, 0x0010}, {0x0020, 0x0020}, {0x0040, 0x0000}, {0x0081, 0x0081}
};


// The rotation depending on the 16 highest address bits depends on a series
// of function on the 8 lowest word-address bits. Some comments:
// * Bits #5 & #6 are unused so, in fact, they only depend on 6 address bits
// * The functions are clearly low-complexity boolean functions on those 6 bits
//   rather than, say, random lookup tables
// * There are quite a number of functionally equivalent ways to implement
//   those boolean functions, so the given implementation (by multiplexing
//   over some simple functions) shouldn't be taken too seriously: while it's
//   functionally correct, it doesn't neccesarily represent the way the hardware
//   is calculating them.

static int unknown(int address) { return 0; }
static int cZero  (int address) { return 0; }
static int cOne   (int address) { return 1; }
static int bit_3  (int address) { return BIT(address,3); }
static int bit_4  (int address) { return BIT(address,4); }
static int bit_7  (int address) { return BIT(address,7); }
static int not_3  (int address) { return BIT(address,3)^1; }
static int not_4  (int address) { return BIT(address,4)^1; }
static int not_7  (int address) { return BIT(address,7)^1; }
static int xor_37 (int address) { return BIT(address,3)^BIT(address,7); }
static int xnor_37(int address) { return BIT(address,3)^BIT(address,7)^1; }
static int xor_47 (int address) { return BIT(address,4)^BIT(address,7); }
static int xnor_47(int address) { return BIT(address,4)^BIT(address,7)^1; }
static int nor_34 (int address) { return (BIT(address,3)|BIT(address,4))^1; }
static int impl_43(int address) { return BIT(address,3)||(BIT(address,4)^1); }


int (*igs036_decryptor::rot_enabling[16][4])(int)  = {
	{bit_3  , not_3  , bit_3  , not_3  },
	{bit_3  , not_3  , bit_3  , not_3  },
	{bit_4  , bit_4  , bit_4  , bit_4  },
	{bit_4  , not_4  , bit_4  , not_4  },
	{bit_3  , bit_3  , bit_3  , bit_3  },
	{nor_34 , bit_7  , bit_7  , cZero  },
	{cZero  , cOne   , cZero  , cOne   },
	{impl_43, xor_37 , xnor_37, not_3  },
	{bit_3  , bit_3  , not_3  , not_3  },
	{bit_4  , bit_4  , not_4  , not_4  },
	{cZero  , cZero  , cZero  , cZero  },
	{nor_34 , bit_7  , not_7  , cOne   },
	{bit_3  , not_3  , bit_3  , not_3  },
	{cZero  , cOne   , cOne   , cZero  },
	{unknown, unknown, unknown, unknown},
	{unknown, unknown, unknown, unknown},
};

int (*igs036_decryptor::rot_direction[4][8])(int)  = {
	{bit_3  , xor_37 , xnor_37, not_3  , bit_3  , xor_37 , xnor_37, not_3  },
	{cZero  , not_7  , not_7  , cZero  , cZero  , not_7  , not_7  , cZero  },
	{bit_4  , xor_47 , xnor_47, not_4  , bit_4  , xor_47 , xnor_47, not_4  },
	{bit_3  , not_7  , bit_7  , cZero  , cOne   , not_7  , bit_7  , cZero  },
};

// ------------------------GAME KEYS---------------------------

// The keys below have been obtained by an automatic process
// exploiting the simple XOR scheme used by the system. Overall, the process,
// while simple, seems to be pretty robust, so few errors should be expected,
// if any. The only exception is DDPDOJ (see below).

const uint16_t orleg2_key[0x100] = {
	0x8100, 0x9202, 0x3000, 0x1200, 0x0100, 0x0800, 0x2100, 0xab05,
	0x130a, 0xba0a, 0x0308, 0x9200, 0x8306, 0xab0f, 0x200c, 0x0301,
	0x9010, 0x1b13, 0x1310, 0x1b11, 0x8104, 0x0212, 0x8204, 0x8214,
	0x8302, 0x1111, 0x8300, 0x1b19, 0x0110, 0x8202, 0x0310, 0x0301,
	0x8322, 0xb202, 0xb200, 0x9121, 0x8222, 0x0a26, 0x2000, 0x0321,
	0xb000, 0x0020, 0x9328, 0x3909, 0x230a, 0x8929, 0x8224, 0x2204,
	0x0322, 0x9b33, 0x0300, 0x9311, 0x8120, 0x8810, 0x0330, 0x0004,
	0x832a, 0x8a0a, 0x0100, 0x1131, 0x0138, 0x093d, 0x8128, 0x081c,
	0xe342, 0x8101, 0xf140, 0x0000, 0x6144, 0x2004, 0x8204, 0x4044,
	0xa302, 0xdb4b, 0x1000, 0xa200, 0xc044, 0xe044, 0x010c, 0x0204,
	0x1212, 0xdb53, 0xd050, 0xcb41, 0x4150, 0xc347, 0x4340, 0x0101,
	0x5252, 0xd959, 0x1310, 0xc040, 0xc252, 0xc959, 0x4340, 0x8919,
	0x2202, 0x3800, 0xe340, 0x2101, 0x0326, 0x2307, 0x4360, 0x8321,
	0x3000, 0xbb0b, 0x5068, 0xf848, 0x436a, 0xab0b, 0xa10c, 0xe240,
	0xc140, 0xc363, 0x8300, 0x4961, 0x0004, 0xc860, 0x0324, 0x0000,
	0xd070, 0x8101, 0xd070, 0x1331, 0x0104, 0x4a6e, 0x4348, 0x4a78,
	0xa282, 0xb282, 0x0200, 0x2200, 0x8180, 0x8080, 0x8080, 0x0800,
	0x1302, 0x9989, 0x2008, 0x2000, 0xa386, 0x0b0f, 0x828c, 0xa280,
	0x9392, 0x9292, 0x1010, 0x8080, 0x0206, 0x8383, 0x8294, 0x0911,
	0x8382, 0x0a0a, 0x9190, 0x1010, 0x0008, 0x0b0b, 0x8098, 0x8b9d,
	0x1120, 0x0820, 0x2200, 0xa080, 0x81a4, 0xa286, 0xa380, 0xaa80,
	0x0120, 0x1020, 0xb088, 0x1020, 0x2000, 0x0b2b, 0x2100, 0x2a0c,
	0x9292, 0x98b0, 0x1330, 0x8880, 0x8396, 0x0b17, 0x8080, 0x0325,
	0x0000, 0x99b9, 0x92b0, 0x82a0, 0x8184, 0x0020, 0x0330, 0x0818,
	0xe1c0, 0xa981, 0xd1c0, 0xd0c0, 0x6140, 0x4242, 0xc2c4, 0x6345,
	0xb088, 0x7141, 0x4040, 0xa181, 0x220e, 0xe0c4, 0x4144, 0x6a4c,
	0xd0d0, 0x9a92, 0x1310, 0xd9d1, 0x8392, 0xc0c4, 0x8284, 0x8890,
	0xc0c0, 0x8282, 0x8280, 0x9090, 0x4342, 0x0a0a, 0x4240, 0xc1d5,
	0xb080, 0xb282, 0xc1e0, 0x90a0, 0xa084, 0x4b63, 0x81a0, 0xeac0,
	0x7242, 0x5363, 0x7348, 0x0321, 0x022a, 0x6949, 0x4360, 0x8aa8,
	0x8282, 0x0303, 0x8180, 0x1331, 0x83a2, 0x4b53, 0x4364, 0x83b1,
	0x121a, 0x80a0, 0x1238, 0x0000, 0x82ba, 0x0030, 0xc0c0, 0x4264,
};

const uint16_t m312cn_key[0x100] = {
	0x1102, 0x1103, 0x1000, 0xb101, 0x2100, 0x2802, 0xa000, 0x0901,
	0x000a, 0x8101, 0xa000, 0x2909, 0x000e, 0x2004, 0x210c, 0x2909,
	0x8000, 0x0903, 0x0100, 0x8901, 0x0100, 0x8814, 0x8110, 0x0810,
	0x0002, 0x9010, 0x9110, 0x8000, 0x000a, 0x0115, 0x0108, 0x8919,
	0xb000, 0x8022, 0x2000, 0xa000, 0x0024, 0xa101, 0x0020, 0x0121,
	0x0020, 0xb80a, 0x0128, 0x9828, 0x802e, 0x882a, 0x8020, 0xa90d,
	0x9130, 0x8802, 0x8000, 0x9111, 0x0102, 0x0935, 0x8110, 0x0830,
	0x002a, 0x1919, 0x9130, 0x8808, 0x0110, 0x0030, 0x811c, 0x8919,
	0x1000, 0x6141, 0x0000, 0xd941, 0x0104, 0x0006, 0x4140, 0xa804,
	0x810a, 0x890b, 0x8100, 0xb000, 0x8008, 0xc040, 0x2004, 0x6040,
	0x5150, 0x4943, 0x1010, 0xd151, 0x4040, 0x8111, 0x4044, 0x4945,
	0x9012, 0xd959, 0x9118, 0x0000, 0x8116, 0x494b, 0x801c, 0x0010,
	0xd162, 0x0121, 0x3000, 0x4060, 0x0124, 0x8826, 0xa100, 0xc064,
	0xa000, 0xe040, 0xb100, 0x7040, 0xe040, 0x2006, 0x2004, 0x694d,
	0x1032, 0x0901, 0x4160, 0xc961, 0x4174, 0x8022, 0x8120, 0x4850,
	0x1110, 0x8121, 0x1110, 0x9838, 0xc156, 0x4171, 0x0020, 0x883c,
	0xb082, 0xa183, 0x9080, 0x1101, 0x2104, 0x0103, 0xa080, 0xa181,
	0x300a, 0x2000, 0x9180, 0x1000, 0x808e, 0x8187, 0x0100, 0x8185,
	0x9190, 0x9090, 0x8080, 0x0901, 0x0002, 0x0002, 0x0014, 0x8884,
	0x9198, 0x9193, 0x9190, 0x8888, 0x0006, 0x8888, 0x8180, 0x090d,
	0x1020, 0x3101, 0x1020, 0x1820, 0x0024, 0x2907, 0xa080, 0xa181,
	0xb082, 0x0828, 0x0128, 0x80a0, 0x210a, 0x290d, 0x0128, 0x2000,
	0x9190, 0x88a0, 0x0100, 0x8880, 0x80a6, 0x0820, 0x0000, 0x81a1,
	0x80aa, 0x989a, 0x90b0, 0x1010, 0x80a0, 0x8181, 0x8094, 0x0024,
	0x5142, 0x3101, 0x8080, 0xa880, 0x4046, 0xe8c2, 0x0000, 0x6145,
	0x714a, 0x280a, 0x5040, 0x0909, 0x614a, 0x4145, 0x2004, 0xa88c,
	0x4040, 0x1812, 0xd0d0, 0x9191, 0x4140, 0x8195, 0x8180, 0x4155,
	0x4140, 0x191b, 0x0000, 0x8181, 0xc0dc, 0x091b, 0x0118, 0x0111,
	0x5060, 0x1123, 0xe0c0, 0x7840, 0x80a0, 0x4963, 0x2000, 0xc8e4,
	0x80aa, 0x3000, 0xe0c0, 0x1121, 0x6046, 0xa88a, 0x4164, 0x0929,
	0x9092, 0x4961, 0xc0c0, 0x8181, 0x4074, 0x88a4, 0x0110, 0x0810,
	0xc0e2, 0xc0c0, 0x0028, 0x9191, 0x80b2, 0x0020, 0x4040, 0x80b0,
};

const uint16_t cjddzsp_key[0x100] = {
	0x0142, 0x3903, 0xb0c0, 0x0040, 0x8006, 0xa145, 0x2004, 0x0141,
	0x21c0, 0xa1c1, 0x0048, 0x8141, 0x8182, 0x884a, 0x2184, 0x0040,
	0x8102, 0x98d2, 0x8180, 0x8941, 0x80d2, 0x0151, 0x01c0, 0x8901,
	0x1090, 0x98d8, 0x9010, 0x0808, 0x0148, 0x095f, 0x804c, 0x090d,
	0x9122, 0xa9c3, 0xa180, 0xb101, 0x00e4, 0x2000, 0x81e4, 0x8921,
	0x2188, 0x89a9, 0x0128, 0x9020, 0x2042, 0xa002, 0x016c, 0x8121,
	0x01e2, 0x9991, 0x8040, 0x9810, 0x0190, 0x0925, 0x01a4, 0x88c4,
	0x0148, 0x9193, 0x91d8, 0x19d9, 0x801a, 0x8919, 0x8120, 0x81c1,
	0x20c2, 0xf0c2, 0xd100, 0x28c0, 0xe0c4, 0xe880, 0xa080, 0x6985,
	0xe100, 0x9042, 0xa1c8, 0x18c8, 0x6146, 0x80c2, 0x604c, 0x09cd,
	0x8180, 0x4002, 0x0000, 0x4880, 0x4100, 0x00c2, 0x8040, 0xc8d4,
	0x5152, 0x5113, 0xd1d8, 0x1090, 0xc19a, 0x4191, 0x80d8, 0xc0c0,
	0xc0e0, 0x6901, 0xd060, 0x00a0, 0x0062, 0x81a3, 0x6004, 0xe1c1,
	0x6042, 0x496b, 0x40e0, 0x78c8, 0xe188, 0xe0c4, 0x40a8, 0x81a5,
	0x1070, 0x9913, 0x91d0, 0x0080, 0x4172, 0x48f6, 0x8120, 0x8961,
	0x51b8, 0xc060, 0x9058, 0xd939, 0x41d2, 0xc9ff, 0x8188, 0xc80c,
	0x81c2, 0x1802, 0xb040, 0x2840, 0x2006, 0x80c2, 0x8184, 0xa1c1,
	0x10c8, 0x30c2, 0x3040, 0x91c1, 0x810c, 0xa1c1, 0x2180, 0x81c1,
	0x0100, 0x18d0, 0x1190, 0x80c0, 0x8046, 0x0941, 0x8154, 0x0111,
	0x0080, 0x8042, 0x1118, 0x0101, 0x004a, 0x89db, 0x0040, 0x8185,
	0x91a2, 0xb143, 0x11a0, 0x2000, 0x01e6, 0x0824, 0x21c4, 0xa181,
	0x2180, 0xb002, 0x90a0, 0xb181, 0xa1c4, 0x0020, 0xa0c0, 0x2105,
	0x01c2, 0x0183, 0x0040, 0x0000, 0x8132, 0x8185, 0x01b4, 0x8975,
	0x91f2, 0x8022, 0x9058, 0x18d8, 0x0128, 0x889e, 0x808c, 0x00f4,
	0x11c0, 0xe840, 0x1140, 0x6981, 0x41c4, 0x00c6, 0x60c4, 0x6884,
	0xa0c8, 0x3143, 0x7180, 0x00c0, 0xa188, 0xc909, 0x4140, 0xa84c,
	0x51d2, 0x0842, 0x8080, 0x9151, 0x81c0, 0x4084, 0x0144, 0x8915,
	0x111a, 0x4103, 0x8000, 0x48c8, 0x01de, 0x00c0, 0xc004, 0xc044,
	0xb100, 0xf983, 0x5060, 0x61c1, 0xe180, 0x2880, 0xe080, 0x8925,
	0x90aa, 0xf8ca, 0xe148, 0x3989, 0x6080, 0x8024, 0x6184, 0x61c5,
	0xc0a0, 0x0020, 0x5190, 0x9111, 0x8180, 0x01b3, 0xc0e4, 0xc191,
	0x4082, 0xd9d9, 0x5110, 0x4000, 0xc164, 0x41c7, 0xc050, 0xc9bd,
};

const uint16_t cjdh2_key[0x100] = {
	0x1180, 0x2983, 0x8080, 0x3981, 0x2182, 0x2181, 0x2100, 0x2901,
	0x2182, 0x198b, 0xa008, 0x8181, 0x0106, 0xa000, 0x018c, 0x880c,
	0x1110, 0x8101, 0x1190, 0x9890, 0x8090, 0x0812, 0x8014, 0x0111,
	0x909a, 0x8808, 0x8000, 0x0101, 0x811a, 0x8000, 0x8088, 0x8195,
	0x9120, 0x9123, 0x90a0, 0x1020, 0x00a0, 0x0125, 0x8120, 0xa985,
	0x90aa, 0x2808, 0xa180, 0x1828, 0x81a4, 0xa808, 0xa080, 0x00a4,
	0x9032, 0x0820, 0x1110, 0x8101, 0x8034, 0x8103, 0x8100, 0x01a1,
	0x01a2, 0x8002, 0x81a8, 0x1818, 0x0092, 0x8127, 0x8184, 0x8888,
	0xb102, 0xa802, 0x7140, 0xb000, 0x8102, 0x8000, 0x41c4, 0x0181,
	0xc140, 0x390b, 0x40c0, 0x1909, 0xe1c2, 0x68ca, 0x2104, 0xa181,
	0x1092, 0x59d1, 0xd050, 0x9111, 0x0014, 0x48c6, 0x41d4, 0x8185,
	0x414a, 0xd1d3, 0x50d0, 0x51d1, 0x0184, 0x48da, 0x8104, 0x4848,
	0x3182, 0x18a2, 0xf140, 0x68c0, 0x4066, 0x49e1, 0xc0e0, 0xa101,
	0x4060, 0x2888, 0x0020, 0x78c8, 0x6146, 0xa888, 0x8028, 0xc064,
	0x41c0, 0xd8d2, 0xd1f0, 0x9991, 0x41c0, 0x49c3, 0x00a4, 0x4870,
	0x5152, 0xd1f3, 0x1038, 0x59f9, 0x8128, 0x0999, 0x4178, 0x8105,
	0x9100, 0x0183, 0xa000, 0x8000, 0xa002, 0x8000, 0x0100, 0x8084,
	0x3182, 0x1989, 0x1008, 0x1181, 0xa08a, 0xa88a, 0x8100, 0x0000,
	0x0100, 0x1111, 0x0080, 0x8000, 0x0082, 0x8892, 0x8194, 0x8195,
	0x8002, 0x9193, 0x0108, 0x1111, 0x8198, 0x8094, 0x0080, 0x0080,
	0x80a2, 0xb181, 0xb000, 0x81a1, 0xa106, 0xa084, 0x2000, 0x00a0,
	0x9120, 0xb181, 0xb108, 0x1020, 0x00a4, 0x0828, 0x8024, 0x01a5,
	0x0000, 0x0101, 0x0120, 0x1131, 0x81a0, 0x89a3, 0x0010, 0x01b1,
	0x802a, 0x9191, 0x8008, 0x0909, 0x811a, 0x091b, 0x8010, 0x8014,
	0xb180, 0x8981, 0x6140, 0xd8c0, 0x6144, 0x8983, 0xc144, 0x0080,
	0x210a, 0xb888, 0x61c0, 0x8989, 0xa10c, 0x68ca, 0x808c, 0xc044,
	0x41c0, 0x0082, 0x4140, 0x4840, 0xc0d4, 0x41c1, 0x41d4, 0x0080,
	0x1110, 0x8002, 0x41c0, 0xc040, 0x8008, 0x088e, 0x011c, 0x8898,
	0x60c2, 0xd961, 0x1020, 0xc860, 0xe1c2, 0x48e0, 0x40e0, 0x4965,
	0xf1c8, 0x388a, 0x80a0, 0xf040, 0xe0cc, 0x08a8, 0x2000, 0x81a5,
	0xc140, 0x0080, 0x1090, 0x40c0, 0xc142, 0x40e2, 0x8124, 0xc1d1,
	0x517a, 0x41e3, 0xd0d8, 0x49c9, 0xc1e6, 0x891d, 0x4044, 0x0125,
};

const uint16_t kov3_key[0x100] = {
	0x9100, 0x0202, 0x0200, 0xb101, 0x2306, 0x8004, 0x0104, 0x2b01,
	0x8100, 0x3a0a, 0xa108, 0x2b09, 0x8206, 0x2b0f, 0xa004, 0x2b09,
	0x9110, 0x0b03, 0x8200, 0x1b11, 0x8306, 0x0010, 0x8304, 0x0814,
	0x9018, 0x0909, 0x1318, 0x8b09, 0x800c, 0x8a0e, 0x0310, 0x0a1c,
	0x2202, 0x0020, 0x0220, 0x8b21, 0x0120, 0x0222, 0xa100, 0x8a20,
	0x1322, 0xbb0b, 0x2000, 0xba08, 0x0326, 0x0929, 0xa00c, 0x2105,
	0x8100, 0x8800, 0x0300, 0x0800, 0x8104, 0x8222, 0x0230, 0x0220,
	0x8028, 0x0b2b, 0x9010, 0x1230, 0x8316, 0x8b1f, 0x0018, 0x0004,
	0xc140, 0x3800, 0x5340, 0x8200, 0xe040, 0x2105, 0x8004, 0x4b41,
	0xd148, 0x6343, 0xb100, 0xdb49, 0xe24a, 0x8105, 0xc248, 0x4b4d,
	0x1010, 0x0101, 0x0300, 0xd850, 0x8010, 0x8206, 0x0000, 0x0115,
	0xd050, 0x9010, 0xc340, 0x1818, 0x0004, 0x484c, 0x8110, 0xc949,
	0xe242, 0x0323, 0x1020, 0x5060, 0x8024, 0x0925, 0xa104, 0x2204,
	0xf24a, 0x2303, 0x0120, 0xe040, 0xa202, 0xa909, 0x230c, 0xaa0c,
	0x5252, 0x5070, 0x5270, 0x1210, 0x0306, 0xc155, 0x0314, 0x8224,
	0x0008, 0xc848, 0x8208, 0x1111, 0x0222, 0x090d, 0x424c, 0x881c,
	0x1100, 0xb880, 0x1000, 0x1901, 0x0104, 0xa884, 0x8084, 0x0b01,
	0x3202, 0x9181, 0x9388, 0xa181, 0xa084, 0x280c, 0x818c, 0xaa8c,
	0x8180, 0x0303, 0x9090, 0x9b91, 0x0302, 0x0a16, 0x8384, 0x0004,
	0x8088, 0x8b8b, 0x1318, 0x1311, 0x8198, 0x0a1a, 0x8284, 0x0b1d,
	0x3302, 0x0921, 0xa380, 0x0321, 0x82a6, 0x0020, 0x81a0, 0xa884,
	0x93a2, 0x9bab, 0x2108, 0xb181, 0xa28a, 0x88a8, 0xa288, 0x2204,
	0x1312, 0x88a0, 0x0220, 0x9191, 0x80b4, 0x80a0, 0x82a0, 0x8395,
	0x83aa, 0x1a1a, 0x93b8, 0x9391, 0x819c, 0x8aaa, 0x0300, 0x0220,
	0x5342, 0x9981, 0x0100, 0x3000, 0xa084, 0xc3c3, 0xc3c0, 0x6345,
	0x2302, 0x9888, 0x6148, 0x7040, 0x624a, 0x8383, 0xe0c0, 0xab8d,
	0xc3c2, 0xc0c0, 0xd2d0, 0x0b01, 0x4150, 0x4955, 0x4244, 0xc0c0,
	0x8282, 0x1b1b, 0x8180, 0xc8c8, 0x0306, 0x8387, 0x0318, 0xc2c0,
	0x1120, 0x90a0, 0x5260, 0xd8e0, 0x2306, 0xa383, 0x6044, 0x88a4,
	0x6342, 0x6040, 0x1020, 0x3b09, 0x200c, 0xa387, 0x81a4, 0x082c,
	0x0222, 0x1333, 0xd2d0, 0x0301, 0x0030, 0xc0c4, 0x4174, 0x4a74,
	0x1010, 0x4b6b, 0x1318, 0x5371, 0x023e, 0x0327, 0x0314, 0xc3d5,
};

const uint16_t kov2_key[0x100] = {
	0x1000, 0x3101, 0x9300, 0x0101, 0xa100, 0x8804, 0x8100, 0x2000,
	0x0100, 0x1101, 0xb208, 0xa101, 0x8100, 0x0b0f, 0x200c, 0x0305,
	0x0000, 0x9b13, 0x8000, 0x9911, 0x0302, 0x8804, 0x8200, 0x8a10,
	0x830a, 0x0a0a, 0x8008, 0x9b19, 0x0110, 0x0808, 0x820c, 0x0204,
	0x2202, 0x8121, 0x3200, 0xb101, 0x8120, 0xa206, 0x0120, 0x0325,
	0xb108, 0x3000, 0xb300, 0x1b29, 0xa20a, 0x8828, 0x2300, 0xa200,
	0x9030, 0x0101, 0x9230, 0x1230, 0x0014, 0x0216, 0x0114, 0x8a24,
	0x0108, 0x1919, 0x9110, 0x1b39, 0x8336, 0x8b2b, 0x830c, 0x8105,
	0x4242, 0xb000, 0xe040, 0x1901, 0xe144, 0x4246, 0x6340, 0xc945,
	0xe24a, 0x6949, 0xe148, 0x3a08, 0xe14c, 0x6a4a, 0x434c, 0xab09,
	0x0302, 0x0800, 0xd250, 0x4b41, 0xc050, 0xca56, 0x0304, 0xcb45,
	0xc342, 0x9a1a, 0x0208, 0xda58, 0x020a, 0xc95d, 0xc158, 0xc250,
	0x2100, 0x4161, 0xa000, 0x8921, 0xc366, 0x2307, 0xe344, 0xeb41,
	0x832a, 0x5969, 0xb208, 0x9321, 0x4160, 0x0b2f, 0x0020, 0x4a6c,
	0x4160, 0x9830, 0x4060, 0x9010, 0x0216, 0x8303, 0x0020, 0x4161,
	0x0222, 0x0a2a, 0xc260, 0xc060, 0x4078, 0x4357, 0xc370, 0xca68,
	0x3000, 0xa282, 0x2100, 0x0a00, 0x2206, 0xaa82, 0x8284, 0x2901,
	0x320a, 0x0303, 0x9388, 0x9381, 0x0000, 0x0000, 0x220c, 0x0808,
	0x9190, 0x1010, 0x8280, 0x0b01, 0x8286, 0x0216, 0x0314, 0x8294,
	0x828a, 0x0101, 0x8180, 0x9898, 0x8386, 0x8181, 0x0204, 0x0305,
	0x3100, 0x2800, 0x1020, 0x80a0, 0x80a4, 0x0b27, 0xa384, 0x2804,
	0x3202, 0x3101, 0x2300, 0x82a0, 0xa084, 0xa084, 0x2000, 0xa080,
	0x1110, 0x99b1, 0x1330, 0x82a0, 0x83b2, 0x0337, 0x8094, 0x8880,
	0x83a2, 0x1b3b, 0x8188, 0x1b39, 0x013c, 0x0333, 0x82b0, 0x0838,
	0x9180, 0xeac2, 0x2000, 0x1301, 0xc0c0, 0x6246, 0x2300, 0xab81,
	0x828a, 0xa989, 0x2308, 0x4b49, 0xe3c6, 0xa185, 0x434c, 0xcbcd,
	0x1010, 0xcbc3, 0xc1c0, 0xdad0, 0x0306, 0xc2d6, 0x4050, 0xcac4,
	0x1118, 0x4343, 0x0300, 0x8989, 0x8080, 0x8094, 0x020c, 0x0a1c,
	0x5060, 0x1222, 0xe2c0, 0x92a0, 0xe2c2, 0x6b47, 0xa180, 0x4264,
	0xf0c8, 0xf8c8, 0xa388, 0xab89, 0x220a, 0x0929, 0xc2e0, 0x4b69,
	0xc1c0, 0x9ab2, 0x81a0, 0x5a70, 0x4164, 0x4353, 0x8390, 0x4161,
	0xd0d0, 0x8383, 0x4260, 0xd1f1, 0x023e, 0x4a5e, 0x0230, 0xcbcd,
};

// DDPDOJ caused lots of problems to the automatic recovery process,
// so it have needed a good amount of manual examination. It is much
// more probable for this key to still contain errors than for the
// other ones.

const uint16_t ddpdoj_key[0x100] = {
	0xb102, 0x8802, 0x0000, 0x9101, 0x2004, 0xa105, 0x0000, 0x8905,
	0x8102, 0x2000, 0x3000, 0x3808, 0x210e, 0xa006, 0x800c, 0xa004,
	0x9110, 0x9911, 0x9010, 0x8800, 0x8006, 0x0901, 0x8114, 0x0115,
	0x1118, 0x1012, 0x0108, 0x8101, 0x000e, 0x8115, 0x0014, 0x0115,
	0x1020, 0x0921, 0xb100, 0xb000, 0x8126, 0x2907, 0x0020, 0xa901,
	0x1128, 0x2000, 0x2000, 0x0929, 0x2100, 0x0123, 0x8028, 0x8929,
	0x1110, 0x8802, 0x1010, 0x9810, 0x8106, 0x0000, 0x8110, 0x8020,
	0x9018, 0x1030, 0x1118, 0x9939, 0x8004, 0x081c, 0x8124, 0x093d,

	0x1102, 0xe141, 0xa000, 0x1901, 0x8102, 0x8901, 0xa004, 0x6141,
	0xe040, 0xa103, 0xf048, 0xc949, 0x8000, 0x090f, 0xe044, 0x880c,
	0xd152, 0xc840, 0x9110, 0xc941, 0x0000, 0x0117, 0x4044, 0xc054,
	0x1010, 0x181a, 0x8000, 0x5959, 0x0002, 0x485c, 0x0114, 0xc151,
	0x5060, 0xc862, 0x1020, 0x7040, 0xc062, 0x6042, 0x8024, 0x2101,
	0xc162, 0x596b, 0xe140, 0x9020, 0xc162, 0x694d, 0x2104, 0xa90d,
	0x1032, 0xd872, 0x5170, 0x1010, 0x4156, 0xc951, 0x4044, 0x4961,
	0x9030, 0xd85a, 0x8020, 0x1939, 0x011e, 0x0012, 0x8000, 0xc151,

	0x3102, 0x9880, 0xa080, 0xb181, 0xa182, 0x8084, 0x0000, 0xa981,
	0x1102, 0x2000, 0x8180, 0x1808, 0x8180, 0x290d, 0xa084, 0x8084,
	0x9092, 0x8981, 0x0100, 0x0901, 0x0004, 0x8884, 0x8094, 0x8991,
	0x0100, 0x989a, 0x1110, 0x0909, 0x8192, 0x889e, 0x0000, 0x898d,
	0xb080, 0x1022, 0xb080, 0x3000, 0x2106, 0x2101, 0x80a4, 0x89a1,
	0x81a2, 0x1022, 0xb088, 0x3101, 0x2002, 0x2107, 0x81a8, 0x0024,
	0x8080, 0x9991, 0x0120, 0x0020, 0x8090, 0x0101, 0x8084, 0x0911,
	0x8082, 0x99b9, 0x9190, 0x1010, 0x81b6, 0x89ab, 0x81ac, 0x8989,

	0xf1c0, 0x3901, 0xa080, 0x7141, 0x0104, 0xa985, 0xe0c4, 0x2800,
	0x310a, 0x280a, 0x7040, 0x1101, 0x210e, 0xe8ca, 0x6040, 0x694d,
	0x9090, 0x8082, 0x8180, 0xd8d0, 0xc1d2, 0x8197, 0x0114, 0x8181,
	0xd0d8, 0xc8c8, 0x0108, 0xc1c1, 0xc1c2, 0x0113, 0x819c, 0xc9cd,
	0x91a0, 0x1121, 0xf0c0, 0x4161, 0xe0c6, 0x2006, 0x4060, 0xa880,
	0x5068, 0x7040, 0x2100, 0x3808, 0x210e, 0x81a3, 0x416c, 0x0121,
	0xd1d0, 0x9193, 0xc1e0, 0xc9c1, 0x0014, 0x0830, 0x4050, 0x4961,
	0x103a, 0xc9e9, 0x1118, 0x1111, 0x404e, 0x8898, 0x0024, 0x0020,
};

// WRONG

const uint16_t kof98umh_key[0x100] = {
	0x9202, 0x0000, 0x0220, 0x3b01, 0x2004, 0x00a0, 0x0000, 0x0905,
	0x830a, 0x8000, 0x8228, 0xa20a, 0x8206, 0x0000, 0x0020, 0x8808,
	0x9212, 0x0800, 0x0000, 0x0301, 0x0020, 0x0a02, 0x0000, 0x0105,
	0x020a, 0x0000, 0x0000, 0x1111, 0x021a, 0x0121, 0x0000, 0x8111,
	0x8020, 0x0802, 0xb120, 0xb901, 0x8326, 0x0800, 0x0000, 0xa101,
	0x922a, 0x0000, 0x1200, 0x9022, 0x0128, 0x0800, 0x0020, 0xaa0c,
	0x0202, 0x0800, 0x8000, 0x1b31, 0x8024, 0x8804, 0x0000, 0x8111,
	0x8120, 0x8000, 0x0080, 0x9030, 0x032e, 0x083c, 0x0000, 0x893d,
	0xe342, 0x0800, 0x0120, 0x7b41, 0xa000, 0x00a0, 0x0000, 0x4141,
	0x130a, 0x0000, 0x8320, 0xdb49, 0x010c, 0x0000, 0x0020, 0xc84c,
	0x0202, 0x0000, 0x0000, 0x1911, 0x0014, 0x0b13, 0x0000, 0x8a10,
	0x434a, 0x0020, 0x0000, 0x5250, 0xc050, 0x0034, 0x0000, 0xc355,
	0x7242, 0x0002, 0x9000, 0xcb61, 0x8120, 0x0020, 0x0000, 0x0921,
	0x4068, 0x0000, 0x6068, 0x922a, 0xc36e, 0x0020, 0x0020, 0x280c,
	0x0322, 0x0800, 0x8020, 0x9810, 0xc060, 0x4b73, 0x0000, 0x4870,
	0xd050, 0x0020, 0x0000, 0x1b39, 0xc26e, 0x0121, 0x0000, 0x4375,
	0x2202, 0x0000, 0x0120, 0xb0a0, 0x2202, 0x2020, 0x2000, 0x8280,
	0x838a, 0x0080, 0x81a0, 0xb082, 0x2004, 0x0000, 0x0020, 0xa385,
	0x8080, 0x0080, 0x0000, 0x9191, 0x80a0, 0x0004, 0x0000, 0x8280,
	0x9098, 0x0020, 0x0080, 0x0808, 0x030e, 0x0337, 0x0000, 0x8290,
	0xb382, 0x0002, 0x0000, 0x1220, 0x2004, 0x0000, 0x0080, 0x2301,
	0x81a8, 0x0000, 0x0228, 0x1121, 0x230a, 0x2000, 0x0000, 0x80a0,
	0x1232, 0x0020, 0x0020, 0x9b91, 0x0114, 0x8185, 0x0080, 0x0010,
	0x82aa, 0x0000, 0x0080, 0x98b8, 0x0226, 0x0a2e, 0x0000, 0x80b4,
	0x1100, 0x0000, 0xa1a0, 0xf2e0, 0x6144, 0x0020, 0x0000, 0x4145,
	0x1108, 0x0000, 0xb280, 0x9a8a, 0xa084, 0x0000, 0x0020, 0x4044,
	0x9190, 0x0000, 0x0000, 0x0a00, 0x4266, 0x0814, 0x0080, 0x8b91,
	0x434a, 0x00a0, 0x0080, 0x0808, 0x4352, 0x83a3, 0x0000, 0xc3c5,
	0x3302, 0x0002, 0xd1c0, 0xf3c1, 0xa080, 0x00a0, 0x0000, 0x0b25,
	0x0128, 0x0000, 0xb288, 0x3b09, 0x200c, 0x2000, 0x0020, 0xeac8,
	0xd1f0, 0x0000, 0x0020, 0x83a1, 0xc3f2, 0xc8d0, 0x0000, 0x8b91,
	0x5272, 0x0080, 0x0000, 0x0b29, 0x4376, 0x0828, 0x0000, 0x8ab8,
};
