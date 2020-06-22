package org.example.viotester;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraManager;
import android.location.LocationManager;
import android.media.Image;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.Size;
import android.view.Display;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;

import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import androidx.preference.PreferenceManager;

public class AlgorithmActivity extends Activity implements GLSurfaceView.Renderer {
    private static final String TAG = AlgorithmActivity.class.getName();

    protected GLSurfaceView mGlSurfaceView;

    private TextView mStatsTextView;
    private AlgorithmWorker mAlgorithmWorker;
    private VisualizationUpdater mVisuUpdater;
    private CameraWorker mCameraWorker;
    private TextureView mDirectPreviewView;
    private DataRecorder mDataRecorder;
    private AlgorithmWorker.Settings mAlgoWorkerSettings;
    private final HandlerThread mHandlerThread;
    private final Handler mNativeHandler; // All native access should go through this

    // adjustable flags for subclasses
    protected boolean mRecordCamera = true;
    protected boolean mDirectCameraPreview = false;
    protected String mRecordPrefix = "";
    protected String mNativeModule;
    // TODO: bad, refactor
    protected boolean mDataCollectionMode = false;

    public AlgorithmActivity() {
        mHandlerThread = new HandlerThread("NativeHandler", Thread.MAX_PRIORITY);
        mHandlerThread.start();
        mNativeHandler = new Handler(mHandlerThread.getLooper());
    }

    protected void adjustSettings(AlgorithmWorker.Settings settings) {}

    protected void logExternalPoseMatrix(long timeNs, float[] viewMtx) {
        mAlgorithmWorker.logExternalPoseMatrix(timeNs, viewMtx, mRecordPrefix);
    }

    protected void logExternalImage(Image image, long frameNumber, int cameraInd,
                                    float focalLength, float px, float py) {
        mAlgorithmWorker.onImage(image, frameNumber, cameraInd, focalLength, px, py);
    }

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        AlgorithmRenderer.onSurfaceCreated(gl10, eglConfig);
    }

    @Override
    public void onSurfaceChanged(GL10 gl10, int w, int h) {
        AlgorithmRenderer.onSurfaceChanged(gl10, w, h);
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
        AlgorithmRenderer.onDrawFrame(gl10);
    }

    /**
     * Handles Android view updates in the correct thread
     */
    private class VisualizationUpdater {
        private Handler mUIHandler;
        private long mLastUpdate = 0;
        private static final long UPDATE_INTERVAL_MILLIS = 100;
        private String mAlgoText = "";

        VisualizationUpdater() {
            mUIHandler = new Handler();
        }

        void setAlgoStatsText(String text) {
            mAlgoText = text;
            updateText();
        }

        // this must always be called from the same thread (or be changed to synchronized),
        // this is just to a small optimization that avoids pushing a new runnable to the
        // queue on each frame
        private void updateText() {
            long t = System.currentTimeMillis();
            if (t > mLastUpdate + UPDATE_INTERVAL_MILLIS) {
                mUIHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        //Log.d(TAG, "(thread " + android.os.Process.myTid() + ") updating the UI");
                        mStatsTextView.setText(mAlgoText);
                    }
                });
                mLastUpdate = t;
            }
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // see https://developer.android.com/training/system-ui/immersive
        getWindow().getDecorView().setSystemUiVisibility(
            // Set the content to appear under the system bars so that the
            // content doesn't resize when the system bars hide and show.
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            // Hide the nav bar and status bar
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN);

        // add View.SYSTEM_UI_FLAG_IMMERSIVE to keep navigation hidden even when tapping

        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);

        setContentView(R.layout.viotester_surface_view);
        mStatsTextView = findViewById(R.id.stats_text_view);
        mGlSurfaceView = findViewById(R.id.gl_surface_view);

        if (mDirectCameraPreview) {
            mGlSurfaceView.setVisibility(View.GONE);
            mGlSurfaceView = null;
        }
        else {
            // set up GL rendering
            mGlSurfaceView.setPreserveEGLContextOnPause(true);
            mGlSurfaceView.setEGLContextClientVersion(2);
            mGlSurfaceView.setEGLConfigChooser(8, 8, 8, 8, 16, 0);
            mGlSurfaceView.setRenderer(this);
            mGlSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        }

        mVisuUpdater = new VisualizationUpdater();
        SensorManager sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager == null) throw new RuntimeException("could not access SensorManager");

        System.loadLibrary("vio_main");

        mAlgoWorkerSettings = parseSettings(prefs, getWindowManager().getDefaultDisplay());
        final boolean recordAny = mAlgoWorkerSettings.recordPoses || mAlgoWorkerSettings.recordSensors;

        if (recordAny) {
            mDataRecorder = new DataRecorder(getExternalCacheDir(), mRecordPrefix);
            mAlgoWorkerSettings.recordingFileName = mDataRecorder.getLogFileName();
        }
        if (mAlgoWorkerSettings.recordSensors) {
            mAlgoWorkerSettings.videoRecordingFileName = mDataRecorder.getVideoFileName();
        }

        final boolean showDebugText = prefs.getBoolean("show_text_debug", true);
        mAlgorithmWorker = new AlgorithmWorker(sensorManager,
                (LocationManager) getSystemService(android.content.Context.LOCATION_SERVICE),
                mAlgoWorkerSettings, new AlgorithmWorker.Listener() {
            @Override
            public void onStats(String stats) {
                if (showDebugText)
                    mVisuUpdater.setAlgoStatsText(stats);
            }

            @Override
            public void onAvailableSizes(Size[] sizes) {
                Set<String> resolutionSet = new TreeSet<>();
                for (Size s : sizes) {
                    resolutionSet.add(s.getWidth() + "x" + s.getHeight());
                }
                PreferenceManager.getDefaultSharedPreferences(AlgorithmActivity.this )
                        .edit()
                        .putStringSet("resolution_set", resolutionSet)
                        .apply();
            }

            @Override
            public void onAvailableCameras(List<String> cameras) {
                PreferenceManager.getDefaultSharedPreferences(AlgorithmActivity.this )
                        .edit()
                        .putStringSet("camera_set", new TreeSet<>(cameras))
                        .apply();
            }

            @Override
            public void onRelativeFocalLength(double relFocalLength) {
                PreferenceManager.getDefaultSharedPreferences(AlgorithmActivity.this )
                        .edit()
                        .putString("focal_length_1280", "" + (relFocalLength*1280))
                        .putBoolean("has_auto_focal_length", true)
                        .apply();
            }
        }, mNativeHandler);
    }

    private AlgorithmWorker.Settings parseSettings(
            SharedPreferences prefs,
            Display display)
    {
        AlgorithmWorker.Settings s = new AlgorithmWorker.Settings();

        String videoVisuString = prefs.getString("visualization", "plain_video").toUpperCase();
        try {
            s.videoVisualization =  AlgorithmWorker.Settings.VideoVisualization.valueOf(videoVisuString);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "unknown video visualization " + videoVisuString);
            s.videoVisualization = AlgorithmWorker.Settings.VideoVisualization.NONE;
        }

        String overlayVisuString = prefs.getString("overlay_visualization", "track").toUpperCase();
        try {
            s.overlayVisualization =  AlgorithmWorker.Settings.OverlayVisualization.valueOf(overlayVisuString);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "unknown overlay visualization " + overlayVisuString);
            s.overlayVisualization = AlgorithmWorker.Settings.OverlayVisualization.NONE;
        }

        s.targetCamera = prefs.getString("target_camera", "0");
        s.targetFps = Integer.parseInt(prefs.getString("fps", "15"));

        Size def = new Size(1280,720);
        String sizeString = prefs.getString("target_size", def.getWidth() + "x" + def.getHeight());
        Log.d(TAG, "target size " + sizeString);
        // no validation, add if necessary
        String[] dims = sizeString.split("x");

        s.targetImageSize = new Size(Integer.parseInt(dims[0]), Integer.parseInt(dims[1]));

        s.useCalibAcc = prefs.getBoolean("use_calib_acc", true);
        s.useCalibGyro = prefs.getBoolean("use_calib_gyro", true);

        s.moduleName = mNativeModule;
        s.trackerOnly = !prefs.getBoolean("enable_odometry", true);

        s.fastMode = prefs.getBoolean("fast_mode", false);
        s.jumpFilter = prefs.getBoolean("jump_filter", true);

        String focalLengthString = prefs.getString("focal_length_1280", "-1");
        try {
            float focalLength = Float.parseFloat(focalLengthString);
            if (focalLength > 0) {
                s.relativeFocalLength = focalLength / 1280;
            }
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "invalid focal length string " + focalLengthString);
        }

        // screen size
        DisplayMetrics displayMetrics = new DisplayMetrics();
        display.getMetrics(displayMetrics);
        s.screenWidth = displayMetrics.widthPixels;
        s.screenHeight = displayMetrics.heightPixels;

        s.orbVocabularyFilename = null;

        if (BuildConfig.USE_SLAM && prefs.getBoolean("enable_slam", true) && prefs.getBoolean(AssetCopier.HAS_SLAM_FILES_KEY, false)) {
            s.orbVocabularyFilename = prefs.getString(AssetCopier.ASSET_PATH_KEY_ORB_VOCABULARY, null);
        }

        final boolean trackingMode = !mDataCollectionMode;
        s.recordCamera =  !trackingMode || prefs.getBoolean("record_tracking_sensors_and_video", false);
        s.recordPoses = mDataCollectionMode || (trackingMode && prefs.getBoolean("record_tracking_poses", false));
        s.recordSensors = mDataCollectionMode || (trackingMode && prefs.getBoolean("record_tracking_sensors_and_video", false));
        final boolean recordingSomething = mDataCollectionMode || s.recordPoses;
        s.recordGps = recordingSomething && prefs.getBoolean("record_gps", false);
        s.recordWiFiLocations = recordingSomething && prefs.getBoolean("record_google_wifi_locations", false);
        s.recordingOnly = mDataCollectionMode;

        adjustSettings(s);
        return s;
    }

    @Override
    public void onPause()
    {
        Log.d(TAG, "onPause");
        super.onPause();
        if (mGlSurfaceView != null) mGlSurfaceView.onPause();
        mAlgorithmWorker.stop();
        if (mCameraWorker != null) {
            mCameraWorker.destroy();
        }
    }

    @Override
    public void onResume()
    {
        // TODO: Pausing&resuming doesn't seem to actually work, preview stays frozen

        Log.d(TAG, "onResume");
        super.onResume();
        if (mGlSurfaceView != null) mGlSurfaceView.onResume();

        if (mCameraWorker == null && mDataCollectionMode) {
            CameraManager cameraManager = (CameraManager) getSystemService(Context.CAMERA_SERVICE);
            if (cameraManager == null) throw new RuntimeException("could not access CameraManager");
            mCameraWorker = new CameraWorker(
                    cameraManager,
                    mDirectPreviewView,
                    mAlgorithmWorker,
                    mNativeHandler);

        }
        mAlgorithmWorker.start(); // after System.loadLibrary
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        mHandlerThread.quitSafely();
        try {
            mHandlerThread.join();
        } catch (InterruptedException e) {
            Log.e(TAG, "Failed to join native access handler thread", e);
        }
        if (mDataRecorder != null) {
            mDataRecorder.flush();
            mDataRecorder = null;
        }
    }
}
