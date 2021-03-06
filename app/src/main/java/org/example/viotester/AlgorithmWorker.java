package org.example.viotester;

import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.GnssClock;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.location.GnssMeasurementsEvent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import android.util.Size;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;

import org.codehaus.jackson.map.ObjectMapper;

public class AlgorithmWorker implements SensorEventListener, CameraWorker.Listener {
    private static final String TAG = AlgorithmWorker.class.getName();
    private static final boolean SUPPORTS_GNSS_CLOCK = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q;

    public interface Listener {
        void onOutput(TrackingOutput output);
        void onAvailableSizes(Size[] sizes);
        void onAvailableCameras(List<String> cameras);
        void onAvailableFps(List<String> fps);
        void onRelativeFocalLength(float relativeFocalLengthX, float relativeFocalLengthY);
        void onGpsLocationChange(double time, double latitude, double longitude, double altitude, float accuracy);
    }

    public static class Settings {
        public String moduleName;

        public int targetFps;
        public Size targetImageSize;
        public String targetCamera;
        public Float relativeFocalLengthX;
        public Float relativeFocalLengthY;
        public boolean halfFps;
        public boolean useCalibAcc;
        public boolean useCalibGyro;

        public boolean recordCamera;
        public boolean recordSensors;
        public boolean recordPoses;

        public int screenWidth;
        public int screenHeight;

        @Nullable
        public String orbVocabularyFilename;
        @Nullable
        public String recordingFileName;
        @Nullable
        public String infoFileName;
        @Nullable
        public String parametersFileName;
        @Nullable
        public String videoRecordingFileName;
        @Nullable
        public String filesDir;

        public boolean recordingOnly = false;
        public boolean recordGps = false;
        public boolean recordWiFiLocations = false;

        // Do not change these directly
        public float focalLengthX = -1;
        public float focalLengthY = -1;
        public float principalPointX = -1;
        public float principalPointY = -1;

        // All SharedPreferences, including generic, possibly module-specific settings
        public Map<String, ?> allPrefs;
    }

    private class GpsListener extends GnssMeasurementsEvent.Callback implements LocationListener {
        private final LocationManager locationManager;

        GpsListener(LocationManager m) {
            locationManager = m;
        }

        void start() {
            try {
            if (mSettings.recordGps) {
                locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 0, this);
                if (SUPPORTS_GNSS_CLOCK) locationManager.registerGnssMeasurementsCallback(this);
            }

            if (mSettings.recordWiFiLocations)
                locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 0, 0, this);
            } catch (SecurityException e) {
                // should only happen if permissions have been modified after app launch
                throw new RuntimeException(e);
            }
        }

        void stop() {
            if (mSettings.recordGps && SUPPORTS_GNSS_CLOCK) locationManager.unregisterGnssMeasurementsCallback(this);
            if (mSettings.recordGps || mSettings.recordWiFiLocations) {
                locationManager.removeUpdates(this);
            }
        }

        @RequiresApi(api = Build.VERSION_CODES.Q)
        @Override
        public void onGnssMeasurementsReceived(GnssMeasurementsEvent event) {
            Log.d(TAG, "onGnssMeasurementsReceived");
            GnssClock clock = event.getClock();
            if (clock.hasBiasNanos() && clock.hasFullBiasNanos() && clock.hasLeapSecond() && clock.hasElapsedRealtimeNanos()) {
                // Use elapsedRealtimeNanos from GnssClock
                long elapsedRealtimeNanos = clock.getElapsedRealtimeNanos();
                // Difference between Unix time epoch (01 Jan 1970) and GPS time epoch (06 Jan 1980) is 315964800 seconds
                final long fromGpsToEpcohSeconds = 315964800;
                // Formula: https://developer.android.com/reference/kotlin/android/location/GnssClock#getleapsecond
                double biasNanos = clock.getBiasNanos();
                long fullBiasNanos = clock.getFullBiasNanos();
                long timeNanos  = clock.getTimeNanos();
                int leapSecond = clock.getLeapSecond();
                double utcGpsTimeSeconds = (timeNanos - (fullBiasNanos + biasNanos) - leapSecond * 1e9) * 1e-9 + fromGpsToEpcohSeconds;
                processGpsTime(elapsedRealtimeNanos, utcGpsTimeSeconds);
            }
        }

        @Override
        public void onLocationChanged(final Location location) {
            final long t = SystemClock.elapsedRealtimeNanos();
            Log.d(TAG, "Location: " + location.getLatitude() + ", " + location.getLongitude() + " (accuracy " + location.getAccuracy() + "m)");
            if (mHandlerThread.isAlive()) {
                mSensorHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        processGpsLocation(t, location.getLatitude(), location.getLongitude(), location.getAltitude(), location.getAccuracy());
                    }
                });
            }
            mListener.onGpsLocationChange(
                    convertTime(t),
                    location.getLatitude(), location.getLongitude(),
                    location.getAltitude(), location.getAccuracy());
        }

        @Override
        public void onStatusChanged(String s, int i, Bundle bundle) {
            Log.d(TAG, "location status changed " + s);
        }

        @Override
        public void onProviderEnabled(String s) {
            Log.d(TAG, "location provider enabled " + s);
        }

        @Override
        public void onProviderDisabled(String s) {
            Log.d(TAG, "location provider disabled " + s);
        }
    }

    private final SensorManager mSensorManager;
    private final List<Sensor> mSensors;
    private HandlerThread mHandlerThread;
    private Handler mSensorHandler;
    private final Settings mSettings;
    private final Listener mListener;
    private final GpsListener mGpsListener;
    private final String mMode;

    private final FrequencyMonitor mAccMonitor;
    private final FrequencyMonitor mGyroMonitor;
    private final FrequencyMonitor mProcessedFpsMonitor;

    private CameraWorker.CameraParameters mCameraParameters = null;
    private int mScreenWidth = -1, mScreenHeight = -1;
    private boolean mExternalInitialized = false;
    private boolean running = false;

    //private final static int GYRO_SENSOR = Sensor.TYPE_GYROSCOPE_UNCALIBRATED;
    private final int mGyroSensor;
    private final int mAccSensor;

    public AlgorithmWorker(
            SensorManager sensorManager,
            LocationManager locationManager,
            Settings settings,
            Listener listener,
            String mode) {
        mSensorManager = sensorManager;

        mSettings = settings;
        mListener = listener;
        mMode = mode;

        mSensors = new ArrayList<>();
        List<Integer> sensorTypes = new ArrayList<>();

        mGyroSensor = mSettings.useCalibGyro ?
                Sensor.TYPE_GYROSCOPE :
                Sensor.TYPE_GYROSCOPE_UNCALIBRATED;
        mAccSensor = mSettings.useCalibAcc ?
                Sensor.TYPE_ACCELEROMETER :
                Sensor.TYPE_ACCELEROMETER_UNCALIBRATED; // TODO: Requires API level 26

        if (false) { // mSettings.recordExtraSensors
            // TODO: record these extra sensors too
            sensorTypes.add(Sensor.TYPE_GYROSCOPE);
            sensorTypes.add(Sensor.TYPE_GYROSCOPE_UNCALIBRATED);
            sensorTypes.add(Sensor.TYPE_ACCELEROMETER);
            sensorTypes.add(Sensor.TYPE_ACCELEROMETER_UNCALIBRATED);
            sensorTypes.add(Sensor.TYPE_MAGNETIC_FIELD);
            sensorTypes.add(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED);
            sensorTypes.add(Sensor.TYPE_PRESSURE);
        } else {
            sensorTypes.add(mGyroSensor);
            sensorTypes.add(mAccSensor);
        }

        for (int sensorType : sensorTypes) {
            mSensors.add(sensorManager.getDefaultSensor(sensorType));
        }

        mGpsListener = new GpsListener(locationManager);

        mAccMonitor = new FrequencyMonitor(new FrequencyMonitor.Listener() {
            @Override
            public void onFrequency(double freq) {
                Log.i(TAG, String.format("acc frequency %.3g Hz", freq));
            }
        });
        mGyroMonitor = new FrequencyMonitor(new FrequencyMonitor.Listener() {
            @Override
            public void onFrequency(double freq) {
                Log.i(TAG, String.format("gyro frequency %.3g Hz", freq));
            }
        });
        mProcessedFpsMonitor = new FrequencyMonitor(new FrequencyMonitor.Listener() {
            @Override
            public void onFrequency(double freq) {
                Log.i(TAG, String.format("processed FPS %.2g", freq));
            }
        });
    }

    synchronized public void start() {
        Log.d(TAG, "start");

        mHandlerThread = new HandlerThread("NativeHandler", Thread.MAX_PRIORITY);
        mHandlerThread.start();
        mSensorHandler = new Handler(mHandlerThread.getLooper());

        // always record sensors at max rate when
        final int sensorDelay = SensorManager.SENSOR_DELAY_FASTEST;
        for (Sensor sensor : mSensors) {
            mSensorManager.registerListener(this, sensor, sensorDelay, mSensorHandler);
        }
        mAccMonitor.start();
        mGyroMonitor.start();
        mProcessedFpsMonitor.start();
        mGpsListener.start();
        running = true;
    }

    synchronized public void stop() {
        Log.d(TAG, "stop");
        mSensorManager.unregisterListener(this);
        mAccMonitor.stop();
        mGyroMonitor.stop();
        mProcessedFpsMonitor.stop();
        mGpsListener.stop();
        mSensorHandler.post(new Runnable() {
            @Override
            public void run() {
                nativeStop();
            }
        });
        mExternalInitialized = false;
        running = false;

        mHandlerThread.quitSafely();
        try {
            mHandlerThread.join();
        } catch (InterruptedException e) {
            Log.e(TAG, "Failed to join native access handler thread", e);
        }
    }

    private String jsonSettings() {
        try {
            return new ObjectMapper().writeValueAsString(mSettings);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    public Size chooseSize(Size[] sizes) {
        if (mListener != null) mListener.onAvailableSizes(sizes);
        return mSettings.targetImageSize;
    }

    @Override
    public String chooseCamera(List<String> cameras) {
        if (mListener != null) mListener.onAvailableCameras(cameras);
        if (mSettings.targetCamera == null || cameras.indexOf(mSettings.targetCamera) < 0) {
            return cameras.get(0);
        }
        return mSettings.targetCamera;
    }

    @Override
    public void availableFpsRanges(List<String> fps) {
        mListener.onAvailableFps(fps);
    }

    @Override
    public void onCaptureStart(CameraWorker.CameraParameters parameters, int textureId) {
        mCameraParameters = parameters;
        int width = mCameraParameters.width;
        int height = mCameraParameters.height;

        mSettings.focalLengthX = -1;
        mSettings.focalLengthY = -1;
        if (mCameraParameters.focalLengthX > 0) {
            Log.i(TAG, "focal length from camera API: " + mCameraParameters.focalLengthX + ", " + mCameraParameters.focalLengthY);
            mSettings.focalLengthX = mCameraParameters.focalLengthX;
            mSettings.focalLengthY = mCameraParameters.focalLengthY;
            // note: both divided by width on purpose!
            mListener.onRelativeFocalLength(mSettings.focalLengthX  / width, mSettings.focalLengthY / width);
        }
        else if (mSettings.relativeFocalLengthX != null) {
            Log.i(TAG, "relative focal length from settings: " + mSettings.relativeFocalLengthX);
            mSettings.focalLengthX = mSettings.relativeFocalLengthX * width;
            mSettings.focalLengthY = mSettings.relativeFocalLengthY * width;
        } else {
            Log.i(TAG, "default focal length");
        }

        mSettings.principalPointX = mCameraParameters.principalPointX;
        mSettings.principalPointY = mCameraParameters.principalPointY;

        long t0 = SystemClock.elapsedRealtimeNanos(); // this should be same as the sensor clock
        Log.d(TAG, jsonSettings());
        configure(t0, width, height, textureId, mSettings.halfFps ? 2 : 1, false, mSettings.moduleName, jsonSettings());

        if (mSettings.parametersFileName != null) {
            mSensorHandler.post(new Runnable() {
                @Override
                public void run() {
                    writeParamsFile();
                }
            });
        }
        if (mSettings.infoFileName != null) {
            final String device = Build.MANUFACTURER
                    + " " + Build.MODEL
                    + " " + Build.VERSION.RELEASE
                    + " " + Build.VERSION_CODES.class.getFields()[android.os.Build.VERSION.SDK_INT].getName();

            mSensorHandler.post(new Runnable() {
                @Override
                public void run() {
                    writeInfoFile(mMode, device);
                }
            });
        }
    }

    @Override
    public void onFrame(long timestamp) {

        if (processFrame(timestamp,
                0, // camera ind
                mCameraParameters.focalLengthX,
                mCameraParameters.focalLengthY,
                mCameraParameters.principalPointX,
                mCameraParameters.principalPointY))
        {
            mProcessedFpsMonitor.onSample();

            String statsString = getStatsString();
            statsString += String.format(" %.3g FPS", mProcessedFpsMonitor.getLatestFrequency());
            int trackingStatus = getTrackingStatus();
            double[] pose = getPose();
            TrackingOutput output = new TrackingOutput(pose, trackingStatus, statsString);
            mListener.onOutput(output);
        }
    }

    public void onSurfaceChanged(int w, int h) {
        mScreenWidth = w;
        mScreenHeight = h;
        configureVisualization(w, h);
    }

    @Override
    public void onDraw() {
        // this clock is typically more or less in sync with the sensors & camera
        drawVisualization(SystemClock.elapsedRealtimeNanos());
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
//        Log.d(TAG, "(thread " + android.os.Process.myTid() + ") processing a sensor event "
//                + event.timestamp);
        switch (event.sensor.getType()) {
            case Sensor.TYPE_ACCELEROMETER:
            case Sensor.TYPE_ACCELEROMETER_UNCALIBRATED:
                if (event.sensor.getType() == mAccSensor) {
                    mAccMonitor.onSample();
                    // Event is reused, extract values we want
                    final long time = event.timestamp;
                    final float[] measurement = {event.values[0], event.values[1], event.values[2]};
                    processAccSample(time, measurement[0], measurement[1], measurement[2]);
                }
                break;
            case Sensor.TYPE_GYROSCOPE:
            case Sensor.TYPE_GYROSCOPE_UNCALIBRATED:
                if (event.sensor.getType() == mGyroSensor) {
                    mGyroMonitor.onSample();
                    // Event is reused, extract values we want
                    final long time = event.timestamp;
                    final float[] measurement = {event.values[0], event.values[1], event.values[2]};
                    processGyroSample(time, measurement[0], measurement[1], measurement[2]);
                }
                break;
            default:
                break;
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int i) {
        Log.d(TAG, "accuracy changed: " + sensor + " " + i);
    }

    void logExternalPoseMatrix(final long timeNs, final float[] viewMtx, final String tag) {
        // beneficial to process this in non-GL thread, hence posting to native handler
        mSensorHandler.post(new Runnable() {
            @Override
            public void run() {
                recordPoseMatrix(timeNs, viewMtx, tag);
            }
        });
    }

    synchronized void logExternalImage(int textureId, long timeNanos, long frameNumber, int cameraInd,
                                        int width, int height, float focalLengthX, float focalLengthY, float ppx, float ppy) {
        if (!running) {
            // logExternalImage() is often called after stop(), this prevents it breaking anything
            Log.w(TAG, "start() must be called before using AlgorithmWorker!");
            return;
        }
        if (!mExternalInitialized) {
            configure(timeNanos, width, height, textureId, 1, mSettings.recordPoses, mSettings.moduleName, jsonSettings());
            configureVisualization(mScreenWidth, mScreenHeight);
            mExternalInitialized = true;
        }

        processExternalImage(timeNanos, frameNumber, cameraInd, focalLengthX, focalLengthY, ppx, ppy);
    }

    // --- these are called from the GL thread
    private native void configureVisualization(int width, int height);
    private native void configure(long timeNanos, int width, int height, int textureId, int frameStride, boolean recordExternalPoses, String moduleName, String settingsJson);

    private native boolean processFrame(long timeNanos, int cameraInd, float fx, float fy, float px, float py);
    public native void processExternalImage(long timeNanos, long frameNumber, int cameraInd, float fx, float fy, float ppx, float ppy);

    private native void drawVisualization(long timeNanos);
    private native String getStatsString(); // TODO: rather call from sensor thread
    private native int getTrackingStatus(); // TODO: rather call from sensor thread
    private native double[] getPose(); // [t,x,y,z,qx,qy,qz,qw]

    // --- these are called from the sensor thread (mSensorHandler)
    private native void processGyroSample(long timeNanos, float x, float y, float z);
    private native void processAccSample(long timeNanos, float x, float y, float z);
    private native void processGpsLocation(long timeNanos, double latitude, double longitude, double altitude, float accuracy);
    private native void processGpsTime(long timeNanos, double gpsTime);
    private native void recordPoseMatrix(long timeNanos, float[] viewMatrix, String tag);
    private native void writeInfoFile(String mode, String device);
    private native void writeParamsFile();
    private native double convertTime(long timeNanos);
    private native void nativeStop();
}
