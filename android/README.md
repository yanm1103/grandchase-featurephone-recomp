# Grand Chase 1 (recomp) - Android

Sem Gradle: `bash android/build_apk.sh [abi ...]` (padrao `x86_64 arm64-v8a`) gera `build/android/gc1.apk` (~34 MB com x86_64+arm64, ~22 MB so arm64; antes de gerar, comprima a musica: `python -I tools/compress_audio.py build/out/audio`; assinado com chave de debug).
Passos do script: CMake/NDK (SDL3 do codigo-fonte em `third_party/SDL3-3.4.20-src` + `libmain.so` = runtime + codigo recompilado + `platform/main_sdl.c`), assets (`build/out/game_ptbr`, `build/out/audio`),
icone (`platform/icon_src_61.png`), javac + d8 (fontes Java do SDL + `GC1Activity`), aapt2, zipalign, apksigner.
Ferramentas: `C:\Users\TwistZero\tools\android-sdk` (platform 34, build-tools 35.0.1 - o d8 do 34.0.0 quebra com uma classe do SDL, NDK 27.2, cmake 3.22.1, emulator, imagem x86_64 API 34), JDK 21. AVD `gc1` em `C:\Users\TwistZero\tools\android-avd`.

## Como o app funciona
- Na primeira execucao copia `assets/game`, `assets/audio` (lista em `files.txt`; `apkstamp.txt` no APK x `stamp.txt` interno decide se recopia; cuidado: SDL_LoadFile relativo olha o armazenamento interno antes do APK) para o armazenamento interno (`files/game`, `files/audio`); os saves ficam em `files/save` e nunca sao sobrescritos.
- Controles de toque (`platform/touch.h`): D-pad, OK, MENU (= CLR/voltar), MINIMAPA, MAPA, SALVAR (popup do jogo), atalhos 1 3 7 9 * 0 #. O botao Voltar do Android = CLR. Teclado fisico/emulador tambem funciona (`keymap.h`).
  No PC: `gc1.exe --touch` mostra o mesmo layout (o mouse vira dedo); `--selftest-shot arq.bmp` salva uma captura.
- Save state: no Android, F5/F9 de teclado fisico (`adb shell input keyevent KEYCODE_F5` / `KEYCODE_F9`); o arquivo e `files/save/estado.snap` e e o mesmo formato do PC (um estado gerado no PC carrega no Android).

## Testar no emulador (sem abrir janela)
```
emulator -avd gc1 -no-window -no-audio -no-snapshot -gpu swiftshader_indirect     (ANDROID_AVD_HOME=C:\Users\TwistZero\tools\android-avd)
adb install -r build/android/gc1.apk ; adb shell am start -n dev.gc1.recomp/.GC1Activity
adb exec-out screencap -p > tela.png          # eu leio a imagem
adb shell input tap X Y ; adb shell input swipe X Y X Y 1400     # toque / segurar (tela 1080x2400: OK ~ (864,1888), D-pad centro ~ (290,1860))
adb push build/pt/states/torre1.snap /data/local/tmp/estado.snap ; adb shell run-as dev.gc1.recomp cp /data/local/tmp/estado.snap files/save/estado.snap ; adb shell input keyevent KEYCODE_F9
```
(No Git Bash use `MSYS_NO_PATHCONV=1` antes dos comandos `adb shell` com caminhos.) O APK e `debuggable` (para `run-as`); mude em `AndroidManifest.xml` para uma versao final.

## Nao feito / limites
- Audio no Android nao foi ouvido (o emulador roda com `-no-audio`); o codigo de audio e o mesmo do PC.
- Sem auto-save ao ir para segundo plano; o Android pode matar o app (use SALVAR do jogo ou o estado).
- Layout de toque ajustado so para 1080x2400 retrato; so retrato (paisagem e tablet foram dispensados pelo usuario). Sem tela de ajuda/rebind (F1) no toque.

- Estado na tela: aba na borda esquerda (arraste na vertical para mudar a altura enquanto fechada; fica em `files/save/gaveta.txt`) abre uma gaveta com SALVAR ESTADO / CARREGAR ESTADO (mesma foto do F5/F9, `files/save/estado.snap`); o botao so dispara ao soltar o dedo em cima dele, e tocar fora fecha. Ao minimizar ou fechar, o app grava `files/save/auto.snap` e ao abrir de novo retoma dali; a gravacao sai de um observador de eventos (`bg_watch` em `platform/main_sdl.c`), porque o SDL bloqueia o app logo apos enfileirar WILL_ENTER_BACKGROUND.
