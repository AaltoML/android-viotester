#ifndef AR_RENDERER_HPP
#define AR_RENDERER_HPP

#include <memory>
#include <vector>

class ArRenderer {
public:
    // Choose your coordinate convention...
    static std::unique_ptr<ArRenderer> buildWithYIsUp();
    static std::unique_ptr<ArRenderer> buildWithZIsUp();

    virtual ~ArRenderer() = default;

    // another API for the same thing
    virtual void setProjection(int w, int h, float focalLength) = 0;
    virtual void setProjectionMatrix(const float projectionMatrix[]) = 0;
    virtual void setPose(double timestamp, const float xyz[], const float quaternion[]) = 0;
    virtual void setPose(double timestamp, const float viewMatrix[]) = 0;
    virtual void setPointCloud(const float *flatData, std::size_t n) = 0;
    virtual void render() const = 0;
};

#endif