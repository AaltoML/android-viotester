#include <jni.h>
#include <memory>
#include <mutex>

#include "util.hpp" // for logging
#include "camera_renderer.hpp"
#include "ar_renderer.hpp"
#include "algorithm_renderer.hpp"

namespace {
    std::mutex mutex;
    std::unique_ptr<CameraRenderer> cameraRenderer;
    std::unique_ptr<ArRenderer> arRenderer;
    int width, height;
    bool arRequested;
}

namespace defaultOpenGLRenderer {
void setBgraCameraData(int w, int h, void *buffer) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!cameraRenderer) return;
    width = w;
    height = h;
    cameraRenderer->setTextureData(width, height, buffer, CameraRenderer::AspectFixMethod::LETTERBOX);
}

void renderARScene(double t, const float xyz[], const float quaternion[],
                                          float relativeFocalLength) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!arRenderer) return;
    arRequested = true;

    assert(cameraRenderer);
    int w = cameraRenderer->getActiveWidth();
    int h = cameraRenderer->getActiveHeight();

    arRenderer->setProjection(w, h, relativeFocalLength*w);
    arRenderer->setPose(t, xyz, quaternion);
}

void setPointCloud(const float *flatData, std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!arRenderer) return;
    arRenderer->setPointCloud(flatData, n);
}
}

#define MY_JNI_FUNC(ret, x) JNIEXPORT ret JNICALL Java_org_example_viotester_AlgorithmRenderer_ ## x
extern "C" {
MY_JNI_FUNC(void, onSurfaceCreated)(JNIEnv*, jclass, jobject, jobject) {
    log_debug("onSurfaceCreated");
    std::lock_guard<std::mutex> lock(mutex);
    // reset everything
    cameraRenderer.reset();
    arRenderer.reset();
    arRequested = false;
}

MY_JNI_FUNC(void, onSurfaceChanged)(JNIEnv*, jclass, jobject, jint width, jint height) {
    log_debug("onSurfaceChanged %dx%d", width, height);
    std::lock_guard<std::mutex> lock(mutex);
    if (!cameraRenderer || !cameraRenderer->matchDimensions(width, height)) {
        cameraRenderer = CameraRenderer::build(width, height);
        arRenderer = ArRenderer::buildWithZIsUp();
    }
}

MY_JNI_FUNC(void, onDrawFrame)(JNIEnv*, jclass, jobject) {
    // The worst that can happen not mutex-locking is occasional garbage on screen, which should
    // should be very unlikely due to double buffering and low FPS
    std::lock_guard<std::mutex> lock(mutex);
    assert(cameraRenderer);
    cameraRenderer->render();
    if (arRequested) {
        arRenderer->render();
    }
}
}
