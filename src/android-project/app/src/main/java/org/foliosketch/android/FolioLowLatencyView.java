package org.foliosketch.android;

import android.content.Context;
import android.os.Build;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.SurfaceView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.graphics.surface.FrontBufferedRenderer;

/**
 * Production-ready Android SurfaceView for zero-latency inking using Folio-Sketch.
 *
 * Implements:
 * 1. Pillar 1: Unbuffered digitizer packet harvesting at 240Hz-480Hz via requestUnbufferedDispatch
 *    and chronological MotionEvent historical sample extraction.
 * 2. Pillar 2: Two-Phase Layer Compositor via Jetpack FrontBufferedRenderer (active in-flight
 *    strokes render directly to hardware scanout buffers, bypassing SurfaceFlinger).
 * 3. Pillar 3: Delegation to Folio-Sketch's unified GPU Capsule SDF fragment shader via JNI.
 */
public class FolioLowLatencyView extends SurfaceView {

    private long mEngineHandle = 0;
    private FrontBufferedRenderer<Object> mFrontBufferedRenderer;

    private final FrontBufferedRenderer.Callback<Object> mRendererCallback =
        new FrontBufferedRenderer.Callback<Object>() {
            @Override
            public void onDrawFrontBufferedLayer(
                @NonNull androidx.graphics.surface.SurfaceControlCompat surfaceControl,
                int bufferWidth,
                int bufferHeight,
                @Nullable Object param
            ) {
                if (mEngineHandle != 0) {
                    FolioNativeBridge.nativeRenderWet(mEngineHandle, bufferWidth, bufferHeight);
                }
            }

            @Override
            public void onDrawMultiBufferedLayer(
                @NonNull androidx.graphics.surface.SurfaceControlCompat surfaceControl,
                int bufferWidth,
                int bufferHeight
            ) {
                if (mEngineHandle != 0) {
                    FolioNativeBridge.nativeRenderDry(mEngineHandle, bufferWidth, bufferHeight);
                }
            }
        };

    public FolioLowLatencyView(@NonNull Context context) {
        super(context);
        init();
    }

    public FolioLowLatencyView(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public FolioLowLatencyView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        mEngineHandle = FolioNativeBridge.nativeCreateEngine();
        mFrontBufferedRenderer = new FrontBufferedRenderer<>(this, mRendererCallback);
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
            mFrontBufferedRenderer.release(true);
            mFrontBufferedRenderer = null;
        }
        if (mEngineHandle != 0) {
            FolioNativeBridge.nativeDestroyEngine(mEngineHandle);
            mEngineHandle = 0;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int toolType = event.getToolType(0);
        final boolean isStylus = (toolType == MotionEvent.TOOL_TYPE_STYLUS ||
                                  toolType == MotionEvent.TOOL_TYPE_ERASER);

        // Pillar 1: Request unbuffered dispatch from the OS InputDispatcher
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            requestUnbufferedDispatch(event);
        }

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
                mFrontBufferedRenderer.renderFrontBufferedLayer(this);
                return true;
            }

            case MotionEvent.ACTION_MOVE: {
                // Drain historical packets in forward chronological order (0 to historySize - 1)
                final int historySize = event.getHistorySize();
                for (int h = 0; h < historySize; ++h) {
                    float hp = event.getHistoricalPressure(h);
                    FolioNativeBridge.nativeIngestPacket(
                        mEngineHandle,
                        event.getHistoricalX(h),
                        event.getHistoricalY(h),
                        hp > 1.0f ? 1.0f : hp,
                        tiltX, tiltY,
                        event.getHistoricalEventTime(h) * 1_000_000L,
                        false, false, isStylus
                    );
                }

                // Ingest latest point
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
                // Commit wet stroke into persistent dry document FBO and present multi-buffered frame
                mFrontBufferedRenderer.commit();
                return true;
            }

            default:
                break;
        }

        return super.onTouchEvent(event);
    }
}
