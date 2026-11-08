package org.foliosketch.android;

import android.os.Build;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;
import androidx.annotation.RequiresApi;
import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

/**
 * FolioHostActivity embeds SDL3 as the application driver while attaching
 * FolioInkView as the sole hardware scanout surface (GLFrontBufferedRenderer).
 */
@RequiresApi(Build.VERSION_CODES.Q)
public class FolioHostActivity extends SDLActivity {

    private FolioInkView mInkView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        super.onCreate(savedInstanceState);

        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setBackgroundDrawableResource(android.R.color.transparent);

        // Attach FolioInkView as the SOLE SurfaceView in the window hierarchy
        mInkView = new FolioInkView(this);
        if (mLayout != null) {
            mLayout.addView(mInkView);
        } else {
            setContentView(mInkView);
        }
    }

    @Override
    protected void onDestroy() {
        if (mInkView != null) {
            mInkView.release();
            mInkView = null;
        }
        super.onDestroy();
    }

    @Override
    protected int getInitSubsystems() {
        // Exclude SDL_INIT_VIDEO on Java side so SDLActivity does NOT create a second SDLSurface.
        // SDL_main starts immediately and drives the application while FolioInkView owns the sole front buffer.
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
