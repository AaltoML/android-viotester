#ifndef CAMERA_RENDERER_HPP
#define CAMERA_RENDERER_HPP

#include <memory>

class CameraRenderer {
public:
    /**
     * How to deal with the situation where the aspect ratios of the screen and
     * camera texture do not match
     */
    enum class AspectFixMethod {
        /** Fill screen with camera texture and crop */
        CROP = 0,
        /** Letterboxing: fit camera texture inside screen and fill rest with black */
        LETTERBOX = 1
    };
    static std::unique_ptr<CameraRenderer> build(int width, int height);
    virtual ~CameraRenderer() = default;

    virtual void render() = 0;

    // data can be set either by giving an external buffer (in RGBA/BGRA format) or an OpenGL
    // texture ID. Do not mix these (e.g., call with a texture ID after calling with a CPU buffer)
    virtual void setTextureData(int textureWidth, int textureHeight, const void *buffer, AspectFixMethod aspectFix) = 0;
    virtual void setExternalTexture(int textureWidth, int textureHeight, int textureId, AspectFixMethod aspectFix) = 0;

    virtual bool matchDimensions(int w, int h) const = 0;

    /**
     * Get the screen width of the camera texture in pixels
     * after transforming according to the AspectFixMethod
     */
    virtual int getActiveWidth() const = 0;

    /**
     * Get the height of the camera texture in pixels
     * after transforming according to the AspectFixMethod
     */
    virtual int getActiveHeight() const = 0;
};

#endif