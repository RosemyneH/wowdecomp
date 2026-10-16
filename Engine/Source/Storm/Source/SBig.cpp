#include <cstdint>
#include <cstring>
#include <new>
#include "SBig.h"
#include "Engine/Source/Storm/H/storm.h"

// INLINED
inline uint32_t ExtractLowPart(uint64_t& value) {
    uint32_t low = static_cast<uint32_t>(value);
    value >>= 32;
    return low;
}

// INLINED
inline uint32_t ExtractLowPartSx(uint64_t& value) {
    uint32_t low = static_cast<uint32_t>(value);
    value >>= 32;

    if (value >= 0x80000000) {
        value |= 0xFFFFFFFF00000000ULL;
    }

    return low;
}

// INLINED
inline uint32_t ExtractLowPartLargeSum(uint64_t& value, uint64_t add) {
    value += add;
    uint32_t carry = value < add;
    uint32_t low = ExtractLowPart(value);
    value += static_cast<uint64_t>(carry) << 32;
    return low;
}

// INLINED
inline void InsertLowPart(uint64_t& value, uint32_t low) {
    value = (value << 32) + low;
}

// INLINED
inline uint64_t MakeLarge(uint32_t low, uint32_t high) {
    return low + (static_cast<uint64_t>(high) << 32);
}

// VERIFY: 336fa318 auto
// OFFSET: 0x0077ACF0
uint32_t BigBuffer::operator[](uint32_t index) const {
    return this->IsUsed(index) ? this->m_data[index + this->m_offset] : 0;
}

// VERIFY: 0e1a40be manual
// OFFSET: 0x0077AD10
static int32_t Compare(const BigBuffer& a, const BigBuffer& b) {
    int32_t result = 0;

    for (uint32_t i = 0; a.IsUsed(i) || b.IsUsed(i); ++i) {
        if (a[i] != b[i]) {
            result = b[i] < a[i] ? 1 : -1;
        }
    }

    return result;
}

// VERIFY: c8057a1f manual
// OFFSET: 0x0077ADD0
static uint32_t HighBitPos(const BigBuffer& a) {
    uint32_t index = a.Count();

    while (index) {
        --index;

        if (a[index]) {
            uint32_t mask = 0x80000000;

            for (uint32_t bit = 32; bit; mask >>= 1) {
                --bit;

                if (a[index] & mask) {
                    return bit + 32 * index;
                }
            }
        }
    }

    return 0;
}

// VERIFY: 651dfc15 auto
// OFFSET: 0x0077AE60
int32_t STORMAPI SBigCompare(const BigData* a, const BigData* b) {
    return Compare(a->Primary(), b->Primary());
}

// VERIFY: 11441f7a manual
// OFFSET: 0x0077AE80
BigStack::~BigStack() {
}

// VERIFY: f5cce634 manual
// OFFSET: 0x0077AEC0
void BigBuffer::GrowToFit(uint32_t index) {
    this->m_data.GrowToFit(index + this->m_offset, 1);
}

// VERIFY: 682c9785 auto
// OFFSET: 0x0077AF30
void BigBuffer::SetCount(uint32_t count) {
    this->m_data.SetCount(count + this->m_offset);
}

// VERIFY: ede94cab manual
// OFFSET: 0x0077AF80
void BigBuffer::Trim() const {
    while (this->Count() && !*this->m_data.Top()) {
        this->m_data.SetCount(this->m_data.Count() - 1);
    }
}

// VERIFY: d5065bc4 manual
// OFFSET: 0x0077AFF0
BigStack::BigStack() {
    this->m_used = 0;
}

// VERIFY: 5ff3c6ab manual
// OFFSET: 0x0077B020
static void Add(BigBuffer& a, const BigBuffer& b, const BigBuffer& c) {
    uint64_t carry = 0;
    uint32_t i = 0;

    for (; carry || b.IsUsed(i) || c.IsUsed(i); ++i) {
        carry += static_cast<uint64_t>(b[i]) + c[i];
        a[i] = ExtractLowPart(carry);
    }

    a.SetCount(i);
}

// VERIFY: 22a6a2d4 manual
// OFFSET: 0x0077B160
static void Div(BigBuffer& a, uint32_t* b, const BigBuffer& c, uint64_t d) {
    a.SetCount(c.Count());

    uint64_t data = 0;
    uint32_t i = c.Count();

    while (i) {
        --i;
        InsertLowPart(data, c[i]);
        a[i] = static_cast<uint32_t>(data / d);
        data %= d;
    }

    a.Trim();
    *b = static_cast<uint32_t>(data);
}

// VERIFY: 897eab1f manual
// OFFSET: 0x0077B2B0
static void Mul(BigBuffer& a, const BigBuffer& b, uint64_t c) {
    uint64_t carry = 0;
    uint32_t i = 0;

    for (; carry || b.IsUsed(i); ++i) {
        carry += b[i] * c;
        a[i] = ExtractLowPart(carry);
    }

    a.SetCount(i);
}

// VERIFY: 830bd17e manual
// OFFSET: 0x0077B3E0
static void Sub(BigBuffer& a, const BigBuffer& b, const BigBuffer& c) {
    uint64_t borrow = 0;
    uint32_t i = 0;

    for (; b.IsUsed(i) || c.IsUsed(i); ++i) {
        borrow += b[i] - static_cast<uint64_t>(c[i]);
        a[i] = ExtractLowPartSx(borrow);
    }

    a.SetCount(i);
}

// INLINED
inline void SetZero(BigBuffer& a) {
    a.Clear();
}

// VERIFY: 6eea81e3 manual
// OFFSET: 0x0077B530
static void SetOne(BigBuffer& a) {
    a.SetCount(1);
    a[0] = 1;
}

// VERIFY: e9adca71 manual
// OFFSET: 0x0077B590
static void Shl(BigBuffer& a, const BigBuffer& b, uint32_t c) {
    uint32_t cWords = c >> 5;
    uint32_t cBits = c & 0x1F;
    uint32_t aCount = b.Count() + cWords + 1;
    uint32_t aIndex = aCount;

    while (aIndex) {
        --aIndex;
        uint32_t value = 0;

        if (aIndex >= cWords) {
            value = b[aIndex - cWords] << cBits;

            if (aIndex > cWords) {
                if (cBits) {
                    value += b[aIndex - cWords - 1] >> (32 - cBits);
                }
            }
        }

        a[aIndex] = value;
    }

    a.SetCount(aCount);
    a.Trim();
}

// VERIFY: 47db10a2 manual
// OFFSET: 0x0077B700
static void Shr(BigBuffer& a, const BigBuffer& b, uint32_t c) {
    uint32_t cWords = c >> 5;
    uint32_t cBits = c & 0x1F;
    uint32_t i = 0;

    for (; b.IsUsed(i + cWords); ++i) {
        uint32_t value = b[i + cWords] >> cBits;

        if (cBits) {
            value += b[i + cWords + 1] << (32 - cBits);
        }

        a[i] = value;
    }

    a.SetCount(i);
}

// VERIFY: 11b22bb4 manual
// OFFSET: 0x0077B7F0
static void FromBinary(BigBuffer& a, const void* data, uint32_t bytes) {
    a.Clear();

    for (uint32_t i = 0; i < bytes; ++i) {
        uint32_t byte = static_cast<const uint8_t*>(data)[i];
        a[i >> 2] = ((i & 3) ? a[i >> 2] : 0) + (byte << (8 * (i & 3)));
    }
}

// VERIFY: fff4feab manual
// OFFSET: 0x0077B8B0
static void FromUnsigned(BigBuffer& a, uint32_t value) {
    a[0] = value;
    a.SetCount(1);
}

// VERIFY: d83c0bf3 manual
// OFFSET: 0x0077B910
static void ToBinaryAppend(TSGrowableArray<uint8_t>& output, const BigBuffer& a) {
    for (uint32_t i = 0; i < 4 * a.Count(); ++i) {
        uint32_t value = a[i >> 2] >> (8 * (i & 3));

        if (value || (i >> 2) + 1 < a.Count()) {
            *output.New() = static_cast<uint8_t>(value);
        }
    }
}

// INLINED
inline void ToBinary(TSGrowableArray<uint8_t>& output, const BigBuffer& a) {
    output.SetCount(0);
    ToBinaryAppend(output, a);
}

// VERIFY: 87a135f6 auto
// OFFSET: 0x0077B9E0
void STORMAPI SBigAdd(BigData* a, const BigData* b, const BigData* c) {
    Add(a->Primary(), b->Primary(), c->Primary());
}

// VERIFY: fb1a64d7 manual
// OFFSET: 0x0077BA00
void STORMAPI SBigBitLen(BigData* a, uint32_t* bits) {
    BigBuffer& buffer = a->Primary();
    buffer.Trim();

    uint32_t index = buffer.Count() - 1;
    uint32_t& high = buffer[index];
    uint32_t bit = 31;

    do {
        if ((1 << bit) & high) {
            break;
        }

        --bit;
    } while (bit);

    *bits = 32 * index + bit + 1;
}

// VERIFY: aa2a87ff auto
// OFFSET: 0x0077BA60
void STORMAPI SBigFromBinary(BigData* num, const void* data, uint32_t bytes) {
    FromBinary(num->Primary(), data, bytes);
}

// VERIFY: 44b810ee auto
// OFFSET: 0x0077BA80
void STORMAPI SBigFromUnsigned(BigData* num, uint32_t val) {
    FromUnsigned(num->Primary(), val);
}

// VERIFY: 24073771 auto
// OFFSET: 0x0077BAA0
void STORMAPI SBigSub(BigData* a, const BigData* b, const BigData* c) {
    Sub(a->Primary(), b->Primary(), c->Primary());
}

// VERIFY: 36845b67 manual
// OFFSET: 0x0077BAC0
static void Div(BigBuffer& a, BigBuffer& b, const BigBuffer& c, const BigBuffer& d, BigStack& stack) {
    c.Trim();
    d.Trim();

    uint32_t cCount = c.Count();
    uint32_t dCount = d.Count();

    if (dCount > cCount) {
        b = c;
        SetZero(a);
        return;
    }

    if (dCount <= 1) {
        Div(a, &b[0], c, d[0]);
        b.SetCount(1);
        return;
    }

    uint32_t allocCount = 0;
    BigBuffer& cc = stack.Alloc(&allocCount);
    BigBuffer& dd = stack.Alloc(&allocCount);
    BigBuffer& work = stack.Alloc(&allocCount);

    uint32_t shift = 31 - (HighBitPos(d) & 0x1F);
    Shl(cc, c, shift);
    Shl(dd, d, shift);

    uint32_t t = dd[dCount - 1] + 1;
    uint32_t index = cCount - dCount + 1;
    a.SetCount(index);

    while (index) {
        --index;
        a.SetOffset(index);
        cc.SetOffset(index);

        if (t) {
            a[0] = static_cast<uint32_t>(MakeLarge(cc[dCount - 1], cc[dCount]) / t);
        } else {
            a[0] = cc[dCount];
        }

        if (a[0]) {
            Mul(work, dd, a[0]);
            Sub(cc, cc, work);
        }

        while (cc[dCount] || Compare(cc, dd) >= 0) {
            ++a[0];
            Sub(cc, cc, dd);
        }
    }

    Shr(b, cc, shift);
    b.Trim();
    stack.Free(allocCount);
}

// VERIFY: 1e6799f2 manual
// OFFSET: 0x0077BEE0
void STORMAPI SBigMod(BigData* a, const BigData* b, const BigData* c) {
    uint32_t allocCount = 0;
    BigBuffer& scratch = a->Stack().Alloc(&allocCount);
    Div(scratch, a->Primary(), b->Primary(), c->Primary(), a->Stack());
    a->Stack().Free(allocCount);
}

// VERIFY: 028bbc8d manual
// OFFSET: 0x0077BF20
void STORMAPI SBigNew(BigData** num) {
    void* mem = SMemAlloc(sizeof(BigData), __FILE__, __LINE__, 0);

    if (!mem) {
        *num = nullptr;
        return;
    }

    *num = new (mem) BigData;
}

// VERIFY: 79d5340d manual
// OFFSET: 0x0077BF90
void STORMAPI SBigToBinaryBuffer(const BigData* num, void* data, uint32_t maxBytes, uint32_t* bytes) {
    TSGrowableArray<uint8_t>& output = num->Output();
    ToBinary(output, num->Primary());

    uint32_t count = maxBytes;

    if (output.Count() < maxBytes) {
        count = output.Count();
    }

    memcpy(data, output.Ptr(), count);

    if (bytes) {
        *bytes = count;
    }
}

// VERIFY: 9ea7a3ce manual
// OFFSET: 0x0077BFF0
static void Mul(BigBuffer& a, const BigBuffer& b, const BigBuffer& c, BigStack& stack) {
    BigBuffer& aa = stack.MakeDistinct(a, &a == &b || &a == &c);
    aa.Clear();

    for (uint32_t bIndex = 0; b.IsUsed(bIndex); ++bIndex) {
        uint64_t carry = 0;
        uint32_t cIndex = 0;

        for (; c.IsUsed(cIndex); ++cIndex) {
            carry += aa[bIndex + cIndex] + static_cast<uint64_t>(b[bIndex]) * c[cIndex];
            aa[bIndex + cIndex] = ExtractLowPart(carry);
        }

        aa[bIndex + cIndex] = ExtractLowPart(carry);
    }

    stack.UnmakeDistinct(a, aa);
}

// VERIFY: b51dbf98 manual
// OFFSET: 0x0077C330
static void Square(BigBuffer& a, const BigBuffer& b, BigStack& stack) {
    BigBuffer& aa = stack.MakeDistinct(a, &a == &b);
    aa.Clear();

    for (uint32_t i = 0; b.IsUsed(i); ++i) {
        uint64_t carry = 0;
        uint32_t j = 0;

        for (; j <= i; ++j) {
            uint64_t mul = static_cast<uint64_t>(b[i]) * b[j];
            uint64_t add = mul + aa[i + j];

            if (j < i) {
                carry += mul;
            }

            aa[i + j] = ExtractLowPartLargeSum(carry, add);
        }

        aa[i + j] = ExtractLowPart(carry);
    }

    stack.UnmakeDistinct(a, aa);
}

// VERIFY: 93c23957 manual
// OFFSET: 0x0077C6A0
void STORMAPI SBigDel(BigData* num) {
    delete num;
}

// VERIFY: 730b3f70 auto
// OFFSET: 0x0077C700
void STORMAPI SBigMul(BigData* a, const BigData* b, const BigData* c) {
    Mul(a->Primary(), b->Primary(), c->Primary(), a->Stack());
}

// INLINED
inline void MulMod(BigBuffer& a, const BigBuffer& b, const BigBuffer& c, const BigBuffer& d, BigStack& stack) {
    uint32_t allocCount = 0;
    BigBuffer& temp = stack.Alloc(&allocCount);
    Mul(temp, b, c, stack);
    Div(temp, a, temp, d, stack);
    stack.Free(allocCount);
}

// VERIFY: 1a9c7b91 manual
// OFFSET: 0x0077C720
static void PowMod(BigBuffer& a, const BigBuffer& b, const BigBuffer& c, const BigBuffer& d, BigStack& stack) {
    c.Trim();

    if (!c.Count()) {
        SetOne(a);
        return;
    }

    uint32_t allocCount = 0;
    BigBuffer& temp = stack.Alloc(&allocCount);
    BigBuffer& b2 = stack.Alloc(&allocCount);
    BigBuffer& b3 = stack.Alloc(&allocCount);
    BigBuffer& aa = stack.MakeDistinct(a, &a == &b || &a == &c || &a == &d);

    MulMod(b2, b, b, d, stack);
    MulMod(b3, b2, b, d, stack);

    const BigBuffer* bPower[3] = { &b, &b2, &b3 };
    SetOne(aa);

    uint32_t cIndex = c.Count();

    while (cIndex) {
        --cIndex;
        uint32_t ci = c[cIndex];
        uint32_t ciBits = 32;

        if (cIndex + 1 == c.Count()) {
            while (!(ci & 0xC0000000)) {
                ci <<= 2;
                ciBits -= 2;
            }
        }

        for (uint32_t bit = 0; bit < ciBits; bit += 2) {
            Square(aa, aa, stack);
            Div(temp, aa, aa, d, stack);
            Square(aa, aa, stack);
            Div(temp, aa, aa, d, stack);

            if (ci >> 30) {
                MulMod(aa, aa, *bPower[(ci >> 30) - 1], d, stack);
            }

            ci <<= 2;
        }
    }

    stack.UnmakeDistinct(a, aa);
    stack.Free(allocCount);
}

// VERIFY: 07b073f1 auto
// OFFSET: 0x0077C980
void STORMAPI SBigPowMod(BigData* a, const BigData* b, const BigData* c, const BigData* d) {
    PowMod(a->Primary(), b->Primary(), c->Primary(), d->Primary(), a->Stack());
}
