#include "algorithm_module.hpp"
#include <cassert>

using AlgoPtr = std::unique_ptr<AlgorithmModule>;
using json = AlgorithmModule::json;

AlgoPtr buildTracking(int w, int h, const json &settings);
AlgoPtr buildRecorder(int w, int h, const json &settings);
AlgoPtr buildCameraCalibrator(int w, int h);

AlgoPtr AlgorithmModule::build(int width, int height, const std::string &name, const json *settings) {
    if (name == "calibration") {
        return buildCameraCalibrator(width, height);
    } else if (name == "recording" || name == "external") {
        return buildRecorder(width, height, *settings);
    } else if (name == "tracking") {
        return buildTracking(width, height, *settings);
    } else {
        assert(false && "no such module");
    }
}