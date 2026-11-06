package org.foliosketch.android;

import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Build;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.SurfaceView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.graphics.lowlatency.BufferInfo;
import androidx.graphics.lowlatency.GLFrontBufferedRenderer;
import androidx.graphics.opengl.egl.EGLManager;
import java.util.Collection;

/**
 * Dedicated bare-metal hardware front-buffered scanout view for zero-latency stylus inking.
 *
 * Runs directly on Android's hardware scanout overlay buffer (via GLFrontBufferedRenderer),
 * bypassing SurfaceFlinger multi-buffer presentation delays and VSYNC queues.
 */
@RequiresApi(Build.VERSION_CODES.Q)
public class FolioInkView extends SurfaceView {

    private long mEngineHandle = 0;
    private GLFrontBufferedRenderer<Object> mFrontBufferedRenderer;

    private final GLFrontBufferedRenderer.Callback<Object> mRendererCallback =
        new GLFrontBufferedRenderer.Callback<Object>() {
            @Override
            public void onDrawFrontBufferedLayer(
                @NonNull EGLManager eglManager,
                int width,
                int height,
                @NonNull BufferInfo bufferInfo,
                @NonNull float[] transform,
                @NonNull Object param
            ) {
                if (mEngineHandle != 0) {
                    FolioNativeBridge.nativeRenderWet(mEngineHandle, width, height, transform);
                }
            }

            @Override
            public void onDrawMultiBufferedLayer(
                @NonNull EGLManager eglManager,
                int width,
                int height,
                @NonNull BufferInfo bufferInfo,
                @NonNull float[] transform,
                @NonNull Collection<? extends Object> params
            ) {
                if (mEngineHandle != 0) {
                    FolioNativeBridge.nativeRenderDry(mEngineHandle, width, height, transform);
                }
            }
        };

    public FolioInkView(@NonNull Context context) {
        super(context);
        init();
    }

    public FolioInkView(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public FolioInkView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        mEngineHandle = FolioNativeBridge.nativeCreateEngine();
        mFrontBufferedRenderer = new GLFrontBufferedRenderer<>(this, mRendererCallback);
    }

    public void clearCanvas() {
        if (mEngineHandle != 0) {
            FolioNativeBridge.nativeClearCanvas(mEngineHandle);
            if (mFrontBufferedRenderer != null) {
                mFrontBufferedRenderer.commit();
            }
        }
    }

    public void release() {
        if (mFrontBufferedRenderer != null) {
            mFrontBufferedRenderer.release(false);
            mFrontBufferedRenderer = null;
        }
        if (mEngineHandle != 0) {
            FolioNativeBridge.nativeDestroyEngine(mEngineHandle);
            mEngineHandle = 0;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int actionIndex = event.getActionIndex();
        final int toolType = (actionIndex >= 0 && actionIndex < event.getPointerCount())
            ? event.getToolType(actionIndex)
            : event.getToolType(0);

        final boolean isStylus = (toolType == MotionEvent.TOOL_TYPE_STYLUS ||
                                  toolType == MotionEvent.TOOL_TYPE_ERASER);

        if (!isStylus) {
            return false;
        }

        // Request unbuffered dispatch for stylus (bypasses 60Hz OS VSYNC batching)
        requestUnbufferedDispatch(event);

        if (mEngineHandle == 0 || mFrontBufferedRenderer == null) {
            return super.onTouchEvent(event);
        }

        final int action = event.getActionMasked();
        final float tiltX = event.getAxisValue(MotionEvent.AXIS_TILT);
        final float tiltY = event.getAxisValue(MotionEvent.AXIS_ORIENTATION);

        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                FolioNativeBridge.nativeIngestPacket(
                    mEngineHandle,
                    event.getX(), event.getY(), event.getPressure(),
                    tiltX, tiltY,
                    event.getEventTime() * 1_000_000L,
                    true, false, isStylus
                );
                if (isStylus) {
                    mFrontBufferedRenderer.renderFrontBufferedLayer(this);
                }
                return true;
            }

            case MotionEvent.ACTION_MOVE: {
                // Drain historical high-frequency samples from digitizer batch (240Hz / 480Hz report rate)
                final int historySize = event.getHistorySize();
                for (int h = 0; h < historySize; ++h) {
                    float hp = event.getHistoricalPressure(0, h);
                    float hTiltX = event.getHistoricalAxisValue(MotionEvent.AXIS_TILT, 0, h);
                    float hTiltY = event.getHistoricalAxisValue(MotionEvent.AXIS_ORIENTATION, 0, h);
                    FolioNativeBridge.nativeIngestPacket(
                        mEngineHandle,
                        event.getHistoricalX(0, h),
                        event.getHistoricalY(0, h),
                        hp > 1.0f ? 1.0f : hp,
                        hTiltX, hTiltY,
                        event.getHistoricalEventTime(h) * 1_000_000L,
                        false, false, isStylus
                    );
                }

                float p = event.getPressure();
                FolioNativeBridge.nativeIngestPacket(
                    mEngineHandle,
                    event.getX(), event.getY(),
                    p > 1.0f ? 1.0f : p,
                    tiltX, tiltY,
                    event.getEventTime() * 1_000_000L,
                    false, false, isStylus
                );
                mFrontBufferedRenderer.renderFrontBufferedLayer(this);
                return true;
            }

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL: {
                float p = event.getPressure();
                FolioNativeBridge.nativeIngestPacket(
                    mEngineHandle,
                    event.getX(), event.getY(),
                    p > 1.0f ? 1.0f : p,
                    tiltX, tiltY,
                    event.getEventTime() * 1_000_000L,
                    false, true, isStylus
                );
                mFrontBufferedRenderer.commit();
                return true;
            }

            default:
                break;
        }

        return super.onTouchEvent(event);
    }
}
