#!/bin/bash
# Monta o APK do Grand Chase 1 (recomp) sem Gradle: CMake/NDK -> .so, javac+d8 -> dex, aapt2 -> apk, zipalign, apksigner (chave de debug).
# Uso: bash android/build_apk.sh [abi ...]        (padrao: x86_64 arm64-v8a)     saida: build/android/gc1.apk
# Precisa do SDK Android (platform 34, build-tools 35.0.1, NDK 27.2, cmake 3.22.1) e JDK 21. Caminhos: variaveis ANDROID_SDK e JAVA_HOME
# (ou android/env.local.sh, ignorado pelo git).
set -e
cd "$(dirname "$0")/.."; ROOT=$PWD
[ -f android/env.local.sh ] && source android/env.local.sh
SDK=${ANDROID_SDK:-C:/Android/sdk}; NDK=$SDK/ndk/27.2.12479018; BT=$SDK/build-tools/35.0.1; AJAR=$SDK/platforms/android-34/android.jar
CMAKE=$SDK/cmake/3.22.1/bin/cmake.exe; NINJA=$SDK/cmake/3.22.1/bin/ninja.exe
export JAVA_HOME="${JAVA_HOME:-C:/Program Files/Eclipse Adoptium/jdk-21}"; export PATH="$JAVA_HOME/bin:$PATH"
ABIS=${@:-x86_64 arm64-v8a}; B=build/android; mkdir -p $B
SRC_ASSETS=${GC1_GAME:-build/out/game_ptbr}; SRC_AUDIO=${GC1_AUDIO:-build/out/audio}
# 1. nativo
for abi in $ABIS; do
  echo "== nativo $abi"
  "$CMAKE" -S android -B $B/$abi -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=$abi -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release > $B/cmake_$abi.log 2>&1 || { tail -20 $B/cmake_$abi.log; exit 1; }
  "$CMAKE" --build $B/$abi --target main SDL3-shared > $B/build_$abi.log 2>&1 || { grep -E "error|Error" -A3 $B/build_$abi.log | head -40; exit 1; }
done
# 2. assets (jogo + audio + lista de arquivos + carimbo de versao)
rm -rf $B/assets; mkdir -p $B/assets
cp -r "$SRC_ASSETS" $B/assets/game; cp -r "$SRC_AUDIO" $B/assets/audio
(cd $B/assets && find game audio -type f | sort > files.txt && date +%s > apkstamp.txt)
# 3. recursos (icone)
rm -rf $B/res $B/rescompiled; mkdir -p $B/res/mipmap-xxxhdpi $B/rescompiled
python -I -c "
from PIL import Image
Image.open('platform/icon_src_61.png').convert('RGBA').resize((192,192), Image.NEAREST).save('$B/res/mipmap-xxxhdpi/ic_launcher.png')"
"$BT/aapt2.exe" compile --dir $B/res -o $B/rescompiled
# 4. java -> dex
rm -rf $B/classes $B/dex; mkdir -p $B/classes $B/dex
SDLJ=third_party/SDL3-3.4.20-src/android-project/app/src/main/java
javac -nowarn --release 8 -cp "$AJAR" -d $B/classes $(find $SDLJ -name '*.java') $(find android/java -name '*.java') 2>&1 | grep -v "^Note:\|warning" || true
"$BT/d8.bat" --lib "$AJAR" --release --min-api 24 --output $B/dex $(find $B/classes -name '*.class')
# 5. apk
rm -f $B/base.apk $B/gc1-unaligned.apk $B/gc1.apk
"$BT/aapt2.exe" link -o $B/base.apk -I "$AJAR" --manifest android/AndroidManifest.xml --min-sdk-version 24 --target-sdk-version 34 -A $B/assets $B/rescompiled/*.flat
python -I - "$B" $ABIS <<'PY'
import sys, zipfile, os, shutil
B = sys.argv[1]; abis = sys.argv[2:]
shutil.copy(f'{B}/base.apk', f'{B}/gc1-unaligned.apk')
z = zipfile.ZipFile(f'{B}/gc1-unaligned.apk', 'a', zipfile.ZIP_DEFLATED)
z.write(f'{B}/dex/classes.dex', 'classes.dex')
for abi in abis:
    for so in ('libmain.so', 'SDL3-build/libSDL3.so'):
        p = f'{B}/{abi}/{so}'
        z.write(p, f'lib/{abi}/{os.path.basename(so)}')
z.close()
PY
"$BT/zipalign.exe" -f -p 4 $B/gc1-unaligned.apk $B/gc1.apk
[ -f $B/debug.keystore ] || keytool -genkeypair -keystore $B/debug.keystore -storepass android -keypass android -alias gc1 -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=gc1 debug" > /dev/null 2>&1
"$BT/apksigner.bat" sign --ks $B/debug.keystore --ks-pass pass:android --key-pass pass:android $B/gc1.apk
ls -la $B/gc1.apk
