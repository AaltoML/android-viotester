package org.example.viotester;

import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import androidx.annotation.NonNull;

import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.util.Log;
import android.util.Range;
import android.util.Size;
import android.view.Surface;

import java.util.ArrayList;
import java.util.List;

public class CameraWorker {
    private static final String TAG = CameraWorker.class.getName();

    interface SizeChooser {
        Size chooseSize(Size[] availableSizes);
    }

    interface Listener extends SizeChooser {
        String chooseCamera(List<String> cameras);
        void availableFpsRanges(List<String> fps);
        void onCaptureStart(CameraParameters parameters, int textureId);
        void onFrame(long timestamp);
        void onDraw();
    }

    private final CameraParameters mParameters;
    private final Listener mListener;
    private final int mTargetFps;
    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface; // use member to avoid garbage collection (may not be needed)
    private boolean mHasNewCameraFrame;

    CameraWorker(CameraManager manager, Listener listener, int targetFps) {
        mListener = listener;
        mParameters = getCamera(manager, listener);
        mTargetFps = targetFps;
    }

    void start() {
        int[] glTextureIds = new int[1];
        GLES20.glGenTextures(1, glTextureIds, 0);
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, glTextureIds[0]);

        mSurfaceTexture = new SurfaceTexture(glTextureIds[0]);

        mSurfaceTexture.setDefaultBufferSize(mParameters.width, mParameters.height);
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                synchronized (CameraWorker.this) {
                    mHasNewCameraFrame = true;
                }
            }
        });

        mSurface = new Surface(mSurfaceTexture);
        startCameraSession(mParameters.cameraId, mTargetFps, mSurface);
        mListener.onCaptureStart(mParameters, glTextureIds[0]);
    }

    void onFrame() {
        if (mSurfaceTexture == null) return;

        boolean hasNewData = false;
        long lastTimestamp = 0;

        synchronized (this) {
            hasNewData = mHasNewCameraFrame;
            if (mHasNewCameraFrame) {
                mSurfaceTexture.updateTexImage();
                lastTimestamp = mSurfaceTexture.getTimestamp();
                mHasNewCameraFrame = false;
            }
        }

        if (hasNewData) mListener.onFrame(lastTimestamp);
        mListener.onDraw();
    }

    public void stop() {
        stopCameraSession();
    }

    private native void startCameraSession(String cameraId, int targetFPs, Surface surface);
    private native void stopCameraSession();

    public static class CameraParameters {
        int width;
        int height;
        float focalLengthX = -1;
        float focalLengthY = -1;
        float principalPointX = -1;
        float principalPointY = -1;

        String cameraId;

        @Override
        public String toString() {
            return "camera " + cameraId + ", " +
                    "width: " + width + ", " +
                    "height: " + height + ", " +
                    "focalLength: (" +
                    focalLengthX + ", " +
                    focalLengthY + "), " +
                    "principalPoint: (" +
                    principalPointX + ", " +
                    principalPointY + ")";
        }
    }

    public static CameraParameters getCamera(CameraManager manager, Listener listener) {
        try {
            String cameraId = selectCamera(manager, listener);
            listener.availableFpsRanges(getSupportedFps(manager, cameraId));
            logCameraParameters(manager, cameraId);
            Size dataSize = selectBestCameraSize(manager, cameraId, listener, SurfaceTexture.class);
            return getCameraParameters(manager, cameraId, dataSize);

        } catch (CameraAccessException e) {
            throw new RuntimeException(e);
        }
    }

    private static String objectToStringStupid(Object o) {
        // there should be a better way... not sure if there is
        StringBuilder b = new StringBuilder();
        if (o instanceof float[]) {
            b.append("float[]{ ");
            for (float x : (float[]) o) b.append(x).append(", ");
            b.append("}");
        } else if (o instanceof int[]) {
            b.append("int[]{ ");
            for (int x : (int[]) o) b.append(x).append(", ");
            b.append("}");
        } else {
            b.append(o.getClass().getName()).append(" ").append(o);
        }
        return b.toString();
    }

    private static void logCameraParameters(CameraManager manager, String cameraId) throws CameraAccessException {
        final CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
        for (CameraCharacteristics.Key<?> key : characteristics.getKeys()) {
            Object o = characteristics.get(key);
            if (o != null) {
                Log.d(TAG, key.getName() + " " + objectToStringStupid(o));
            }
        }
    }

    private static CameraParameters getCameraParameters(CameraManager manager, String cameraId, @NonNull Size dataSize) throws CameraAccessException {
        final CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
        float[] intrinsics = characteristics.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION);
        Size pixelArraySize = characteristics.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE);
        Rect activeRect = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);

        CameraParameters params = new CameraParameters();
        params.cameraId = cameraId;
        params.width = dataSize.getWidth();
        params.height = dataSize.getHeight();
        params.principalPointX = params.width * 0.5f;
        params.principalPointY = params.height * 0.5f;

        if (intrinsics == null || pixelArraySize == null || activeRect == null) {
            Log.w(TAG, "no intrinsic camera parameters available");
            return params;
        }

        if (intrinsics.length < 5) throw new IllegalStateException("unexpected intrinsics array size");

        float fx = intrinsics[0];
        float fy = intrinsics[1];
        float cx = intrinsics[2];
        float cy = intrinsics[3];
        float skew = intrinsics[4];

        if (fx <= 0.0 || fy <= 0.0) {
            Log.w(TAG, "invalid focal lengths " + fx + ", " + fy);
        } else {
            int nativeWidth = activeRect.right - activeRect.left;
            int nativeHeight = activeRect.bottom - activeRect.top;
            float expectedCx = nativeWidth * 0.5f + activeRect.left;
            float expectedCy = nativeHeight * 0.5f + activeRect.top;

            Log.d(TAG, "active array dimensions: " + nativeWidth + "x" + nativeHeight);
            if (nativeWidth < nativeHeight) {
                Log.w(TAG, "unexpected native array dimensions, width " + nativeWidth + " < height " + nativeHeight);
            }

            params.principalPointX = ((cx - expectedCx) / nativeWidth + 0.5f) * params.width;
            params.principalPointY = ((cy - expectedCy) / nativeWidth) * params.width + params.height * 0.5f;

            if (Math.abs(skew) > 1e-6) {
                Log.w(TAG, "non zero intrinsic skew " + skew);
            }

            float relFocalX = fx / nativeWidth;
            float relFocalY = fy / nativeWidth; // width and not height on purpose
            params.focalLengthX = relFocalX * params.width;
            params.focalLengthY = relFocalY * params.width;

            Log.d(TAG, "native principal point " + cx + ", " + cy);
            Log.d(TAG, "focal length " + params.focalLengthX + ", " + params.focalLengthY + " / " + dataSize.getWidth() + " = "
                    + fx + " / " + pixelArraySize.getWidth());
        }

        return params;
    }

    private static <T> Size selectBestCameraSize(CameraManager manager, String cameraId, SizeChooser chooser, Class<T> klass) throws CameraAccessException {
        CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);

        StreamConfigurationMap map = characteristics.get(
                CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

        if (map == null)
            throw new RuntimeException("camera StreamConfigurationMap is null");

        final Size[] previewSizes;
        Log.d(TAG, "Listing available sizes for camera " + cameraId + " for " + klass);
        previewSizes = map.getOutputSizes(klass);
        if (previewSizes == null)
            throw new RuntimeException("no supported sizes!");
        for (Size s : previewSizes)
            Log.d(TAG, " - size " + s.getWidth() + "x" + s.getHeight());
        Size targetSize = chooser.chooseSize(previewSizes);
        Size previewSize = selectOptimalSize(previewSizes, targetSize.getWidth(), targetSize.getHeight());
        Log.i(TAG, "selected size "+previewSize.getWidth()+"x"+previewSize.getHeight());
        return previewSize;
    }

    private static Size selectOptimalSize(Size[] sizes, int targetWidth, int targetHeight) {
        int maxDiff = Integer.MAX_VALUE;
        Size best = null;
        for (Size s : sizes) {
            int diff = Math.abs(s.getHeight() - targetHeight) + Math.abs(s.getWidth() - targetWidth);
            if (diff < maxDiff) {
                best = s;
                maxDiff = diff;
            }
        }
        return best;
    }

    private static List<String> getSupportedFps(CameraManager manager, String cameraId) throws CameraAccessException {
        CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
        Range<Integer>[] fpsRanges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
        List<String> fixedFpsRanges = new ArrayList<>();
        for (Range<Integer> range : fpsRanges) {
            if (range.getLower() == range.getUpper()) {
                fixedFpsRanges.add(Integer.toString(range.getLower()));
            }
        }
        return fixedFpsRanges;
    }

    private static String selectCamera(CameraManager manager, Listener listener) throws CameraAccessException {
        List<String> viableCameraIds = new ArrayList<>();

        for (String cameraId : manager.getCameraIdList()) {
            CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);

            // We don't use a front facing camera here
            Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
            if (facing != null && facing == CameraCharacteristics.LENS_FACING_FRONT) {
                Log.d(TAG, "Camera " + cameraId + " is front-facing -> skipping");
                continue;
            }

            StreamConfigurationMap map = characteristics.get(
                    CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            if (map == null) {
                Log.w(TAG, "Camera " + cameraId + " skipped: no stream configuration map");
                continue;
            }

            Log.d(TAG, "viable camera " + cameraId);
            viableCameraIds.add(cameraId);
        }

        if (viableCameraIds.isEmpty()) {
            throw new RuntimeException("unable to find suitable camera");
        }

        String selectedCamera = listener.chooseCamera(viableCameraIds);
        Log.i(TAG, "using Camera " + selectedCamera);
        return selectedCamera;
    }
}
