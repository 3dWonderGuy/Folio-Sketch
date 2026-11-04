package org.foliosketch.android;

/**
 * JNI Bridge interface to Folio-Sketch's C++20 inking engine, predictive modeler,
 * and unified GPU Capsule SDF renderer.
 */
public final class FolioNativeBridge {
    static {
        System.loadLibrary("main");
    }

    private FolioNativeBridge() {}

    /**
     * Allocates native Folio engine, GlCapsuleRenderer, and WetDryCompositor session.
     * @return 64-bit native pointer handle.
     */
    public static native long nativeCreateEngine();

    /**
     * Destroys native session and frees GPU/ODE solver buffers.
     */
    public static native void nativeDestroyEngine(long engineHandle);

    /**
     * Ingests a single stylus/pointer packet.
     */
    public static native void nativeIngestPacket(
        long engineHandle,
        float x, float y, float pressure,
        float tiltX, float tiltY,
        long timestampNs,
        boolean isDown, boolean isUp, boolean isStylus
    );

    /**
     * Ingests a batch of chronological packets harvested from MotionEvent.
     */
    public static native void nativeIngestBatch(
        long engineHandle,
        float[] xCoords,
        float[] yCoords,
        float[] pressures,
        long[] timestampsNs,
        int count,
        boolean isDown,
        boolean isUp
    );

    /**
     * Renders active in-flight wet stroke directly to the front scanout buffer.
     */
    public static native void nativeRenderWet(long engineHandle, int width, int height);

    /**
     * Renders persistent dry document FBO to the multi-buffered surface.
     */
    public static native void nativeRenderDry(long engineHandle, int width, int height);

    /**
     * Clears persistent dry document canvas.
     */
    public static native void nativeClearCanvas(long engineHandle);
}
