# Grand Chase 1 (LG U+ / WIPI-C) — recomp · 그랜드체이스 1 리컴파일

**[Português (BR)](#português-br) · [한국어](#한국어)**

*Static recompilation of the Korean feature-phone game Grand Chase 1 (WIPI-C, ARM → C) with an SDL3 runtime. No game files included.*

---

## Português (BR)

Recompilação estática do `binary.mod` (ARM/Thumb) do Grand Chase 1 para C, com um runtime em C + SDL3 que reimplementa a API do aparelho.
Roda nativo no PC (Windows) e no Android, e inclui uma retradução PT-BR feita a partir do coreano.

> **Os arquivos do jogo não estão incluídos.** Você precisa da sua própria cópia do jar, já extraído (pasta com `binary.mod`, `*.pbp`, `*.mmf`…).
> Grand Chase pertence aos seus donos; este projeto não é afiliado a eles. Não distribua `gc1.exe`/APK compilados: eles embutem o código recompilado (e, no APK, os arquivos do jogo). Compartilhe só o código-fonte.

**Requisitos:** clang + lld, VS Build Tools 2022, Windows SDK, Python 3 (`pip install capstone pyelftools pillow`) e [SDL3 3.4.20 (pacote VC)](https://github.com/libsdl-org/SDL/releases) em `third_party/SDL3-3.4.20/`.
Os caminhos das ferramentas ficam em `build/env.sh`; para a sua máquina, crie `build/env.local.sh`.

**Compilar:**
```bash
export GC1_BINMOD=/caminho/do/jar/binary.mod   # só para gerar o código recompilado (gen/)
bash build/build.sh sdl                        # gera build/out/gc1.exe
```

**Jogar:**
```bash
build/out/gc1.exe --res /caminho/do/jar --save save [--audio audio] [--scale 3] [--fullscreen]
```
Teclas: setas · **Enter/Z/Espaço** = ação · **Q/A** = tecla esquerda · **W/S/Esc** = tecla direita · Backspace/C = limpar · 0-9, `*`, `#` · F1 = ajuda · F11 = tela cheia. Gamepad: D-pad, A, X, B.
O áudio precisa de clipes pré-renderizados (`tools/make_audio.py`); sem `--audio` o jogo roda sem som.

**Retradução PT-BR:** `tools/translate/` (precisa dos jars `ko` e `pt` extraídos em `$GC1_EXTRACTED`). **Android:** `android/README.md`.
**Mais:** [arquitetura e validação](docs/ARQUITETURA.md) · [pendências](PROBLEMAS.md) · [licenças de terceiros](THIRD_PARTY_NOTICES.md) · licença do código: MIT.

---

## 한국어

그랜드체이스 1(LG U+ / WIPI-C 피처폰판)의 `binary.mod`(ARM/Thumb)를 C로 정적 리컴파일하고, 단말기 API를 다시 구현한 C + SDL3 런타임으로 실행합니다.
PC(Windows)와 안드로이드에서 네이티브로 동작하며, 한국어 원문에서 다시 번역한 브라질 포르투갈어(PT-BR) 번역도 포함합니다.

> **게임 파일은 포함되어 있지 않습니다.** 압축을 푼 jar 폴더(`binary.mod`, `*.pbp`, `*.mmf` 등)를 직접 준비해야 합니다.
> 그랜드체이스의 권리는 각 권리자에게 있으며, 이 프로젝트는 그들과 무관합니다. 빌드한 `gc1.exe`/APK에는 리컴파일된 코드(APK는 게임 파일까지)가 들어 있으므로 배포하지 말고, 소스 코드만 공유하세요.

**필요한 것:** clang + lld, VS Build Tools 2022, Windows SDK, Python 3 (`pip install capstone pyelftools pillow`), 그리고 `third_party/SDL3-3.4.20/`에 [SDL3 3.4.20 (VC 패키지)](https://github.com/libsdl-org/SDL/releases).
도구 경로는 `build/env.sh`에 있으며, 내 환경에 맞게 `build/env.local.sh`를 만들어 덮어쓰세요.

**빌드:**
```bash
export GC1_BINMOD=/jar/경로/binary.mod    # 리컴파일 코드(gen/) 생성에만 필요
bash build/build.sh sdl                  # build/out/gc1.exe 생성
```

**실행:**
```bash
build/out/gc1.exe --res /jar/경로 --save save [--audio audio] [--scale 3] [--fullscreen]
```
키: 방향키 · **Enter/Z/Space** = 확인 · **Q/A** = 왼쪽 소프트키 · **W/S/Esc** = 오른쪽 소프트키 · Backspace/C = 지우기 · 0-9, `*`, `#` · F1 = 도움말 · F11 = 전체 화면. 게임패드: 십자키, A, X, B.
소리를 내려면 미리 렌더링한 오디오 클립이 필요합니다(`tools/make_audio.py`). `--audio`가 없으면 소리 없이 실행됩니다.

**PT-BR 재번역:** `tools/translate/` (`$GC1_EXTRACTED`에 압축을 푼 `ko`, `pt` jar 필요). **안드로이드:** `android/README.md`.
**더 보기:** [구조와 검증](docs/ARQUITETURA.md) · [알려진 문제](PROBLEMAS.md) · [서드파티 라이선스](THIRD_PARTY_NOTICES.md) · 코드 라이선스: MIT.
