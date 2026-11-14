#version 450
// Filtro CRT (do Miki's World, adaptado): leve curvatura, linhas de varredura, mascara de fosforo RGB, um pouco de brilho e vinheta.
// Params.prm = (largura do jogo, altura, pixels de tela por pixel do jogo, 0).
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params { vec4 prm; };

void main()
{
    vec2 cc = v_uv * 2.0 - 1.0;
    cc *= vec2(1.0 + 0.025 * cc.y * cc.y, 1.0 + 0.035 * cc.x * cc.x);       // barril
    vec2 uv2 = cc * 0.5 + 0.5;
    if (any(lessThan(uv2, vec2(0.0))) || any(greaterThan(uv2, vec2(1.0)))) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }

    float ps = prm.z;
    vec3 c = texture(u_tex, uv2).rgb;
    vec2 o = vec2(0.6) / prm.xy;                                           // brilho: 4 amostras ao redor (0,6 pixel do jogo)
    vec3 glow = (texture(u_tex, uv2 + vec2(o.x, 0.0)).rgb + texture(u_tex, uv2 - vec2(o.x, 0.0)).rgb
               + texture(u_tex, uv2 + vec2(0.0, o.y)).rgb + texture(u_tex, uv2 - vec2(0.0, o.y)).rgb) * 0.25;
    c = c + 0.18 * glow;

    float fx = clamp((ps - 1.5) / 2.5, 0.0, 1.0);                          // efeitos somem quando os pixels sao pequenos demais
    float row = fract(uv2.y * prm.y) - 0.5;
    float scan = mix(1.0, 0.45 + 0.55 * exp(-row * row * 11.0), fx);
    int tri = int(mod(floor(v_uv.x * prm.x * ps), 3.0));
    vec3 mask = vec3(0.72);
    mask[tri] = 1.0;
    c *= scan * mix(vec3(1.0), mask * 1.15, fx);
    vec2 v = uv2 * (1.0 - uv2);
    c *= pow(clamp(v.x * v.y * 16.0, 0.0, 1.0), 0.18) * 1.1;
    o_color = vec4(c, 1.0) * v_color;
}
