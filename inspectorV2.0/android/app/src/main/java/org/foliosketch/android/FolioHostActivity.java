package org.foliosketch.android;

import android.os.Build;
import android.os.Bundle;
import androidx.annotation.RequiresApi;
import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

/**
 * FolioHostActivity embeds SDL3 as the application host (windowing, ImGui, multi-buffer background).
 */
@RequiresApi(Build.VERSION_CODES.Q)
public class FolioHostActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected int getInitSubsystems() {
        // Do NOT initialize SDL video on Java side to avoid creating an invisible mSurface that blocks thread startup
        return SDL.SDL_INIT_AUDIO | SDL.SDL_INIT_JOYSTICK | SDL.SDL_INIT_HAPTIC | SDL.SDL_INIT_GAMEPAD | SDL.SDL_INIT_SENSOR | SDL.SDL_INIT_CAMERA;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "main"
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "libmain.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }
}
