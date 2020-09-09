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
    explicit NativeCameraSessionImplementation(const std::string &cameraId) {
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

std::unique_ptr<NativeCameraSession> NativeCameraSession::create(std::string cameraId) {
    return std::unique_ptr<NativeCameraSession>(new NativeCameraSessionImplementation(cameraId));
}
NativeCameraSession::~NativeCameraSession() = default;