#ifndef ALGORITHM_MODULE_HPP
#define ALGORITHM_MODULE_HPP

#include <string>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include "jsonl-recorder/types.hpp"

namespace cv { class Mat; }

class AlgorithmModule {
public:
    using Vector3d = recorder::Vector3d;
    using Pose = recorder::Pose;
    using json = nlohmann::json;

    struct Gps {
        double latitude;
        double longitude;
        double altitude = 0;
        float accuracy;
    };

    struct CameraIntrinsics {
        int cameraIndex = 0;
        float focalLengthX, focalLengthY;
        float principalPointX, principalPointY;
    };

    // these may be called from the GL thread
    static std::unique_ptr<AlgorithmModule> build(int textureId, int width, int height, const std::string &name, const json *settings = nullptr);
    virtual ~AlgorithmModule() = default;

    // these methods are guaranteed to be called from an "algorithm/sensor thread"
    virtual void stop() {}

    virtual void addGyro(double t, const Vector3d &val) = 0;
    virtual void addAcc(double t, const Vector3d &val) = 0;
    virtual void addGps(double t, const Gps &gps) { (void)t; (void)gps; };
    virtual void addJsonData(const json &json) { (void)json; };
    virtual std::string status() const { return ""; }
    virtual int trackingStatus() const  { return -1; };
    virtual bool pose(Pose &pose) const { (void)pose; return false; };

    // these methods are called from the OpenGL thread
    virtual void addFrame(double t, const CameraIntrinsics &cameraIntrinsics) = 0;

    /**
     * If this module supports visualizations, initialize visualizations for
     * given screen size
     * @param width screen / visualization window width
     * @param height screen / visualization window height
     */
    virtual void setupRendering(int width, int height) { (void)width; (void)height; }

    /**
     * Render visualizations, if any
     * @param t Current time (real time)
     */
    virtual void render(double t) { (void)t; }

    // This helper function will protect all calls with a single mutex
    static std::unique_ptr<AlgorithmModule> makeThreadSafe(AlgorithmModule *nonThreadSafe);
};

class CpuAlgorithmModule : public AlgorithmModule {
public:
    virtual void addFrame(double t, const cv::Mat &grayFrame, cv::Mat *colorFrame,
                          const CameraIntrinsics &cameraIntrinsics,
                          cv::Mat &outputColorFrame) = 0;

    void addFrame(double t, const CameraIntrinsics &cameraIntrinsics) final;
    void setupRendering(int width, int height) final;
    void render(double t) final;

    virtual ~CpuAlgorithmModule();

protected:
    CpuAlgorithmModule(int textureId, int width, int height);
    bool visualizationEnabled = true;

private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

#endif