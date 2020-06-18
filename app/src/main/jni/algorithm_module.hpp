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
     * If this module supports visualizations, initialize the given matrix to the
     * correct size (can be assumed to be of type CV_8UC4) and return true
     * @param output Initialized cv::Mat if visualization are supported, otherwise undefined
     * @return true if visualizations are supported, false otherwise
     */
    virtual bool setupRendering(cv::Mat &output) { (void)output; return false; }

    /**
     * @param outMat MUST be CV_8UC4, and the same size (or the same object) as created by
     *  setupRendering, which must be called first
     * @return true if something was rendered. false otherwise
     */
    virtual bool render(cv::Mat &outMat) { return false; }
    virtual std::string status() const { return ""; }

    virtual ~AlgorithmModule() = default;

    static std::unique_ptr<AlgorithmModule> build(int width, int height, const std::string &name, const json *settings = nullptr);
};

#endif