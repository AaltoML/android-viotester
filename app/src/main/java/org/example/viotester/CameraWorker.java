package org.example.viotester;

import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.CaptureResult;
import android.hardware.camera2.TotalCaptureResult;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import android.util.Log;
import android.util.Size;
import android.view.Surface;
import android.view.TextureView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class CameraWorker {
    private static final String TAG = CameraWorker.class.getName();

    private static final int MAX_BUFFERED_IMAGES = 3;

    private final Handler mNativeHandler;
    private final CameraManager mCameraManager;
    private final CameraDevice.StateCallback mStateCallback;
    private final CameraCaptureSession.CaptureCallback mCaptureCallback;
    private final TextureView mTextureView;
    private final Listener mListener;
    private final FrequencyMonitor mFpsMonitor;

    private CameraDevice mCameraDevice;
    private CameraCaptureSession mCaptureSession;
    private ImageReader mImageReader;
    private String mCameraId;
    private boolean mKilled;
    private long mFrameNumber;
    private Size mDataSize;

    private final FpsFilter mFpsFilter;

    public static class CameraParameters {
        int width;
        int height;
        float focalLength;
        float principalPointX;
        float principalPointY;

        @Override
        public String toString() {
            return "width: " + width + ", " +
                    "height: " + height + ", " +
                    "focalLength: " + focalLength + ", " +
                    "principalPoint: (" +
                    principalPointX + ", " +
                    principalPointY + ")";
        }
    }

    interface SizeChooser {
        Size chooseSize(Size[] availableSizes);
    }

    interface Listener extends SizeChooser {
        String chooseCamera(List<String> cameras);
        void onCameraStart(CameraParameters cameraParameters);
        void onImage(Image image, long frameNumber, int cameraInd, float focalLength, float px, float py);
        double getTargetFps();
        Handler getHandler();
    }

    public CameraWorker(CameraManager manager, @Nullable TextureView view, Listener listener, Handler handler) {
        Log.d(TAG, "ctor");
        mCameraManager = manager;
        mTextureView = view;

        mListener = listener;
        mNativeHandler = handler;
        mFpsFilter = new FpsFilter(listener.getTargetFps());

        mFpsMonitor = new FrequencyMonitor(new FrequencyMonitor.Listener() {
            @Override
            public void onFrequency(double freq) {
                Log.i(TAG, String.format("Preview FPS: %.3g", freq));
            }
        });

        // CameraDevice.StateCallback is called when CameraDevice changes its state.
        mStateCallback = new CameraDevice.StateCallback() {

            @Override
            public void onOpened(@NonNull CameraDevice cameraDevice) {
                // This method is called when the camera is opened.  We start camera preview here.
                Log.d(TAG, "onOpened");
                if (mKilled) {
                    Log.w(TAG, "already dead (started on background?)");
                    cameraDevice.close();
                    return;
                }
                mCameraDevice = cameraDevice;
                try {
                    createCameraPreviewSession();
                } catch (CameraAccessException e) {
                    throw new RuntimeException(e);
                } catch (IOException e) {
                    throw new RuntimeException(e);
                }
            }

            @Override
            public void onDisconnected(@NonNull CameraDevice cameraDevice) {
                Log.w(TAG, "onDisconnected");
                cameraDevice.close();
                mCameraDevice = null;
            }

            @Override
            public void onError(@NonNull CameraDevice cameraDevice, int error) {
                cameraDevice.close();
                mCameraDevice = null;
                throw new RuntimeException("camera error! " + error);
            }

        };

        mCaptureCallback = new CameraCaptureSession.CaptureCallback() {
            @Override
            public void onCaptureProgressed(@NonNull CameraCaptureSession session,
                                            @NonNull CaptureRequest request,
                                            @NonNull CaptureResult partialResult) {
            }

            @Override
            public void onCaptureCompleted(@NonNull CameraCaptureSession session,
                                           @NonNull CaptureRequest request,
                                           @NonNull TotalCaptureResult result) {
                mFpsMonitor.onSample();
            }
        };

        // When the screen is turned off and turned back on, the SurfaceTexture is already
        // available, and "onSurfaceTextureAvailable" will not be called. In that case, we can open
        // a camera and start preview from here (otherwise, we wait until the surface is ready in
        // the SurfaceTextureListener).
        if (mTextureView == null || mTextureView.isAvailable()) {
            openCamera();
        } else {
            mTextureView.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
                @Override
                public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
                    Log.d(TAG, "onSurfaceTextureAvailable " + width + "x" + height);
                    openCamera();
                }

                @Override
                public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
                    Log.d(TAG, "onSurfaceTextureSizeChanged " + width + "x" + height);
                }


                @Override
                public boolean onSurfaceTextureDestroyed(SurfaceTexture surfaceTexture) {
                    Log.d(TAG, "onSurfaceTextureDestroyed");
                    return true; // TODO: what does this mean
                }

                @Override
                public void onSurfaceTextureUpdated(SurfaceTexture surfaceTexture) {
                }
            });
        }
    }

    private void openCamera() {
        Log.v(TAG, "openCamera");
        mNativeHandler.post(new Runnable() {
            @Override
            public void run() {
                mFpsMonitor.start();
                try {
                    mCameraId = selectCamera(mCameraManager);
                    mCameraManager.openCamera(mCameraId, mStateCallback, mNativeHandler);
                    logCameraParameters();
                    mFpsFilter.reset();
                } catch (CameraAccessException e) {
                    throw new RuntimeException(e);
                } catch (SecurityException e) {
                    throw new RuntimeException(e);
                }
            }
        });
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

    private void logCameraParameters() throws CameraAccessException {
        final CameraCharacteristics characteristics = mCameraManager.getCameraCharacteristics(mCameraId);
        for (CameraCharacteristics.Key<?> key : characteristics.getKeys()) {
            Object o = characteristics.get(key);
            if (o != null) {
                Log.d(TAG, key.getName() + " " + objectToStringStupid(o));
            }
        }
    }

    private CameraParameters getCameraParameters() {
        try {
            final CameraCharacteristics characteristics = mCameraManager.getCameraCharacteristics(mCameraId);
            float[] intrinsics = characteristics.get(CameraCharacteristics.LENS_INTRINSIC_CALIBRATION);
            Size pixelArraySize = characteristics.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE);
            Rect activeRect = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);

            if (intrinsics == null || pixelArraySize == null || activeRect == null) {
                Log.w(TAG, "no intrinsic camera parameters available");
                return null;
            }

            if (intrinsics.length < 5) throw new IllegalStateException("unexpected intrinsics array size");

            if (mDataSize == null) {
                Log.d(TAG, "null capture size");
                return null;
            }

            float fx = intrinsics[0];
            float fy = intrinsics[1];
            float cx = intrinsics[2];
            float cy = intrinsics[3];
            float skew = intrinsics[4];

            if (fx <= 0.0 || fy <= 0.0) {
                Log.w(TAG, "invalid focal lengths " + fx + ", " + fy);
                return null;
            }

            CameraParameters params = new CameraParameters();

            int nativeWidth = activeRect.right - activeRect.left;
            int nativeHeight = activeRect.bottom - activeRect.top;
            float expectedCx = nativeWidth * 0.5f + activeRect.left;
            float expectedCy = nativeHeight * 0.5f + activeRect.top;

            Log.d(TAG, "active array dimensions: " + nativeWidth + "x" + nativeHeight);
            if (nativeWidth < nativeHeight) {
                Log.w(TAG, "unexpected native array dimensions, width " + nativeWidth + " < height " + nativeHeight);
            }

            params.width = mDataSize.getWidth();
            params.height = mDataSize.getHeight();

            params.principalPointX = ((cx - expectedCx) / nativeWidth + 0.5f) * params.width;
            params.principalPointY = ((cy - expectedCy) / nativeWidth) * params.width + params.height * 0.5f;

            if (Math.abs(skew) > 1e-6) {
                Log.w(TAG, "non zero intrinsic skew " + skew);
            }

            float relFocalX = fx / nativeWidth;
            float relFocalY = fy / nativeWidth; // width and not height on purpose
            params.focalLength = 0.5f * (relFocalX + relFocalY) * params.width;

            Log.d(TAG, "native principal point " + cx + ", " + cy);
            Log.d(TAG, "focal length " + params.focalLength + " / " + mDataSize.getWidth() + " = "
                    + fx + " / " + pixelArraySize.getWidth());

            return params;

        } catch (CameraAccessException e) {
            throw new RuntimeException(e);
        }
    }

    private void createCameraPreviewSession() throws CameraAccessException, IOException {
        final List<Surface> targets = new ArrayList<>();
        Log.d(TAG, "createCameraPreviewSession");

        mDataSize = selectBestCameraSize(mListener, ImageReader.class);

        if (mTextureView != null) {
            Log.d(TAG, "creating texture view target");
            SurfaceTexture texture = mTextureView.getSurfaceTexture();
            if (texture == null) throw new RuntimeException("null texture");
            Size previewSize = selectBestCameraSize(new FixedTargetChooser(mDataSize), SurfaceTexture.class);

            // We configure the size of default buffer to be the size of camera preview we want.
            texture.setDefaultBufferSize(previewSize.getWidth(), previewSize.getHeight());

            // This is the output Surface we need to start preview.
            final Surface surface = new Surface(texture);

            // transform 90 degrees (surprisingly difficult)
            setPreviewTransform(mTextureView, previewSize);

            targets.add(surface);
        }

        Log.d(TAG, "creating preview target");

        mImageReader = ImageReader.newInstance(mDataSize.getWidth(), mDataSize.getHeight(), ImageFormat.YUV_420_888, MAX_BUFFERED_IMAGES);
        targets.add(mImageReader.getSurface());

        // deliver gray images to Listener's Handler (= event queue)
        mImageReader.setOnImageAvailableListener(new ImageReader.OnImageAvailableListener() {
            @Override
            public void onImageAvailable(ImageReader imageReader) {
                //Log.v(TAG, "onImageAvailable");
                final Image image = imageReader.acquireLatestImage();
                if (image == null) return;

                final long tNanos = image.getTimestamp();
                mFpsFilter.setTime(tNanos);
                if (mFpsFilter.poll()) {
                    mListener.onImage(image, mFrameNumber, 0, -1.f, -1.f, -1.f);
                }
                if (mFpsFilter.pollAll()) {
                    Log.v(TAG,"frame(s) skipped");
                }
                image.close();
                mFrameNumber++;
            }
        }, mListener.getHandler());

        // Here, we create a CameraCaptureSession for camera preview.
        mCameraDevice.createCaptureSession(targets,
                new CameraCaptureSession.StateCallback() {

                    @Override
                    public void onConfigured(@NonNull final CameraCaptureSession cameraCaptureSession) {
                        Log.v(TAG, "onConfigured");

                        if (null == mCameraDevice) {
                            Log.i(TAG, "onConfigured: camera already closed");
                            return;
                        }

                        // When the session is ready, we start displaying the preview.
                        mCaptureSession = cameraCaptureSession;
                        try {
                            CaptureRequest.Builder previewRequest = mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
                            for (Surface target : targets) {
                                previewRequest.addTarget(target);
                            }
                            previewRequest.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO);
                            mCaptureSession.setRepeatingRequest(previewRequest.build(), mCaptureCallback, mNativeHandler);
                        } catch (CameraAccessException e) {
                            throw new RuntimeException(e);
                        }

                        mListener.getHandler().post(new Runnable() {
                            @Override
                            public void run() {
                                mListener.onCameraStart(getCameraParameters());
                            }
                        });
                    }

                    @Override
                    public void onConfigureFailed(
                            @NonNull CameraCaptureSession cameraCaptureSession) {
                        throw new RuntimeException("failed");
                    }
                }, mNativeHandler); // TODO: Use another Handler here?
    }

    public void destroy() {
        Log.d(TAG, "destroy");
        mKilled = true;
        mFpsMonitor.stop();
        if (null != mCaptureSession) {
            mCaptureSession.close();
            mCaptureSession = null;
        }
        if (null != mCameraDevice) {
            mCameraDevice.close();
            mCameraDevice = null;
        }
    }

    private static class FixedTargetChooser implements SizeChooser {
        private final Size mSize;
        FixedTargetChooser(Size size) {
            mSize = size;
        }

        @Override
        public Size chooseSize(Size[] availableSizes) {
            return mSize;
        }
    }

    private <T> Size selectBestCameraSize(SizeChooser chooser, Class<T> klass) throws CameraAccessException {
        CameraCharacteristics characteristics = mCameraManager.getCameraCharacteristics(mCameraId);

        StreamConfigurationMap map = characteristics.get(
                CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

        if (map == null)
            throw new RuntimeException("camera StreamConfigurationMap is null");

        final Size[] previewSizes;
        Log.d(TAG, "Listing available sizes for camera " + mCameraId + " for " + klass);
        previewSizes = map.getOutputSizes(klass);
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

    private String selectCamera(CameraManager manager) throws CameraAccessException {
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

        String selectedCamera = mListener.chooseCamera(viableCameraIds);
        Log.i(TAG, "using Camera " + selectedCamera);
        return selectedCamera;
    }

    private static void setPreviewTransform(TextureView textureView, Size previewSize) {
        final int viewWidth = textureView.getWidth();
        final int viewHeight = textureView.getHeight();
        Matrix matrix = new Matrix();
        RectF viewRect = new RectF(0, 0, viewWidth, viewHeight);
        RectF bufferRect = new RectF(0, 0, previewSize.getHeight(), previewSize.getWidth());
        final float centerX = viewRect.centerX();
        final float centerY = viewRect.centerY();
        bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY());
        matrix.setRectToRect(viewRect, bufferRect, Matrix.ScaleToFit.FILL);
        final float scale = Math.max((float) viewHeight / previewSize.getHeight(), (float) viewWidth / previewSize.getWidth());
        matrix.postScale(scale, scale, centerX, centerY);
        matrix.postRotate(-90 , centerX, centerY);
        textureView.setTransform(matrix);
    }
}
