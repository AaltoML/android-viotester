#include "algorithm_module.hpp"
#include <cassert>

using AlgoPtr = std::unique_ptr<AlgorithmModule>;
using json = AlgorithmModule::json;

AlgoPtr buildRecorder(int textureId, int w, int h, const json &settings);
AlgoPtr buildCameraCalibrator(int textureId, int w, int h);
AlgoPtr buildTracking(int textureId, int w, int h, const json &settings);
AlgoPtr buildGpuExample(int textureId, int w, int h, const json &settings);

AlgoPtr AlgorithmModule::build(int textureId, int width, int height, const std::string &name, const json *settings) {
    if (name == "calibration") {
#ifdef USE_CAMERA_CALIBRATOR
        return buildCameraCalibrator(textureId, width, height);
#else
        assert(false && "Camera calibrator not built");
#endif
    } else if (name == "recording" || name == "external") {
        return buildRecorder(textureId, width, height, *settings);
    } else if (name == "tracking") {
#ifdef USE_CUSTOM_VIO
        return buildTracking(textureId, width, height, *settings);
#else
        assert(false && "custom VIO not built");
#endif
    } else if (name == "gpu_examples") {
#ifdef USE_GPU_EXAMPLES
        return buildGpuExample(textureId, width, height, *settings);
#else
        assert(false && "GPU examples not built");
#endif
    } else {
        assert(false && "no such module");
    }
}