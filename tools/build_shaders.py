"""Compila platform/shaders/*.frag (GLSL 450 -> SPIR-V, glslc do Vulkan SDK) e gera platform/shaders_spv.h com um array por shader (spv_<nome>[]).
O header gerado e versionado: o build normal nao precisa do glslc. Uso: python -I tools/build_shaders.py   (GLSLC=caminho do glslc.exe se nao estiver no PATH)"""
import os, sys, glob, subprocess
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
glslc = os.environ.get('GLSLC') or next((p for p in (r'C:\VulkanSDK\1.4.363.1\Bin\glslc.exe', 'glslc') if os.path.exists(p) or p == 'glslc'), 'glslc')
out = ['/* GERADO por tools/build_shaders.py a partir de platform/shaders/*.frag - nao edite. */']
for f in sorted(glob.glob(os.path.join(ROOT, 'platform', 'shaders', '*.frag'))):
    name = os.path.splitext(os.path.basename(f))[0]
    spv = f[:-5] + '.spv'
    r = subprocess.run([glslc, '-fshader-stage=frag', '-O', f, '-o', spv], capture_output=True, text=True)
    if r.returncode: sys.exit(f'{name}: {r.stderr}')
    data = open(spv, 'rb').read(); os.remove(spv)
    out.append(f'static const unsigned char spv_{name}[] = {{')
    for i in range(0, len(data), 16): out.append('  ' + ','.join(f'0x{b:02x}' for b in data[i:i + 16]) + ',')
    out.append('};')
    print(f'{name}: {len(data)} bytes')
open(os.path.join(ROOT, 'platform', 'shaders_spv.h'), 'w', newline='\n').write('\n'.join(out) + '\n')
