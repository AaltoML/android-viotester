#include <jni.h>

#include <memory>
#include <Eigen/Dense>
#include "ar_renderer.hpp"
#include "util.hpp"

namespace {
    std::unique_ptr<ArRenderer> arRenderer;
}

#define MY_JNI_FUNC(ret, x) JNIEXPORT ret JNICALL Java_org_example_viotester_ext_1ar_Renderer_ ## x

extern "C" {
MY_JNI_FUNC(void, onSurfaceCreated)(JNIEnv *env, jobject a, jobject b, jobject c) {
    log_debug("External AR onSurfaceCreated");
    // reset everything
    arRenderer.reset();
}

MY_JNI_FUNC(void, onSurfaceChanged)(JNIEnv*, jobject, jobject, jint width, jint height) {
    log_debug("External AR onSurfaceChanged %dx%d", width, height);
    if (!arRenderer) {
        arRenderer = ArRenderer::buildWithYIsUp();
    }
}

MY_JNI_FUNC(void, onDrawFrame)(JNIEnv *env, jobject thiz, jdouble t, jfloatArray mvMatrix, jfloatArray projMatrix) {
    assert(arRenderer);
    auto *vmat = env->GetFloatArrayElements(mvMatrix, nullptr);
    auto *projMat = env->GetFloatArrayElements(projMatrix, nullptr);

    arRenderer->setProjectionMatrix(projMat);
    arRenderer->setPose(t, vmat);

    env->ReleaseFloatArrayElements(mvMatrix, vmat, JNI_ABORT);
    env->ReleaseFloatArrayElements(projMatrix, projMat, JNI_ABORT);

    arRenderer->render();
}

MY_JNI_FUNC(void, setPointCloud)(JNIEnv *env, jobject thiz, jfloatArray pointCloudData, jint size) {
    assert(arRenderer);
    auto *data = env->GetFloatArrayElements(pointCloudData, nullptr);
    arRenderer->setPointCloud(data, static_cast<std::size_t>(size));

    env->ReleaseFloatArrayElements(pointCloudData, data, JNI_ABORT);
}
}