package org.example.viotester;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class AlgorithmRenderer {
    public static native void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig);
    public static native void onSurfaceChanged(GL10 gl10, int w, int h);
    public static native void onDrawFrame(GL10 gl10);
}
