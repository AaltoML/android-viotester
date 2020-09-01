package org.example.viotester;

import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.media.Image;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import android.util.Size;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

import androidx.annotation.Nullable;
import org.codehaus.jackson.map.ObjectMapper;

public class AlgorithmWorker implements SensorEventListener, CameraWorker.Listener {
    private static final String TAG = AlgorithmWorker.class.getName();

    public interface Listener {
        void onStats(String stats, int trackingStatus);
        void onAvailableSizes(Size[] sizes);
        void onAvailableFps(Set<Integer> fps);
        void onAvailableCameras(List<String> cameras);
        void onRelativeFocalLength(double relativeFocalLength);
    }

    public static class Settings {
        public enum VideoVisualization {
            NONE(false),
            PLAIN_VIDEO(true),
            TRACKER_DEBUG(true),
            ODOMETRY_DEBUG(true);

            VideoVisualization(boolean colorFrame) {
                processColorFrame = colorFrame;
            }
            private final boolean processColorFrame;
        }

        public enum OverlayVisualization {
            NONE,
            TRACK,
            AR,
            SPLIT_SCREEN
        }

        public String moduleName;

        public VideoVisualization videoVisualization;
        public OverlayVisualization overlayVisualization;

        public int targetFps;
        public Size targetImageSize;
        public String targetCamera;
        public Float relativeFocalLength;
        public boolean halfFps;
        public boolean useCalibAcc;
        public boolean useCalibGyro;
        public boolean fastMode;
        public boolean jumpFilter;
        public boolean trackerOnly;

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

        public boolean recordingOnly = false;
        public boolean recordGps = false;
        public boolean recordWiFiLocations = false;

        // Do not change these directly
        public float focalLength = -1;
        public float principalPointX = -1;
        public float principalPointY = -1;
    }

    private class GpsListener implements LocationListener {
        private final LocationManager locationManager;

        GpsListener(LocationManager m) {
            locationManager = m;
        }

        void start() {
            if (mSettings.recordGps)
                locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 0, this);
            if (mSettings.recordWiFiLocations)
                locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 0, 0, this);
        }

        void stop() {
            if (mSettings.recordGps || mSettings.recordWiFiLocations)
                locationManager.removeUpdates(this);
        }

        @Override
        public void onLocationChanged(final Location location) {
            final long t = SystemClock.elapsedRealtimeNanos();
            Log.d(TAG, "Location: " + location.getLatitude() + ", " + location.getLongitude() + " (accuracy " + location.getAccuracy() + "m)");
            mNativeHandler.post(new Runnable() {
                @Override
                public void run() {
                    processGpsLocation(t, location.getLatitude(), location.getLongitude(), location.getAltitude(), location.getAccuracy());
                }
            });
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
    private final Handler mNativeHandler;
    private final Settings mSettings;
    private final Listener mListener;
    private final GpsListener mGpsListener;
    private final String mMode;

    private final FrequencyMonitor mAccMonitor;
    private final FrequencyMonitor mGyroMonitor;
    private final FrequencyMonitor mProcessedFpsMonitor;
    private final double[] mPoseData = new double[7];

    private byte[] mCameraImageBuffer = null;
    private CameraWorker.CameraParameters mCameraParameters = null;
    private boolean mInitialized = false;
    private boolean mProcessColorFrames = false;

    //private final static int GYRO_SENSOR = Sensor.TYPE_GYROSCOPE_UNCALIBRATED;
    private final int mGyroSensor;
    private final int mAccSensor;

    public AlgorithmWorker(
            SensorManager sensorManager,
            LocationManager locationManager,
            Settings settings,
            Listener listener,
            Handler handler,
            String mode) {
        mSensorManager = sensorManager;

        mSettings = settings;
        mListener = listener;
        mMode = mode;

        mNativeHandler = handler;

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

    public void start() {
        Log.d(TAG, "start");
        // always record sensors at max rate when
        mInitialized = false;
        final int sensorDelay = SensorManager.SENSOR_DELAY_FASTEST;
        for (Sensor sensor : mSensors) {
            mSensorManager.registerListener(this, sensor, sensorDelay, mNativeHandler);
        }
        mAccMonitor.start();
        mGyroMonitor.start();
        mProcessedFpsMonitor.start();
        mGpsListener.start();

        if (!mInitialized && !mSettings.recordCamera) {
            Log.i(TAG, "dummy initialization with screen dimensions");
            configure(mSettings.screenWidth, mSettings.screenHeight, mSettings.moduleName, jsonSettings());
        }
    }

    public void stop() {
        Log.d(TAG, "stop");
        mSensorManager.unregisterListener(this);
        mAccMonitor.stop();
        mGyroMonitor.stop();
        mProcessedFpsMonitor.stop();
        mGpsListener.stop();
    }

    @Override
    public void onCameraStart(CameraWorker.CameraParameters cameraParameters) {
        Log.d(TAG, "onCameraStart(" + cameraParameters + ")");
        mCameraParameters = cameraParameters;
    }

    @Override
    public void onImage(Image image, long frameNumber, final int cameraInd,
                        final float focalLength, final float px, final float py) {
        final long t = image.getTimestamp();
        mProcessedFpsMonitor.onSample();

        //Log.v(TAG, "onImage t: " + t + ", image t " + image.getTimestamp());

        final int width = image.getWidth();
        final int height = image.getHeight();

        if (!mInitialized) {
            mInitialized = true;

            mSettings.focalLength = -1;
            if (mCameraParameters != null) {
                Log.i(TAG, "focal length from camera API: "+mCameraParameters.focalLength);
                mSettings.focalLength = mCameraParameters.focalLength;
                mListener.onRelativeFocalLength(mCameraParameters.focalLength / width);
            }
            else if (mSettings.relativeFocalLength != null) {
                Log.i(TAG, "relative focal length from settings: " + mSettings.relativeFocalLength);
                mSettings.focalLength = mSettings.relativeFocalLength * width;
            } else {
                Log.i(TAG, "default focal length");
            }

            mSettings.principalPointX = mCameraParameters == null ? -1 : mCameraParameters.principalPointX;
            mSettings.principalPointY = mCameraParameters == null ? -1 : mCameraParameters.principalPointY;

            mProcessColorFrames = configure(width, height, mSettings.moduleName, jsonSettings());

            if (mSettings.parametersFileName != null) {
                writeParamsFile();
            }
            if (mSettings.infoFileName != null) {
                String device = Build.MANUFACTURER
                        + " " + Build.MODEL
                        + " " + Build.VERSION.RELEASE
                        + " " + Build.VERSION_CODES.class.getFields()[android.os.Build.VERSION.SDK_INT].getName();
                writeInfoFile(mMode, device);
            }
        }

        final Image.Plane[] planes = image.getPlanes();
        final Image.Plane grayPlane = planes[0];

        final int imageSize = grayPlane.getBuffer().remaining();
        int colorPlaneSize = 0;
        if (mCameraImageBuffer == null) {
            // enough to fit both Gray (= Y) and subsampled U & V buffers
            mCameraImageBuffer = new byte[imageSize * (mProcessColorFrames ? 2 : 1)];
            assert(grayPlane.getPixelStride() == 1);
            if (mProcessColorFrames) {
                // most of this stuff is guaranteed in the Android Camera 2 API, checking anyway...
                assert(planes.length == 3);
                assert(planes[2].getBuffer().remaining() == planes[1].getBuffer().remaining());
                assert(planes[1].getPixelStride() <= 2);
                assert(planes[2].getPixelStride() <= 2);
                assert(planes[1].getRowStride() == grayPlane.getRowStride());
                assert(planes[1].getRowStride() == planes[2].getRowStride());
            }
        }

        grayPlane.getBuffer().get(mCameraImageBuffer, 0, imageSize);

        if (mProcessColorFrames) {
            // get U and V data
            assert(imageSize % 2 == 0);
            final int vSize = planes[1].getBuffer().remaining();
            final int uSize = planes[2].getBuffer().remaining();
            assert(vSize <= imageSize/2 && vSize == uSize);
            colorPlaneSize = vSize;
            assert(imageSize % 2 == 0);
            planes[1].getBuffer().get(mCameraImageBuffer, imageSize, colorPlaneSize);
            planes[2].getBuffer().get(mCameraImageBuffer, imageSize + colorPlaneSize, colorPlaneSize);
        }

        boolean visualizationEnabled = mProcessColorFrames;

        final int finalColorPlaneSize = colorPlaneSize;
        final int rowStride = grayPlane.getRowStride(); // Store to var so image can be closed
        final int pixelStride = planes[1].getPixelStride(); // Store to var so image can be closed
        mNativeHandler.post(new Runnable() {
            @Override
            public void run() {
                processFrame(t,
                        rowStride,
                        pixelStride,
                        imageSize,
                        finalColorPlaneSize,
                        mCameraImageBuffer,
                        mProcessColorFrames,
                        mPoseData,
                        cameraInd,
                        focalLength,
                        px,
                        py);
            }
        });

        if (visualizationEnabled) {
            drawVisualization();
        }

        String statsString = getStatsString();
        statsString += String.format(" %.3g FPS", mProcessedFpsMonitor.getLatestFrequency());
        mListener.onStats(statsString, getTrackingStatus());
    }

    private String jsonSettings() {
        try {
            return new ObjectMapper().writeValueAsString(mSettings);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    public int getTargetFps() {
        return mSettings.targetFps;
    }

    @Override
    public void setAvailableFps(Set<Integer> fps) {
        mListener.onAvailableFps(fps);
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
    public Handler getHandler() {
        return mNativeHandler;
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        //Log.d(TAG, "(thread " + android.os.Process.myTid() + ") processing a sensor event");
        switch (event.sensor.getType()) {
            case Sensor.TYPE_ACCELEROMETER:
            case Sensor.TYPE_ACCELEROMETER_UNCALIBRATED:
                if (event.sensor.getType() == mAccSensor) {
                    mAccMonitor.onSample();
                    // Event is reused, extract values we want
                    final long time = event.timestamp;
                    final float[] measurement = {event.values[0], event.values[1], event.values[2]};
                    mNativeHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            processAccSample(time, measurement[0], measurement[1], measurement[2]);
                        }
                    });
                }
                break;
            case Sensor.TYPE_GYROSCOPE:
            case Sensor.TYPE_GYROSCOPE_UNCALIBRATED:
                if (event.sensor.getType() == mGyroSensor) {
                    mGyroMonitor.onSample();
                    // Event is reused, extract values we want
                    final long time = event.timestamp;
                    final float[] measurement = {event.values[0], event.values[1], event.values[2]};
                    mNativeHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            processGyroSample(time, measurement[0], measurement[1], measurement[2]);
                        }
                    });
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
        if (mSettings.recordPoses) {
            mNativeHandler.post(new Runnable() {
                @Override
                public void run() {
                    recordPoseMatrix(timeNs, viewMtx, tag);
                }
            });
        }
    }

    /**
     * @return true if the algorithm should process color frames, false if not
     */
    public native boolean configure(int width, int height, String moduleName, String settingsJson);

    /**
     *
     * @param timeNanos input frame timestamp in nanoseconds
     * @param rowStride YUV data parameter
     * @param chromaPixelStride YUV data parameter
     * @param grayImageSize YUV data parameter
     * @param chromaPlaneSize YUV data parameter
     * @param yuvData input YUV/Gray-only color buffer
     * @param hasColorData does the yuvBuffer contain color planes
     * @param outPoseData output pose [x,y,z,qw,qx,qy,qz]
     * @return output timestamp in nanoseconds
     */
    public native long processFrame(
            long timeNanos,
            int rowStride, int chromaPixelStride, int grayImageSize, int chromaPlaneSize,
            byte[] yuvData,
            boolean hasColorData,
            double[] outPoseData,
            int cameraInd, float focalLength, float px, float py);

    public native void drawVisualization();
    public native void processGyroSample(long timeNanos, float x, float y, float z);
    public native void processAccSample(long timeNanos, float x, float y, float z);
    public native void processGpsLocation(long timeNanos, double latitude, double longutide, double altitude, float accuracy);
    public native void recordPoseMatrix(long timeNanos, float[] viewMatrix, String tag);
    public native String getStatsString();
    public native int getTrackingStatus();
    public native void writeInfoFile(String mode, String device);
    public native void writeParamsFile();
}
