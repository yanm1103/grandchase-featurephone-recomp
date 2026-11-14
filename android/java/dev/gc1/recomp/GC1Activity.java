package dev.gc1.recomp;

import org.libsdl.app.SDLActivity;

/** SDLActivity ja carrega libSDL3.so e libmain.so; aqui so fixamos os nomes. */
public class GC1Activity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "SDL3", "main" }; }
}
