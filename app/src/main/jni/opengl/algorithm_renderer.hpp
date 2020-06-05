#ifndef OPENGL_ANDROID_DEFAULT_HPP
#define OPENGL_ANDROID_DEFAULT_HPP

#include <jni.h>
#include <vector>

namespace defaultOpenGLRenderer {
    void setBgraCameraData(int width, int height, void *buffer);
    void renderARScene(double t, const float xyz[], const float quaternion[], float relativeFocalLength);
    void setPointCloud(const float *flatData, std::size_t n);
}


#endif