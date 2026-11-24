// Denge testi "Arena v1" (spec 2026-11-12 A1). scene.hlsl ile ayni ertelenmis boru hatti ve hedef kumesi:
//   1) G-buffer: cift ok prizmalari (ornekli YA DA nesne basina sabit tamponla) + her pikseli kaplayan tunel
//   2) Aydinlatma: tam ekran; piksel basina N nokta isik (parcalara bolunur; parca boyu piksel sayisiyla kuculur)
//   3) Bloom ve 4) tonemap: scene.hlsl'deki ps_blur / ps_tonemap aynen kullanilir (bu dosyada yok)
// Kamera orijinde, +z yonune bakar; tum konumlar kameraya goredir (buyuk dunya koordinati yok).
// Derleme zamani fxc ile gomulur. Dongu sinirlari sabit tampondan gelir; sonuc renge girer.

cbuffer Frame : register(b0) {  // scene.hlsl `Frame` ile ayni yerlesim (ps_blur / ps_tonemap ortak)
    float4x4 view_proj;
    float4 eye_frame;  // Arena'da kullanilmaz (kamera orijinde)
    float4 sun_dir;    // xyz dolgu isigi yonu (normalize), w siddeti: yuzeyler isik havuzlari disinda da okunur
    uint4 counts;      // x isik sayisi, y bulaniklik yaricapi
    float4 misc;       // x isik siddet olcegi, y bloom esigi, z pozlama, w 1/yukseklik
};

cbuffer LightChunk : register(b2) {
    uint4 chunk;  // x ilk isik, y parca sonu, z: 1 = taban parca
};

cbuffer ArenaFrame : register(b3) {
    uint4 arena_u;      // x kare indeksi, y akis sarma maskesi (Q16.16; 0xFFFFFFFF = sarma yok), z yakin ayiklama (Q16.16)
    float4 arena_f;     // x kamera z (tunel dongusunde), y kamera z (isik periyodunda), z tunel yari kenari, w sis / uc mesafesi
    float4 arena_g;     // x isik periyodu, y isik geri payi, z isima kazanci, w tunel panel adimi
    float4 arena_wall;  // rgb tunel yuzeyi (dogrusal)
    float4 arena_line;  // rgb panel cizgisi (dogrusal), a cizgi isimasi
    float4 arena_fog;   // rgb sis / uzak renk (dogrusal), a nesne isimasi
};

cbuffer Object : register(b4) {  // PerObject yolu: nesne basina Map(WRITE_DISCARD)
    float4 obj_pos_scale;   // x, y, -, olcek
    float4 obj_dir_roll;    // yon xyz, donus fazi
    float4 obj_color_rate;  // rgb, donus hizi (rad / kare)
    uint4 obj_zq;           // x kameraya gore z (Q16.16), y kare basi akis hizi (Q16.16)
};

struct VsOut {
    float4 pos : SV_Position;
    float3 wpos : WPOS;    // kameraya gore konum
    float3 nrm : NORMAL;
    float4 color : COLOR;  // rgb albedo, a: 1 = nesne, 0 = tunel
};

// ---------------- 1) G-buffer: nesne ----------------
// Ag ekseni: x = ok yonu (hiz yonune), y = yukari, z = kalinlik. Yon ekseni cevresinde kare indeksiyle doner.
// z tamsayi aritmetigiyle ilerler: (z0 + kare * hiz) & maske; sim acikken hiz = 0 ve maske = 0xFFFFFFFF.
VsOut arena_object(float3 pos, float3 nrm, float4 pos_scale, float4 dir_roll, float4 color_rate, uint4 zq) {
    const uint zi = (zq.x + arena_u.x * zq.y) & arena_u.y;
    const float3 center = float3(pos_scale.xy, (float)zi * (1.0 / 65536.0));
    const float3 f = normalize(dir_roll.xyz);
    const float3 r0 = normalize(float3(-f.z, 0, f.x) + float3(0, 0, 1e-6));  // cross(f, yukari)
    const float3 u0 = cross(r0, f);
    const float a = dir_roll.w + color_rate.w * (float)arena_u.x;
    const float c = cos(a), s = sin(a);
    const float3 r = r0 * c + u0 * s;
    const float3 u = u0 * c - r0 * s;
    const float3 w = center + pos_scale.w * (pos.x * f + pos.y * u + pos.z * r);
    VsOut o;
    // Yakin ayiklama CPU tarafiyla ayni kural (merkez derinligi < yakin sinir -> cizilmez): akis kipinde kameraya
    // varan nesne ekrani kaplamadan duser (tum koseler kirpma hacmi disina tasinir), iki kipte GPU yuku ayni kalir.
    o.pos = zi < arena_u.z ? float4(0, 0, -1, 1) : mul(float4(w, 1), view_proj);
    o.wpos = w;
    o.nrm = nrm.x * f + nrm.y * u + nrm.z * r;
    o.color = float4(color_rate.rgb, 1);
    return o;
}

struct VsInst {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float4 pos_scale : INST0;
    float4 dir_roll : INST1;
    float4 color_rate : INST2;
    uint4 zq : INST3;
};
VsOut vs_arena_inst(VsInst i) { return arena_object(i.pos, i.nrm, i.pos_scale, i.dir_roll, i.color_rate, i.zq); }

struct VsObj {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};
VsOut vs_arena_object(VsObj i) { return arena_object(i.pos, i.nrm, obj_pos_scale, obj_dir_roll, obj_color_rate, obj_zq); }

// ---------------- 1) G-buffer: tunel (kose tamponu yok; 5 dortgen = 30 kose) ----------------
// Dort duvar + uc kapak: her piksel geometri tasir, boylece isik dongusu tum piksellerde calisir.
VsOut vs_arena_tunnel(uint id : SV_VertexID) {
    static const float2 kCorner[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1)};
    const uint quad = id / 6;
    const float2 t = kCorner[id % 6];
    const float h = arena_f.z, z0 = -arena_g.y, z1 = arena_f.w + 8.0;
    const float a = lerp(-h, h, t.x), z = lerp(z0, z1, t.y);
    float3 p, n;
    if (quad == 0) {
        p = float3(-h, a, z), n = float3(1, 0, 0);
    } else if (quad == 1) {
        p = float3(h, a, z), n = float3(-1, 0, 0);
    } else if (quad == 2) {
        p = float3(a, -h, z), n = float3(0, 1, 0);
    } else if (quad == 3) {
        p = float3(a, h, z), n = float3(0, -1, 0);
    } else {
        p = float3(a, lerp(-h, h, t.y), z1), n = float3(0, 0, -1);
    }
    VsOut o;
    o.pos = mul(float4(p, 1), view_proj);
    o.wpos = p;
    o.nrm = n;
    o.color = float4(arena_wall.rgb, 0);
    return o;
}

Texture2D<float4> noise_tex : register(t0);
SamplerState wrap_smp : register(s0);

struct GOut {
    float4 albedo : SV_Target0;  // rgb albedo, a isima payi
    float4 nrm : SV_Target1;     // xyz normal, w parlaklik ussu
    float4 wpos : SV_Target2;    // xyz kameraya gore konum, w = 1: geometri var
};

float3 triplanar(float3 p, float3 w) {
    return noise_tex.Sample(wrap_smp, p.yz).rgb * w.x + noise_tex.Sample(wrap_smp, p.zx).rgb * w.y +
           noise_tex.Sample(wrap_smp, p.xy).rgb * w.z;
}

GOut ps_arena_gbuffer(VsOut i) {
    const float3 n = normalize(i.nrm);
    float3 w = abs(n);
    w /= max(w.x + w.y + w.z, 1e-4);
    // Doku koordinati: tunel dongusundeki dunya konumu (duvar deseni kamerayla birlikte akar).
    const float3 world = float3(i.wpos.xy, i.wpos.z + arena_f.x);
    // Frekanslar tunel uzunlugunun (512) tam bolenleridir: dongu sarmasinda desen kesintisiz kalir.
    float3 fbm = 0;
    float amp = 0.5, freq = 26.0 / 512.0;
    [unroll] for (int o = 0; o < 3; ++o) {
        fbm += amp * triplanar(world * freq, w);
        amp *= 0.5;
        freq *= 2.0;
    }
    // Tunel paneli: duvar duzleminde arena_g.w adimli hucreler, kenarlarda ince isikli cizgi. Turev (fwidth)
    // dallanma disinda hesaplanir; nesne / tunel secimi en sonda yapilir.
    const float2 uv = w.x > 0.5 ? world.yz : (w.y > 0.5 ? world.xz : world.xy);
    const float2 cell = abs(frac(uv / arena_g.w) - 0.5) * arena_g.w;
    const float edge = max(cell.x, cell.y);
    const float lim = 0.5 * arena_g.w - 0.14;
    const float aa = max(fwidth(edge), 1e-3);
    const float ln = smoothstep(lim - aa, lim + aa, edge);
    const float4 wall_albedo = float4(lerp(arena_wall.rgb * (0.45 + 0.9 * fbm.r), arena_line.rgb, ln), ln * arena_line.a);
    const float4 wall_nrm = float4(normalize(n + (fbm - 0.47) * 0.35), 12.0 + 40.0 * fbm.g);
    const float4 obj_albedo = float4(i.color.rgb * (0.65 + 0.5 * fbm.r), arena_fog.a);
    const float4 obj_nrm = float4(n, 24.0 + 48.0 * fbm.g);
    const float is_obj = step(0.5, i.color.a);
    GOut g;
    g.albedo = lerp(wall_albedo, obj_albedo, is_obj);
    g.nrm = lerp(wall_nrm, obj_nrm, is_obj);
    g.wpos = float4(i.wpos, 1);
    return g;
}

// ---------------- 2) Aydinlatma ----------------
struct PointLight {
    float4 pos_inv_r2;  // xy konum, z isik periyodundaki dunya z'si, w = 1/r^2
    float4 color;
};
Texture2D<float4> g_albedo : register(t0);
Texture2D<float4> g_normal : register(t1);
Texture2D<float4> g_wpos : register(t2);
StructuredBuffer<PointLight> lights : register(t3);

// Taban parca (z = 1) ortam, isima ve sis rengini yazar; diger parcalar yalniz isik katkilarini toplar.
float4 ps_arena_light(float4 sv : SV_Position) : SV_Target {
    const int3 px = int3(sv.xy, 0);
    const float4 wp = g_wpos.Load(px);
    const bool base = chunk.z != 0;
    if (wp.w == 0) return base ? float4(arena_fog.rgb, 1) : float4(0, 0, 0, 0);
    const float4 alb = g_albedo.Load(px);
    const float4 nn = g_normal.Load(px);
    const float3 n = normalize(nn.xyz);
    const float3 v = normalize(-wp.xyz);
    float3 col = base ? alb.rgb * (0.035 + alb.a * arena_g.z + sun_dir.w * saturate(dot(n, -sun_dir.xyz))) : float3(0, 0, 0);
    float3 acc = 0;
    const uint end = min(chunk.y, counts.x);
    [loop] for (uint k = chunk.x; k < end; ++k) {
        const PointLight L = lights[k];
        // Isik dunyaya sabittir; kameranin arena_g.y gerisi ile (periyot - arena_g.y) ilerisi arasina sarilir.
        float lz = L.pos_inv_r2.z - arena_f.y;
        lz += lz < -arena_g.y ? arena_g.x : 0.0;
        lz -= lz >= arena_g.x - arena_g.y ? arena_g.x : 0.0;
        const float3 d = float3(L.pos_inv_r2.xy, lz) - wp.xyz;
        const float d2 = max(dot(d, d), 1e-4);
        const float3 l = d * rsqrt(d2);
        float att = saturate(1 - d2 * L.pos_inv_r2.w);
        att *= att;
        const float ndl = saturate(dot(n, l));
        const float3 h = normalize(l + v);
        const float spec = pow(saturate(dot(n, h)), nn.w);
        acc += L.color.rgb * att * (alb.rgb * ndl + spec * 0.2);
    }
    col += acc * misc.x;
    const float fog = saturate(length(wp.xyz) / arena_f.w);
    const float f2 = fog * fog;
    const float3 fog_part = base ? arena_fog.rgb * f2 : float3(0, 0, 0);
    return float4(col * (1 - f2) + fog_part, base ? 1 : 0);
}
