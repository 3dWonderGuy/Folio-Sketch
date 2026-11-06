# Android Zero-Latency Backend

This directory contains the turnkey, production-proven **Zero-Latency Stylus Scanout Backend** for Android devices running `Folio-Sketch`.

It provides sub-2ms ("glued to the pen nib") inking on physical Android hardware (Samsung S-Pen, Pixel Pen, etc.) while maintaining clean, decoupled integration with the pure C++ core library.

---

## 1. Directory Structure

```text
folio_backends/android/
├── README.md                      # This integration guide
├── folio_android_jni.cpp          # JNI bridge forwarding Android digitizer to C++
├── android_frontbuffer.hpp / .cpp # Native front-buffer abstractions
└── java/org/foliosketch/android/  # Reusable Backend Java Library (Imported by any app)
    ├── FolioInkView.java          # Drop-in single hardware scanout SurfaceView
    └── FolioNativeBridge.java     # JNI native interface declarations

# Consumer / Application Host (Example: Inspector V2.0):
inspectorV2.0/android/
├── gradlew
├── app/
│   ├── build.gradle               # Includes folio_backends/android/java in sourceSets
│   └── src/main/
│       ├── AndroidManifest.xml
│       └── java/org/libsdl/app/SDLActivity.java
```

---

## 2. Core Architecture: The Single-Surface Rule

> **The Hardware Law:** A mobile Display Processing Unit (DPU) **cannot** maintain unbuffered direct hardware scanout if an application stacks multiple active `SurfaceView`s in the same window. Direct scanout is **only** granted when there is **exactly one** hardware `SurfaceView`.

To satisfy this law while supporting both live wet inking and a persistent multi-stroke document canvas, this backend implements the **Dual-Layer Single-Surface Architecture** via Jetpack's `GLFrontBufferedRenderer`:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   SINGLE HARDWARE SURFACEVIEW                          │
│                      (FolioInkView.java)                               │
├────────────────────────────────────────────────────────────────────────┤
│  LAYER A: Multi-Buffered Layer (Document Canvas)                       │
│  • Redraws at 120Hz VSync when pen lifts or canvas pans                │
│  • Clears to notebook paper color and renders all committed strokes    │
│  • Managed by: LayerManager::RenderDocumentCanvas()                    │
├────────────────────────────────────────────────────────────────────────┤
│  LAYER B: Front-Buffered Layer (Live Wet Inking)                       │
│  • Bypasses SurfaceFlinger composition queues (<2ms latency)           │
│  • Streams 240Hz/480Hz digitizer packets directly to glass             │
│  • Managed by: LayerManager::RenderLiveInking()                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. How to Tie This Backend to Your System

Integrating this backend into any multiplatform Android application requires 3 simple steps:

### Step 1: Add the Java View to Your Layout

In your Android Activity or XML layout, add `FolioInkView`:

```xml
<org.foliosketch.android.FolioInkView
    android:id="@+id/ink_view"
    android:layout_width="match_parent"
    android:layout_height="match_parent" />
```

Or programmatically in an `Activity`:
```java
FolioInkView inkView = new FolioInkView(this);
setContentView(inkView);
```

`FolioInkView` automatically:
1. Requests unbuffered dispatch for stylus (`requestUnbufferedDispatch`).
2. Drains historical batch samples from the digitizer (up to 480Hz).
3. Connects to `GLFrontBufferedRenderer` for hardware scanout.

---

### Step 2: Bridge Input to C++ (`folio_android_jni.cpp`)

Android `MotionEvent`s arrive in `FolioInkView::onTouchEvent` and are forwarded via `FolioNativeBridge` into C++:

```cpp
JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeIngestPacket(
    ..., jfloat x, jfloat y, jfloat pressure, jfloat tiltX, jfloat tiltY,
    jlong timestampNs, jboolean isDown, jboolean isUp, jboolean isStylus)
{
    Folio::V2::PointerPacket pkt;
    pkt.device = isStylus ? Folio::V2::DeviceType::Stylus : Folio::V2::DeviceType::Touch;
    pkt.phase  = isDown ? Folio::V2::InputPhase::Down :
                 isUp   ? Folio::V2::InputPhase::Up   : Folio::V2::InputPhase::Move;
    pkt.x = x;
    pkt.y = y;
    pkt.pressure = pressure;
    pkt.tiltX = tiltX;
    pkt.tiltY = tiltY;
    pkt.timestampSec = static_cast<double>(timestampNs) * 1e-9;

    session->layerManager.ProcessPointer(pkt);
}
```

The C++ [`LayerManager`](file:///home/odysseus/Documents/GitHub/Folio-Sketch/inspectorV2.0/layer_manager.hpp) automatically applies the routing rules:
- **Stylus:** Routes directly to `Folio::InkEngine` (Physics modeler $\to$ Gaussian smoothing $\to$ Kalman prediction).
- **Fingers:** Ignored for drawing. Two-finger gestures pan/zoom the canvas.
- **Palm Rejection:** While the stylus is touching or hovering, all finger touches are dropped.

---

### Step 3: Render Wet & Dry Passes

When `GLFrontBufferedRenderer` requests draw callbacks, `FolioInkView` calls native render functions:

#### A. Live Wet Inking (Sub-2ms Scanout Overlay)
```cpp
JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderWet(
    JNIEnv* env, jclass clazz, jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    // Renders live modeled + predicted points straight to front-buffer
    session->layerManager.RenderLiveInking(width, height, matrixElements);
}
```

#### B. Committed Document Canvas (Multi-Buffered)
```cpp
JNIEXPORT void JNICALL
Java_org_foliosketch_android_FolioNativeBridge_nativeRenderDry(
    JNIEnv* env, jclass clazz, jlong handle, jint width, jint height, jfloatArray surfaceTransform)
{
    // Clears canvas to paper color and redraws all committed document strokes
    session->layerManager.RenderDocumentCanvas(width, height, matrixElements);
}
```

---

## 4. Coordinate Spaces & Nib Alignment

To ensure ink is 100% glued to the physical pen nib without translation or drift:
1. `InputManager` receives raw screen pixels $(x_{\text{screen}}, y_{\text{screen}})$.
2. `LayerManager` unprojects screen coordinates into document world coordinates:
   $$x_{\text{world}} = \frac{x_{\text{screen}} - \text{panX}}{\text{zoom}}$$
3. `GlCapsuleRenderer` projects world coordinates back to screen NDC in the GPU vertex shader:
   $$\text{screenPos} = x_{\text{world}} \times \text{zoom} + \text{panX} \equiv x_{\text{screen}}$$
4. Single-finger resting palms are blocked from modifying $\text{panX}$ / $\text{panY}$, preventing nib drift.

---

## 5. Building & Deploying the Android Inspector App

From the project root:

```bash
cd inspectorV2.0/android
./gradlew installDebug
```

This compiles:
- `libmain.so` (containing `folio_sketch`, `folio_backends`, and `inspectorV2.0`)
- `FolioInkView` and the Android application
- Deploys directly to your connected Android tablet or phone via ADB.
