package org.example.viotester.ext_ar;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class Renderer {
    public native void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig);
    public native void onSurfaceChanged(GL10 gl10, int w, int h);
    public native void onDrawFrame(double t, float[] arViewMatrix, float[] arProjMatrix, int w, int h);
    public native void setPointCloud(float[] pointCloudBuffer, int size);
}

