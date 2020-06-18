package org.example.viotester.arengine;

import android.media.Image;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;

import com.huawei.hiar.ARCamera;
import com.huawei.hiar.AREnginesSelector;
import com.huawei.hiar.ARFrame;
import com.huawei.hiar.ARImageMetadata;
import com.huawei.hiar.ARSession;
import com.huawei.hiar.AREnginesApk;
import com.huawei.hiar.ARTrackable;
import com.huawei.hiar.ARWorldTrackingConfig;
import com.huawei.hiar.ARPointCloud;

import java.io.IOException;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import org.example.viotester.AlgorithmActivity;
import org.example.viotester.PermissionHelper;
import org.example.viotester.ext_ar.Renderer;

// See the comment in ARCoreActivity about the Gradle bug if you get weird errors
// from building this class
public class AREngineActivity extends AlgorithmActivity implements GLSurfaceView.Renderer {
    private static final String TAG = AREngineActivity.class.getName();

    private ARSession mSession;
    private boolean mInstallRequested;
    private DisplayRotationHelper mDisplayRotationHelper;
    private Renderer mNativeRenderer = new Renderer();
    private final BackgroundRenderer mBackgroundRenderer = new BackgroundRenderer();

    private ARPointCloud mARPointCloud = null;
    private float[] mPointCloudBuffer = null;
    private float[] mPointCloudOutBuffer = null;

    private long frameNumber = 0;

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        try {
            mBackgroundRenderer.createOnGlThread(/*context=*/ this);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        mNativeRenderer.onSurfaceCreated(gl10, eglConfig);
    }


    @Override
    public void onSurfaceChanged(GL10 gl10, int w, int h) {
        mDisplayRotationHelper.onSurfaceChanged(w, h);
        GLES20.glViewport(0, 0, w, h);
        mNativeRenderer.onSurfaceChanged(gl10, w, h);
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
        // Clear screen to notify driver it should not load any pixels from previous frame.
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);

        if (mSession == null) {
            return;
        }
        // Notify ARCore session that the view size changed so that the perspective matrix and
        // the video background can be properly adjusted.
        mDisplayRotationHelper.updateSessionIfNeeded(mSession);

        try {
            mSession.setCameraTextureName(mBackgroundRenderer.getTextureId());

            // Obtain the current frame from ARSession. When the configuration is set to
            // UpdateMode.BLOCKING (it is by default), this will throttle the rendering to the
            // camera framerate.
            ARFrame frame = mSession.update();
            ARCamera camera = frame.getCamera();

            // If frame is ready, render camera preview image to the GL surface.
            mBackgroundRenderer.draw(frame);

            // If not tracking, don't draw 3D objects, show tracking failure reason instead.
            if (camera.getTrackingState() == ARTrackable.TrackingState.PAUSED) {
                return;
            }

            // Get projection matrix.
            float[] projmtx = new float[16];
            camera.getProjectionMatrix(projmtx, 0, 0.1f, 100.0f);

            // Send image to native code for recording
            Image image = frame.acquireCameraImage();
            float focalLength = projmtx[0] * image.getWidth() / 2.f;
            logExternalImage(image, frameNumber++, 0, focalLength, -1.f, -1.f);
            // While ARCore requires this, AREngine works without. However, to ensure future
            // compatibility, it feels safer to call it if AREngine gets more aligned with ARCore
            // in the future.
            image.close();

            // Get camera matrix and draw.
            float[] viewmtx = new float[16];
            camera.getViewMatrix(viewmtx, 0);

            // Get point cloud
            ARPointCloud pointCloud = frame.acquirePointCloud();

            // NOTE: this check is actually different from ARCore: If the point cloud has not
            // changed, AREngine returns the same object/pointer, while in ARCore, you are supposed
            // to check the timestamp
            if (pointCloud != mARPointCloud) {
                // Log.v(TAG, "Point cloud updated");
                mARPointCloud = pointCloud;

                final int ARCORE_FLOATS_PER_POINT = 4;
                final int OUT_FLOATS_PER_POINT = 3;
                final int nElements = pointCloud.getPoints().remaining();
                if (mPointCloudBuffer == null || mPointCloudOutBuffer.length < nElements) {
                    // allocate a bit more than required now so this does not have to be
                    // reallocated very often
                    final int newSize = nElements * 2;
                    // Log.d(TAG, "Allocating point cloud buffer of size " + newSize);
                    mPointCloudBuffer = new float[newSize];
                    mPointCloudOutBuffer = new float[nElements/ARCORE_FLOATS_PER_POINT*OUT_FLOATS_PER_POINT];
                }
                pointCloud.getPoints().get(mPointCloudBuffer, 0, nElements);
                final int nPoints = nElements/ARCORE_FLOATS_PER_POINT;
                for (int i=0; i < nPoints; ++i) {
                    final int inOffs = i*ARCORE_FLOATS_PER_POINT;
                    final int outOffs = i*OUT_FLOATS_PER_POINT;
                    for (int j = 0; j < OUT_FLOATS_PER_POINT; ++j) {
                        mPointCloudOutBuffer[outOffs + j] = mPointCloudBuffer[inOffs + j];
                    }
                }
                mNativeRenderer.setPointCloud(mPointCloudOutBuffer, nPoints*OUT_FLOATS_PER_POINT);
            }

            long timeNs = frame.getTimestampNs();
            logExternalPoseMatrix(timeNs, viewmtx);
            mNativeRenderer.onDrawFrame(timeNs * 1e-9, viewmtx, projmtx);

        } catch (Throwable t) {
            // Avoid crashing the application due to unhandled exceptions.
            Log.e(TAG, "Exception on the OpenGL thread", t);
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        mRecordPrefix = "arengine";
        mNativeModule = "external";

        super.onCreate(savedInstanceState);

        mDisplayRotationHelper = new DisplayRotationHelper(/*context=*/ this);
        mGlSurfaceView.setWillNotDraw(false); // TODO what is this?
        mInstallRequested = false;
    }

    @Override
    public void onPause()
    {
        Log.d(TAG, "onPause");
        super.onPause();
        if (mSession != null) mSession.pause();
    }

    @Override
    public void onResume()
    {
        Log.d(TAG, "onResume");
        super.onResume();

        try {

            if (null == mSession) {
                //If you do not want to switch engines, AREnginesSelector is useless.
                // You just need to use AREnginesApk.requestInstall() and the default engine
                // is Huawei AR Engine.
                AREnginesSelector.AREnginesAvaliblity enginesAvaliblity = AREnginesSelector.checkAllAvailableEngines(this);
                if ((enginesAvaliblity.ordinal()
                        & AREnginesSelector.AREnginesAvaliblity.HWAR_ENGINE_SUPPORTED.ordinal()) != 0) {

                    AREnginesSelector.setAREngine(AREnginesSelector.AREnginesType.HWAR_ENGINE);

                    Log.d(TAG, "installRequested:" + mInstallRequested);
                    switch (AREnginesApk.requestInstall(this, !mInstallRequested)) {
                        case INSTALL_REQUESTED:
                            Log.d(TAG, "INSTALL_REQUESTED");
                            mInstallRequested = true;
                            return;
                        case INSTALLED:
                            break;
                    }

                    if (!PermissionHelper.havePermissions(this))
                        throw new IllegalStateException("should have permissions at this point");

                    mSession = new ARSession(/*context=*/this);
                    ARWorldTrackingConfig config = new ARWorldTrackingConfig(mSession);

                    int supportedSemanticMode = mSession.getSupportedSemanticMode();
                    Log.d(TAG, "supportedSemanticMode:" + supportedSemanticMode);

                    if (supportedSemanticMode != ARWorldTrackingConfig.SEMANTIC_NONE) {
                        Log.d(TAG, "supported mode:" + supportedSemanticMode);
                        config.setSemanticMode(supportedSemanticMode);
                        final int modeSet = config.getSemanticMode();
                        Log.d(TAG, "supportedSemanticMode:" + modeSet);
                    }

                    mSession.configure(config);
                } else {
                    throw new RuntimeException("This device does not support Huawei AR Engine");
                }
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        mDisplayRotationHelper.onResume();
    }
}
