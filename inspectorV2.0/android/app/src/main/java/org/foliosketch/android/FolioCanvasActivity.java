package org.foliosketch.android;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;
import androidx.annotation.RequiresApi;

/**
 * Lightweight, bare-metal Android Activity for zero-latency stylus inking.
 *
 * Eliminates desktop windowing, VSYNC-throttled SDL event loops, and multi-buffer swapchain queues.
 * Displays FolioInkView full-screen to test Folio-Sketch C++ inking directly on the glass.
 */
@RequiresApi(Build.VERSION_CODES.Q)
public class FolioCanvasActivity extends Activity {

    private FolioInkView mInkView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setBackgroundDrawableResource(android.R.color.transparent);

        mInkView = new FolioInkView(this);
        setContentView(mInkView);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mInkView != null) {
            mInkView.post(new Runnable() {
                @Override
                public void run() {
                    if (mInkView != null) {
                        mInkView.requestMultiBufferRender();
                    }
                }
            });
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mInkView != null) {
            mInkView.release();
            mInkView = null;
        }
    }
}
