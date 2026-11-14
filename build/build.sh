#!/bin/bash
# Compila o recomp. Uso: bash build/build.sh [headless|sdl|all]   (gera build/out/*.exe)
# Passos: (1) gera gen/*.c com tools/lift/emit.py se faltar; (2) compila objetos em paralelo; (3) liga.
set -e
cd "$(dirname "$0")/.."
source build/env.sh
OUT=build/out; mkdir -p $OUT/obj
TARGET=${1:-headless}
PY=python
CF=(-O1 -w -I runtime -DGC1_CHECK_MEM)
if [ ! -f gen/tables.c ] || [ tools/lift/lift.py -nt gen/tables.c ] || [ tools/lift/cfg.py -nt gen/tables.c ] || [ tools/lift/emit.py -nt gen/tables.c ]; then
  echo "gerando gen/ ..."; $PY -I tools/lift/emit.py gen > /dev/null
fi
objs=()
compile() { # compile <src>  -> obj
  local src=$1 o=$OUT/obj/$(echo "$1" | tr '/' '_').o
  if [ ! -f "$o" ] || [ "$src" -nt "$o" ] || [ runtime/rt.h -nt "$o" ] || [ runtime/gc1.h -nt "$o" ]; then cc "${CF[@]}" -I third_party/unicorn/include -c "$src" -o "$o"; fi
}
pids=()
for src in gen/blocks_*.c gen/tables.c runtime/cpu.c runtime/hle.c runtime/audio_policy.c runtime/cpu_unicorn.c; do
  compile "$src" & pids+=($!)
  [ "$src" != runtime/cpu_unicorn.c ] && objs+=("$OUT/obj/$(echo "$src" | tr '/' '_').o")
  if (( ${#pids[@]} >= 8 )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
for p in "${pids[@]}"; do wait "$p"; done
if [ "$TARGET" = headless ] || [ "$TARGET" = all ]; then
  compile platform/main_headless.c
  cc "${CF[@]}" "${objs[@]}" "$OUT/obj/platform_main_headless.c.o" -o $OUT/gc1_headless.exe
  echo "ok: $OUT/gc1_headless.exe"
fi
if [ "$TARGET" = ref ] || [ "$TARGET" = all ]; then
  compile platform/main_headless.c
  ro=(); for o in "${objs[@]}"; do [ "$o" != "$OUT/obj/runtime_cpu.c.o" ] && ro+=("$o"); done
  cc "${CF[@]}" -I third_party/unicorn/include "${ro[@]}" "$OUT/obj/runtime_cpu_unicorn.c.o" "$OUT/obj/platform_main_headless.c.o" third_party/unicorn/unicorn.lib -o $OUT/gc1_ref_headless.exe
  cp -f third_party/unicorn/unicorn.dll $OUT/
  echo "ok: $OUT/gc1_ref_headless.exe"
fi
if [ "$TARGET" = sdl ] || [ "$TARGET" = all ]; then
  cc "${CF[@]}" -I third_party/SDL3-3.4.20/include -c platform/main_sdl.c -o "$OUT/obj/platform_main_sdl.c.o"
  (cd platform && "C:/Users/TwistZero/tools/llvm/bin/llvm-rc.exe" /FO "../$OUT/obj/icon.res" icon.rc)   # icone do exe (platform/icon.ico)
  cc "${CF[@]}" "${objs[@]}" "$OUT/obj/platform_main_sdl.c.o" "$OUT/obj/icon.res" -L third_party/SDL3-3.4.20/lib/x64 -lSDL3 -o $OUT/gc1.exe
  cp -f third_party/SDL3-3.4.20/lib/x64/SDL3.dll $OUT/
  echo "ok: $OUT/gc1.exe"
fi
