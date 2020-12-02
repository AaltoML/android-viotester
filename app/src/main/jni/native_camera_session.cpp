#include "native_camera_session.hpp"
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraDevice.h>
#include <android/native_window_jni.h>
#include <media/NdkImageReader.h>

#include <string>

#include "logging.hpp"

namespace {
namespace camCallbacks {
    // device
    void onDisconnected(void* context, ACameraDevice* device)
    {
        log_debug("onDisconnected");
    }

    void onError(void* context, ACameraDevice* device, int error)
    {
        log_error("error %d", error);
    }

    // session
    void onSessionActive(void* context, ACameraCaptureSession *session)
    {
        log_debug("onSessionActive()");
    }

    void onSessionReady(void* context, ACameraCaptureSession *session)
    {
        log_debug("onSessionReady()");
    }

    void onSessionClosed(void* context, ACameraCaptureSession *session)
    {
        log_debug("onSessionClosed()");
    }

    // capture
    void onCaptureFailed(void* context, ACameraCaptureSession* session,
                         ACaptureRequest* request, ACameraCaptureFailure* failure)
    {
        log_error("onCaptureFailed ");
    }

    void onCaptureSequenceCompleted(void* context, ACameraCaptureSession* session,
                                    int sequenceId, int64_t frameNumber)
    {

    }

    void onCaptureSequenceAborted(void* context, ACameraCaptureSession* session,
                                  int sequenceId)
    {

    }

    void onCaptureCompleted (
            void* context, ACameraCaptureSession* session,
            ACaptureRequest* request, const ACameraMetadata* result)
    {
        // called on each frame
        // log_debug("Capture completed");
    }
}

class NativeCameraSessionImplementation : public NativeCameraSession {
private:
    ACameraManager* cameraManager = nullptr;
    ACameraDevice* cameraDevice = nullptr;
    ACameraOutputTarget* textureTarget = nullptr;
    ACaptureRequest* request = nullptr;
    ANativeWindow* textureWindow = nullptr;
    ACameraCaptureSession* textureSession = nullptr;
    ACaptureSessionOutput* textureOutput = nullptr;
    ACaptureSessionOutput* output = nullptr;
    ACaptureSessionOutputContainer* outputs = nullptr;
    const std::string cameraId;
    int targetFps;

    ACameraDevice_stateCallbacks cameraDeviceCallbacks = {
            .context = nullptr,
            .onDisconnected = camCallbacks::onDisconnected,
            .onError = camCallbacks::onError,
    };

    ACameraCaptureSession_stateCallbacks sessionStateCallbacks {
            .context = nullptr,
            .onActive = camCallbacks::onSessionActive,
            .onReady = camCallbacks::onSessionReady,
            .onClosed = camCallbacks::onSessionClosed
    };


    ACameraCaptureSession_captureCallbacks captureCallbacks {
            .context = nullptr,
            .onCaptureStarted = nullptr,
            .onCaptureProgressed = nullptr,
            .onCaptureCompleted = camCallbacks::onCaptureCompleted,
            .onCaptureFailed = camCallbacks::onCaptureFailed,
            .onCaptureSequenceCompleted = camCallbacks::onCaptureSequenceCompleted,
            .onCaptureSequenceAborted = camCallbacks::onCaptureSequenceAborted,
            .onCaptureBufferLost = nullptr,
    };

public:
    explicit NativeCameraSessionImplementation(const std::string &cameraId, int targetFps) : cameraId(cameraId), targetFps(targetFps) {
        cameraManager = ACameraManager_create();
        ACameraManager_openCamera(cameraManager, cameraId.c_str(), &cameraDeviceCallbacks, &cameraDevice);
    }

    ~NativeCameraSessionImplementation() final {
        if (cameraManager)
        {
            // Stop recording to SurfaceTexture and do some cleanup
            ACameraCaptureSession_stopRepeating(textureSession);
            ACameraCaptureSession_close(textureSession);
            ACaptureSessionOutputContainer_free(outputs);
            ACaptureSessionOutput_free(output);

            ACameraDevice_close(cameraDevice);
            ACameraManager_delete(cameraManager);
            cameraManager = nullptr;

            // Capture request for SurfaceTexture
            ANativeWindow_release(textureWindow);
            ACaptureRequest_free(request);
        }
    }

    void initCameraSurface(JNIEnv* env, jobject surface) final
    {
        // Prepare surface
        textureWindow = ANativeWindow_fromSurface(env, surface);

        // Prepare request for texture target
        ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_PREVIEW, &request);

        // Find highest supported FPS and use it in camera request
        ACameraMetadata *cameraMetadata = nullptr;
        camera_status_t camera_status = ACameraManager_getCameraCharacteristics(cameraManager,
                                                                                cameraId.c_str(),
                                                                                &cameraMetadata);
        if (camera_status != ACAMERA_OK) {
            log_error("Failed to load camera characteristics %d", camera_status);
        } else {
            ACameraMetadata_const_entry supportedFpsRanges;
            ACameraMetadata_getConstEntry(cameraMetadata,
                                          ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                                          &supportedFpsRanges);
            int32_t highestRange[2] = {0, 0};
            for (int32_t i = 0; i < supportedFpsRanges.count; i += 2) {
                int32_t min = supportedFpsRanges.data.i32[i];
                int32_t max = supportedFpsRanges.data.i32[i + 1];
                log_debug("Supported camera FPS range: [%d-%d]", min, max);
                if (highestRange[0] <= min && highestRange[1] <= max
                    && (targetFps <= 0 || max <= (int32_t)targetFps)) {
                    highestRange[0] = min;
                    highestRange[1] = max;
                }
            }
            if (highestRange[0] > 0 && highestRange[1] > 0) {
                log_debug("Using FPS range: [%d-%d]", highestRange[0], highestRange[1]);
                camera_status = ACaptureRequest_setEntry_i32(request,
                                                             ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,
                                                             2, highestRange);
                if (camera_status != ACAMERA_OK) {
                    log_error("Failed to set FPS range for camera %d", camera_status);
                }
            } else {
                log_error("Failed to find proper FPS range");
            }

            ACameraMetadata_const_entry entry;
            ACameraMetadata_getConstEntry(cameraMetadata,
                                          ACAMERA_REQUEST_AVAILABLE_CAPABILITIES,
                                          &entry);

            bool motionTrackingSupported = false;
            for (int32_t i = 0; i < (int32_t)entry.count; i++)
                if (entry.data.u8[i] == ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_MOTION_TRACKING)
                    motionTrackingSupported = true;
            if (motionTrackingSupported) {
                const int32_t cameraIntent = ACAMERA_CONTROL_CAPTURE_INTENT_MOTION_TRACKING;
                camera_status = ACaptureRequest_setEntry_i32(request,ACAMERA_CONTROL_CAPTURE_INTENT,1, &cameraIntent);
                if (camera_status != ACAMERA_OK) log_warn("Failed to set capture intent to motion tracking");
            } else {
                log_info("Motion tracking intent not supported on this device");
            }
        }

        // Prepare outputs for session
        ACaptureSessionOutput_create(textureWindow, &textureOutput);
        ACaptureSessionOutputContainer_create(&outputs);
        ACaptureSessionOutputContainer_add(outputs, textureOutput);

        // Prepare target surface
        ANativeWindow_acquire(textureWindow);
        ACameraOutputTarget_create(textureWindow, &textureTarget);
        ACaptureRequest_addTarget(request, textureTarget);

        // Create the session
        ACameraDevice_createCaptureSession(cameraDevice, outputs, &sessionStateCallbacks, &textureSession);

        // Start capturing continuously
        ACameraCaptureSession_setRepeatingRequest(textureSession, &captureCallbacks, 1, &request, nullptr);
    }
};
}

std::unique_ptr<NativeCameraSession> NativeCameraSession::create(std::string cameraId, int targetFps) {
    return std::unique_ptr<NativeCameraSession>(new NativeCameraSessionImplementation(cameraId, targetFps));
}
NativeCameraSession::~NativeCameraSession() = default;