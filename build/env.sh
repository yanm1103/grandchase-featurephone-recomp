# ambiente de build (clang + MSVC CRT + Windows SDK + SDL3 + Unicorn). Uso: source build/env.sh
export MSYS2_ARG_CONV_EXCL='*'          # impede o Git Bash de reescrever /LIBPATH: etc.
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$R/build/env.local.sh" ] && source "$R/build/env.local.sh"
# Caminhos das ferramentas: ajuste aqui ou crie build/env.local.sh (ignorado pelo git) definindo as variaveis abaixo.
LLVM="${LLVM:-C:/Program Files/LLVM}"
CLANG="${CLANG:-$LLVM/bin/clang.exe}"; LLVM_RC="${LLVM_RC:-$LLVM/bin/llvm-rc.exe}"
MSVC="${MSVC:-C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207}"
WK="${WK:-C:/Program Files (x86)/Windows Kits/10}"; WV="${WV:-10.0.26100.0}"
SDL="$R/third_party/SDL3-3.4.20"; UNI="$R/third_party/unicorn"
CFLAGS_SYS=(-isystem "$MSVC/include" -isystem "$WK/Include/$WV/ucrt" -isystem "$WK/Include/$WV/um" -isystem "$WK/Include/$WV/shared")
LDFLAGS_SYS=(-fuse-ld=lld -L"$MSVC/lib/x64" -L"$WK/Lib/$WV/ucrt/x64" -L"$WK/Lib/$WV/um/x64")
cc() { "$CLANG" "${CFLAGS_SYS[@]}" "$@" "${LDFLAGS_SYS[@]}"; }
