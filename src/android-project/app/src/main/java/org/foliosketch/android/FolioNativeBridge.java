package org.foliosketch.android;

/**
 * JNI bindings connecting Android low-latency touch dispatch directly to Folio-Sketch C++ engine.
 */
public final class FolioNativeBridge {

    private FolioNativeBridge() {
        // Prevent instantiation of utility class
    }

    static {
        System.loadLibrary("main");
    }

    public static native long nativeCreateEngine();
    public static native void nativeDestroyEngine(long handle);

    public static native void nativeIngestPacket(
        long handle,
        float x, float y, float pressure,
        float tiltX, float tiltY,
        long timestampNs,
        boolean isDown, boolean isUp, boolean isStylus
    );

    public static native void nativeRenderWet(long handle, int width, int height, float[] surfaceTransform);
    public static native void nativeRenderDry(long handle, int width, int height, float[] surfaceTransform);
    public static native void nativeClearCanvas(long handle);
}
