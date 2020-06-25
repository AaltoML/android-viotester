package org.example.viotester.arcore;

import android.media.Image;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;
import android.util.Size;

import com.google.ar.core.ArCoreApk;
import com.google.ar.core.Camera;
import com.google.ar.core.CameraConfig;
import com.google.ar.core.CameraConfigFilter;
import com.google.ar.core.CameraIntrinsics;
import com.google.ar.core.Frame;
import com.google.ar.core.PointCloud;
import com.google.ar.core.Session;
import com.google.ar.core.TrackingState;

import java.io.IOException;
import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import org.example.viotester.PermissionHelper;
import org.example.viotester.ext_ar.Renderer;
import org.example.viotester.AlgorithmActivity;

// Unfortunately, due to a bug in Gradle, https://github.com/gradle/gradle/issues/9202
// sometimes modifying this file results to a "Unable to find source java class" error when
// used through a symlink, as in the ARCore & AREngine activity. The workaround is to
// "Rebuild project" with Gradle OR just deleted the folder app/build/intermediates/javac
public class ARCoreActivity extends AlgorithmActivity implements GLSurfaceView.Renderer {
    private static final String TAG = ARCoreActivity.class.getName();

    private Session mArCoreSession;
    private boolean mArCoreInstallRequested;
    private DisplayRotationHelper mDisplayRotationHelper;

    private final BackgroundRenderer mBackgroundRenderer = new BackgroundRenderer();
    private Renderer mNativeRenderer = new Renderer();

    private long mLastPointCloudTimestamp = 0;
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

        if (mArCoreSession == null) {
            return;
        }
        // Notify ARCore session that the view size changed so that the perspective matrix and
        // the video background can be properly adjusted.
        mDisplayRotationHelper.updateSessionIfNeeded(mArCoreSession);

        try {
            mArCoreSession.setCameraTextureName(mBackgroundRenderer.getTextureId());

            // Obtain the current frame from ARSession. When the configuration is set to
            // UpdateMode.BLOCKING (it is by default), this will throttle the rendering to the
            // camera framerate.
            Frame frame = mArCoreSession.update();
            Camera camera = frame.getCamera();

            // If frame is ready, render camera preview image to the GL surface.
            mBackgroundRenderer.draw(frame);

            // Get projection matrix.
            float[] projmtx = new float[16];
            camera.getProjectionMatrix(projmtx, 0, 0.1f, 100.0f);

            // Send image to native code for recording
            Image image = frame.acquireCameraImage();
            CameraIntrinsics intrinsics = camera.getImageIntrinsics();
            float focalLength = projmtx[0] * image.getWidth() / 2.f;
            // intrinsics.getFocalLength()[0] is the same, but let's use same method as AREngine version
            logExternalImage(image, frameNumber++, 0, focalLength,
                    intrinsics.getPrincipalPoint()[0], intrinsics.getPrincipalPoint()[1]);
            image.close(); // Release image, otherwise it's kept and we run out of memory

            // If not tracking, don't draw 3D objects, show tracking failure reason instead.
            if (camera.getTrackingState() == TrackingState.PAUSED) {
                return;
            }

            // Get camera matrix and draw.
            float[] viewmtx = new float[16];
            camera.getViewMatrix(viewmtx, 0);

            // Get point cloud
            try (PointCloud pointCloud = frame.acquirePointCloud()) {
                if (pointCloud.getTimestamp() != mLastPointCloudTimestamp) {
                    // Log.v(TAG, "Point cloud updated");
                    mLastPointCloudTimestamp = pointCloud.getTimestamp();

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
            }

            long timeNs = frame.getTimestamp();
            logExternalPoseMatrix(timeNs, viewmtx);
            mNativeRenderer.onDrawFrame(timeNs * 1e-9, viewmtx, projmtx);

        } catch (Throwable t) {
            // Avoid crashing the application due to unhandled exceptions.
            Log.e(TAG, "Exception on the OpenGL thread", t);
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        mRecordPrefix = "arcore";
        mNativeModule = "external";

        super.onCreate(savedInstanceState);

        mDisplayRotationHelper = new DisplayRotationHelper(/*context=*/ this);
        mGlSurfaceView.setWillNotDraw(false); // TODO what is this?

        mArCoreInstallRequested = false;
    }

    @Override
    public void onPause()
    {
        Log.d(TAG, "onPause");
        super.onPause();
        if (mArCoreSession != null) mArCoreSession.pause();
    }

    @Override
    public void onResume()
    {
        Log.d(TAG, "onResume");
        super.onResume();

        try {
            if (mArCoreSession == null) {
                switch (ArCoreApk.getInstance().requestInstall(this, !mArCoreInstallRequested)) {
                    case INSTALL_REQUESTED:
                        mArCoreInstallRequested = true;
                        return;
                    case INSTALLED:
                        break;
                }

                if (!PermissionHelper.havePermissions(this))
                    throw new IllegalStateException("should have permissions by now");

                // Create the session.
                mArCoreSession = new Session(/* context= */ this);
            }

            CameraConfigFilter filter = new CameraConfigFilter(mArCoreSession);
            List<CameraConfig> cameraConfigList = mArCoreSession.getSupportedCameraConfigs(filter);
            for (CameraConfig config : cameraConfigList) {
                if (config.getImageSize().equals(mAlgoWorkerSettings.targetImageSize)) {
                    Log.d(TAG, "Founding matching camera config for resolution "
                            + config.getImageSize());
                    mArCoreSession.setCameraConfig(config);
                    break;
                }
            }

            mArCoreSession.resume();

        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        mDisplayRotationHelper.onResume();
    }
}
