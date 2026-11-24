// BENCH-07 scene_v2: ertelenmis aydinlatma. Gecisler:
//   1) G-buffer: ornekli kupler + zemin; prosedurel gurultu dokusundan triplanar fBm (doku ornekleme)
//   2) Aydinlatma: tam ekran; piksel basina tier'a gore N nokta isik (Blinn-Phong + zayiflama) + gunes.
//      En cok 512 isiklik cizimlere bolunur, toplamsal karisim (tek cizim TDR suresine yaklasmaz).
//   3) Bloom: parlak kisim + ayrik Gauss bulaniklik (yatay, dikey)
//   4) Tonemap: ACES yaklasimi + gamma -> RGBA8
// Derleme zamani fxc ile gomulur (calisma aninda derleyici yok). Tum dongu sinirlari sabit tampondan gelir;
// optimizer isi kaldiramaz (sonuc renge girer).

cbuffer Frame : register(b0) {
    float4x4 view_proj;
    float4 eye_frame;  // xyz kamera, w kare indeksi
    float4 sun_dir;    // xyz isik yonu (normalize), w gunes siddeti
    uint4 counts;      // x isik sayisi, y bulaniklik yaricapi
    float4 misc;       // x isik siddet olcegi, y bloom esigi, z pozlama, w 1/yukseklik
};

cbuffer PassParams : register(b1) {
    int2 blur_dir;    // (1,0) yatay, (0,1) dikey
    int2 pass_flags;  // x: 1 = parlak kisim cikar (ilk gecis)
};

// ---------------- 1) G-buffer ----------------
struct VsIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float4 inst_pos_scale : INST0;  // xyz konum, w olcek
    float4 inst_color : INST1;
    float inst_spin : INST2;
};

struct VsOut {
    float4 pos : SV_Position;
    float3 wpos : WPOS;
    float3 nrm : NORMAL;
    float4 color : COLOR;
};

VsOut vs_main(VsIn i) {
    const float a = i.inst_spin * eye_frame.w;
    const float c = cos(a), s = sin(a);
    const float3 p = float3(i.pos.x * c - i.pos.z * s, i.pos.y, i.pos.x * s + i.pos.z * c);
    const float3 n = float3(i.nrm.x * c - i.nrm.z * s, i.nrm.y, i.nrm.x * s + i.nrm.z * c);
    const float3 w = p * i.inst_pos_scale.w + i.inst_pos_scale.xyz;
    VsOut o;
    o.pos = mul(float4(w, 1), view_proj);
    o.wpos = w;
    o.nrm = n;
    o.color = i.inst_color;
    return o;
}

Texture2D<float4> noise_tex : register(t0);
SamplerState wrap_smp : register(s0);

struct GOut {
    float4 albedo : SV_Target0;
    float4 nrm : SV_Target1;
    float4 wpos : SV_Target2;
};

float3 triplanar(float3 p, float3 w) {
    return noise_tex.Sample(wrap_smp, p.yz).rgb * w.x + noise_tex.Sample(wrap_smp, p.zx).rgb * w.y +
           noise_tex.Sample(wrap_smp, p.xy).rgb * w.z;
}

GOut ps_gbuffer(VsOut i) {
    const float3 n = normalize(i.nrm);
    float3 w = abs(n);
    w /= max(w.x + w.y + w.z, 1e-4);
    float3 fbm = 0;
    float amp = 0.5, freq = 0.05;
    [unroll] for (int o = 0; o < 4; ++o) {
        fbm += amp * triplanar(i.wpos * freq, w);
        amp *= 0.5;
        freq *= 2.07;
    }
    GOut g;
    g.albedo = float4(i.color.rgb * (0.45 + 0.7 * fbm), 1);
    g.nrm = float4(normalize(n + (fbm - 0.47) * 0.6), 8.0 + 56.0 * fbm.g);  // w: parlaklik ussu
    g.wpos = float4(i.wpos, 1);                                             // w = 1: geometri var
    return g;
}

// ---------------- Tam ekran ucgen ----------------
float4 vs_fullscreen(uint id : SV_VertexID) : SV_Position {
    const float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}

// ---------------- 2) Aydinlatma ----------------
struct PointLight {
    float4 pos_inv_r2;  // xyz konum, w = 1/r^2
    float4 color;
};
Texture2D<float4> g_albedo : register(t0);
Texture2D<float4> g_normal : register(t1);
Texture2D<float4> g_wpos : register(t2);
StructuredBuffer<PointLight> lights : register(t3);

// TDR guvenligi: isik dongusu parcalara bolunur (parca basina en cok kLightsPerDraw = 512 isik).
// Taban parca (z = 1) gok, ortam, gunes ve sis rengini yazar (karisim yok); diger parcalar yalniz
// kendi isik katkilarini toplamsal karisimla ekler. Sis dogrusal oldugundan
// lerp(col, sis, f) = col * (1 - f) + sis * f parcalara bolunebilir.
cbuffer LightChunk : register(b2) {
    uint4 chunk;  // x ilk isik, y parca sonu (ilk + 512), z: 1 = taban parca
};

float4 ps_light(float4 sv : SV_Position) : SV_Target {
    const int3 px = int3(sv.xy, 0);
    const float4 wp = g_wpos.Load(px);
    const bool base = chunk.z != 0;
    if (wp.w == 0) {
        if (!base) return float4(0, 0, 0, 0);
        const float h = saturate(sv.y * misc.w);
        return float4(lerp(float3(0.35, 0.45, 0.65), float3(0.05, 0.06, 0.10), h), 1);  // gok
    }
    const float3 alb = g_albedo.Load(px).rgb;
    const float4 nn = g_normal.Load(px);
    const float3 n = normalize(nn.xyz);
    const float3 v = normalize(eye_frame.xyz - wp.xyz);
    float3 col = base ? alb * (0.06 + sun_dir.w * saturate(dot(n, -sun_dir.xyz))) : float3(0, 0, 0);
    float3 acc = 0;
    const uint end = min(chunk.y, counts.x);
    [loop] for (uint k = chunk.x; k < end; ++k) {
        const PointLight L = lights[k];
        const float3 d = L.pos_inv_r2.xyz - wp.xyz;
        const float d2 = max(dot(d, d), 1e-4);
        const float3 l = d * rsqrt(d2);
        float att = saturate(1 - d2 * L.pos_inv_r2.w);
        att *= att;
        const float ndl = saturate(dot(n, l));
        const float3 h = normalize(l + v);
        const float spec = pow(saturate(dot(n, h)), nn.w);
        acc += L.color.rgb * att * (alb * ndl + spec * 0.25);
    }
    col += acc * misc.x;
    // Mesafe sisi
    const float fog = saturate(length(eye_frame.xyz - wp.xyz) / 420.0);
    const float f2 = fog * fog;
    const float3 fog_part = base ? float3(0.20, 0.24, 0.32) * f2 : float3(0, 0, 0);
    return float4(col * (1 - f2) + fog_part, base ? 1 : 0);
}

// ---------------- 3) Bloom (ayrik Gauss) ----------------
Texture2D<float4> src_tex : register(t0);

float4 ps_blur(float4 sv : SV_Position) : SV_Target {
    uint w, h;
    src_tex.GetDimensions(w, h);
    const int2 hi = int2(w, h) - 1;
    const int r = (int)counts.y;
    const float inv2s2 = 1.0 / (2.0 * max(r * 0.5, 1.0) * max(r * 0.5, 1.0));
    float3 acc = 0;
    float wsum = 0;
    [loop] for (int k = -r; k <= r; ++k) {
        const int2 q = clamp(int2(sv.xy) + blur_dir * k, int2(0, 0), hi);
        float3 c = src_tex.Load(int3(q, 0)).rgb;
        if (pass_flags.x != 0) c = max(c - misc.y, 0);
        const float wk = exp(-(float)(k * k) * inv2s2);
        acc += c * wk;
        wsum += wk;
    }
    return float4(acc / wsum, 1);
}

// ---------------- 4) Tonemap ----------------
Texture2D<float4> hdr_tex : register(t0);
Texture2D<float4> bloom_tex : register(t1);

float4 ps_tonemap(float4 sv : SV_Position) : SV_Target {
    const int3 px = int3(sv.xy, 0);
    float3 c = (hdr_tex.Load(px).rgb + 0.6 * bloom_tex.Load(px).rgb) * misc.z;
    c = saturate((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14));
    return float4(pow(c, 1.0 / 2.2), 1);
}

// ---------------- 5) Canli onizleme (spec 2026-11-12 A4) ----------------
// LDR hedefinden onizleme boyutuna kucultme. Eksen basina N tap (1..4), her tap bilineer (2x2 texel): 4K -> 960x540
// tam kutu suzgeci, 1080p -> 960x540 tek tap. Cikti RGBA8 hedefe BGRA sirasiyla yazilir (bellekte B, G, R, A;
// alfa 1 -> onceden carpilmis = duz). Olcum hedefini degistirmez.
Texture2D<float4> preview_src : register(t0);
SamplerState linear_clamp : register(s1);

cbuffer PreviewParams : register(b5) {
    float4 preview_inv;  // xy: 1 / onizleme boyutu, z: eksen basina tap sayisi
};

float4 ps_preview(float4 sv : SV_Position) : SV_Target {
    const int n = (int)preview_inv.z;
    const float2 corner = (sv.xy - 0.5) * preview_inv.xy;  // pikselin sol ust kosesi (uv)
    const float2 cell = preview_inv.xy / (float)n;
    float3 acc = 0;
    [loop] for (int y = 0; y < n; ++y) {
        [loop] for (int x = 0; x < n; ++x) {
            const float2 uv = corner + (float2(x, y) + 0.5) * cell;
            acc += preview_src.SampleLevel(linear_clamp, uv, 0).rgb;
        }
    }
    acc /= (float)(n * n);
    return float4(acc.b, acc.g, acc.r, 1);
}
