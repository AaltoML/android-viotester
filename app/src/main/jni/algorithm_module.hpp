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

    virtual void addGyro(double t, const Vector3d &val) = 0;
    virtual void addAcc(double t, const Vector3d &val) = 0;
    virtual Pose addFrame(double t, const cv::Mat &grayFrame, cv::Mat *colorFrame,
                          int cameraInd, double focalLength, double px, double py) = 0;
    virtual void addGps(double t, const Gps &gps) {};
    virtual void addJsonData(const json &json) {};

    /**
     * If this module supports visualizations, initialize visualizations for
     * given screen size
     * @param width screen / visualization window width
     * @param height screen / visualization window height
     * @return true if visualizations are supported, false otherwise
     */
    virtual bool setupRendering(int width, int height) { (void)width; (void)height; return false; }

    /**
     * @param outMat MUST be CV_8UC4, and the same size (or the same object) as created by
     *  setupRendering, which must be called first
     * @return true if something was rendered. false otherwise
     */
    virtual void render() {}
    virtual std::string status() const { return ""; }
    virtual int trackingStatus() { return -1; };

    virtual ~AlgorithmModule() = default;

    static std::unique_ptr<AlgorithmModule> build(int width, int height, const std::string &name, const json *settings = nullptr);
};

#endif