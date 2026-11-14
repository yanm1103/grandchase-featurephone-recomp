#version 450
// Filtro LCD (do Miki's World, adaptado): vao escuro entre os pixels do jogo, brilho levemente aumentado para compensar.
// Fragment shader do renderer GPU do SDL3: v_uv = posicao no quadro do jogo (0..1); Params.prm = (largura do jogo, altura, pixels de tela por pixel do jogo, 0).
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params { vec4 prm; };

void main()
{
    vec3 c = texture(u_tex, v_uv).rgb;
    vec2 g = v_uv * prm.xy;                                                // posicao em pixels do jogo
    float ps = prm.z;
    vec2 d = min(fract(g), 1.0 - fract(g));                                // distancia ate a borda mais proxima do pixel
    float gap = clamp(1.25 / ps, 0.05, 0.22);
    vec2 m = smoothstep(vec2(0.0), vec2(gap), d);
    float strength = clamp((ps - 1.5) / 2.5, 0.0, 1.0);                    // sem grade quando os pixels sao pequenos demais
    c *= mix(1.0, mix(0.5, 1.0, m.x * m.y) * 1.12, strength);
    o_color = vec4(c, 1.0) * v_color;
}
