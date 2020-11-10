package org.example.viotester;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraManager;
import android.location.LocationManager;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.Size;
import android.view.Display;
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

    private CameraWorker mCameraWorker = null;

    private DataRecorder mDataRecorder;
    protected AlgorithmWorker.Settings mAlgoWorkerSettings;

    // adjustable flags for subclasses
    protected boolean mDirectCameraPreview = false;
    protected String mRecordPrefix = "";
    protected String mNativeModule;
    // TODO: bad, refactor
    protected boolean mDataCollectionMode = false;

    protected boolean mUseCameraWorker = false;

    protected void adjustSettings(AlgorithmWorker.Settings settings) {}

    protected void logExternalPoseMatrix(long timeNs, float[] viewMtx) {
        mAlgorithmWorker.logExternalPoseMatrix(timeNs, viewMtx, mRecordPrefix);
    }

    protected void logExternalImage(int textureId, long timeNanos, long frameNumber, int cameraInd,
                                    int[] dimensions, float[] focalLength, float[] principalPoint) {
        mAlgorithmWorker.logExternalImage(textureId, timeNanos, frameNumber, cameraInd,
                dimensions[0], dimensions[1],
                0.5f * (focalLength[0] + focalLength[1]),
                principalPoint[0], principalPoint[1]);
    }

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        Log.d(TAG, "onSurfaceCreated");
        mCameraWorker.start();
    }

    @Override
    public void onSurfaceChanged(GL10 gl10, int w, int h) {
        Log.d(TAG, "onSurfaceChanged");
        mAlgorithmWorker.onSurfaceChanged(w, h);
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
        // AlgorithmRenderer.onDrawFrame(gl10);
        mCameraWorker.onFrame();
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
        Log.d(TAG, "onCreate");

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

        mVisuUpdater = new VisualizationUpdater();
        SensorManager sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager == null) throw new RuntimeException("could not access SensorManager");

        System.loadLibrary("vio_main");

        mAlgoWorkerSettings = parseSettings(prefs, getWindowManager().getDefaultDisplay());

        final boolean recordAny = mAlgoWorkerSettings.recordPoses || mAlgoWorkerSettings.recordSensors;

        if (recordAny) {
            mDataRecorder = new DataRecorder(
                    getExternalCacheDir(),
                    mRecordPrefix,
                    prefs.getBoolean("compress_to_archive", true));
            mAlgoWorkerSettings.recordingFileName = mDataRecorder.getLogFileName();
            mAlgoWorkerSettings.infoFileName = mDataRecorder.getInfoFileName();
            mAlgoWorkerSettings.parametersFileName = mDataRecorder.getParametersFileName();
        }
        if (mAlgoWorkerSettings.recordSensors) {
            mAlgoWorkerSettings.videoRecordingFileName = mDataRecorder.getVideoFileName();
        }

        final boolean showDebugText = prefs.getBoolean("show_text_debug", true);
        mAlgorithmWorker = new AlgorithmWorker(sensorManager,
                (LocationManager) getSystemService(android.content.Context.LOCATION_SERVICE),
                mAlgoWorkerSettings, new AlgorithmWorker.Listener() {
            @Override
            public void onStats(String stats, int trackingStatus) {
                if (showDebugText) {
                    mVisuUpdater.setAlgoStatsText(stats);
                } else  {
                    if (trackingStatus == 2) // 2 Lost tracking
                        mVisuUpdater.setAlgoStatsText("Lost tracking");
                    else // 0 Init, 1 Tracking
                        mVisuUpdater.setAlgoStatsText("");
                }
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
            public void onAvailableFps(List<String> fpsList) {
                PreferenceManager.getDefaultSharedPreferences(AlgorithmActivity.this )
                        .edit()
                        .putStringSet("fps_set", new TreeSet<>(fpsList))
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
        }, mRecordPrefix);

        CameraManager cameraManager = (CameraManager) getSystemService(Context.CAMERA_SERVICE);
        if (cameraManager == null) throw new RuntimeException("could not access CameraManager");
        mCameraWorker = new CameraWorker(cameraManager, mAlgorithmWorker, mAlgoWorkerSettings.targetFps);

        if (mDirectCameraPreview) {
            mGlSurfaceView.setVisibility(View.GONE);
            mGlSurfaceView = null;
        }
        else {
            // set up GL rendering
            mGlSurfaceView.setPreserveEGLContextOnPause(true);
            mGlSurfaceView.setEGLContextClientVersion(3);
            // TODO: drop depth buffer in non-AR mode
            mGlSurfaceView.setEGLConfigChooser(8, 8, 8, 8, 16, 0);
            mGlSurfaceView.setRenderer(this);
            mGlSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        }
    }

    private AlgorithmWorker.Settings parseSettings(
            SharedPreferences prefs,
            Display display)
    {
        AlgorithmWorker.Settings s = new AlgorithmWorker.Settings();

        s.targetCamera = prefs.getString("target_camera", "0");

        s.halfFps = prefs.getBoolean("half_fps", false);
        s.targetFps = Integer.parseInt(prefs.getString("fps", "15"));
        if (s.halfFps) s.targetFps /= 2;

        Size def = new Size(1280,720);
        String sizeString = prefs.getString("target_size", def.getWidth() + "x" + def.getHeight());
        Log.d(TAG, "target size " + sizeString);
        // no validation, add if necessary
        String[] dims = sizeString.split("x");

        s.targetImageSize = new Size(Integer.parseInt(dims[0]), Integer.parseInt(dims[1]));

        s.useCalibAcc = prefs.getBoolean("use_calib_acc", true);
        s.useCalibGyro = prefs.getBoolean("use_calib_gyro", true);

        s.moduleName = mNativeModule;

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

        s.allPrefs = prefs.getAll();

        adjustSettings(s);
        return s;
    }

    @Override
    public void onPause()
    {
        Log.d(TAG, "onPause");
        super.onPause();
        mAlgorithmWorker.stop();
        if (mGlSurfaceView != null) mGlSurfaceView.onPause();
    }

    @Override
    public void onResume()
    {
        // TODO: Pausing&resuming doesn't seem to actually work, preview stays frozen

        Log.d(TAG, "onResume");
        super.onResume();
        if (mGlSurfaceView != null) mGlSurfaceView.onResume();
        mAlgorithmWorker.start(); // after System.loadLibrary

    }

    @Override
    public void onDestroy() {
        Log.d(TAG, "onDestroy");
        super.onDestroy();
        if (mCameraWorker != null) mCameraWorker.stop();
        if (mDataRecorder != null) {
            mDataRecorder.flush();
            mDataRecorder = null;
        }
    }
}
