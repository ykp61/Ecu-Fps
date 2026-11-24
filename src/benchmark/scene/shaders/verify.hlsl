// EF-X22: deterministik GPU hesap dogrulama cekirdegi. CPU kopyasi: gpu_verify_rules.cpp reference_word
// (iki taraf birlikte degismeli; kVerifyVersion artirilir). Float ara degerleri tamsayi ve < 2^24: tam.

cbuffer VerifyParams : register(b0)
{
    uint g_words;
    uint g_seed;
    uint g_iterations;
    uint g_pad;
};

RWStructuredBuffer<uint> g_out : register(u0);

uint verify_word(uint index, uint seed, uint iterations)
{
    uint x = (index * 0x9E3779B9u) ^ seed;
    if (x == 0u) x = 0x6D2B79F5u;
    uint acc = index;
    [loop] for (uint k = 0u; k < iterations; ++k)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        float a = (float)(x & 0xFFFu);
        float b = (float)((x >> 12) & 0xFFFu);
        float p = a * b + a;
        acc = acc * 1664525u + ((uint)p ^ k);
    }
    return acc ^ x;
}

[numthreads(64, 1, 1)]
void cs_verify(uint3 id : SV_DispatchThreadID)
{
    if (id.x < g_words) g_out[id.x] = verify_word(id.x, g_seed, g_iterations);
}
