#include "../algorithm_module.hpp"
#include "opengl/algorithm_renderer.hpp"

#include <iomanip>
#include <sstream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <algorithm_module.hpp>
#include "logging.hpp" // logger

namespace {
class CameraCalibrator : public AlgorithmModule {
private:
    const cv::Size patternSize;

    std::vector<cv::Point3f> patternPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;

    std::vector<cv::Point2f> centers;
    std::vector<std::vector<cv::Point3f>> objectPoints;

    cv::Mat colorMat;

    // outputs
    cv::Mat cameraMatrix;
    std::vector<double> distCoeffs;
    std::vector<cv::Mat> rvecs, tvecs;
    std::vector<cv::Point3f> newObjPoints;

    bool ready = false;
    double lastUpdateTime = 0;

    static constexpr unsigned MIN_POSES = 3;
    static constexpr unsigned MAX_POSES = 30;
    static constexpr long POSE_INTERVAL_MILLIS = 500;

    void calibrate() {
        if (imagePoints.size() < MIN_POSES) return;
        objectPoints.resize(imagePoints.size(), patternPoints);

        int iFixedPoint = -1;
        //iFixedPoint = patternSize.width - 1; // ?

        cameraMatrix = cv::Mat::eye(3, 3, CV_64F);

        double rms = cv::calibrateCameraRO(objectPoints, imagePoints, colorMat.size(), iFixedPoint,
                                           cameraMatrix, distCoeffs, rvecs, tvecs, newObjPoints,
                                           cv::CALIB_FIX_ASPECT_RATIO | cv::CALIB_FIX_K3 | cv::CALIB_USE_LU);
        log_debug("RMS error reported by calibrateCamera: %g\n", rms);

        bool ok = cv::checkRange(cameraMatrix) && cv::checkRange(distCoeffs);
        if (ok) {
            ready = true;
            log_debug("calibration success. Focal lengths %g, %g", cameraMatrix.at<double>(0,0), cameraMatrix.at<double>(0,0));
        }
    }

public:
    CameraCalibrator(int w, int h) : patternSize(4, 11)
    {
        colorMat = cv::Mat(h, w, CV_8UC4);
        const float squareSize = 1.0;
        for( int i = 0; i < patternSize.height; i++ )
            for( int j = 0; j < patternSize.width; j++ )
                patternPoints.push_back(cv::Point3f(
                        ((2*j + i % 2)*squareSize),
                        (i*squareSize),
                        0));
        log_debug("initialized camera calibrator");
    }

    Pose addFrame(double t, const cv::Mat &grayMat, cv::Mat *rgbaMat,
                  int cameraInd, double focalLength, double px, double py) final {
        assert(rgbaMat != nullptr);
        assert(!colorMat.empty());
        rgbaMat->copyTo(colorMat);
        const cv::Size patternSize(4, 11);
        centers.clear();
        // https://raw.githubusercontent.com/opencv/opencv/master/doc/acircles_pattern.png
        if (cv::findCirclesGrid(grayMat, patternSize, centers, cv::CALIB_CB_ASYMMETRIC_GRID)) {
            log_debug("detected circles with %zu center(s)", centers.size());
            if (t > lastUpdateTime + POSE_INTERVAL_MILLIS * 1e-3) {
                lastUpdateTime = t;
                imagePoints.push_back(centers);
                if (imagePoints.size() > MAX_POSES) {
                    imagePoints.erase(imagePoints.begin());
                }
            }
            calibrate();

        } else {
            centers.clear();
        }

        return {};
    }

    bool setupRendering(cv::Mat &outMat) final {
        outMat = colorMat.clone();
        return true;
    }

    bool render(cv::Mat &outMat) final {
        assert(!colorMat.empty() && !outMat.empty());

        constexpr int RADIUS = 10;
        colorMat.copyTo(outMat);
        for (const auto &c : centers) {
            cv::circle(outMat, c, RADIUS, cv::Scalar(0xff, 0x0, 0xff));
        }
        defaultOpenGLRenderer::setBgraCameraData(outMat.cols, outMat.rows, outMat.data);
        return true;
    }

    std::string status() const final {
        std::ostringstream oss;
        oss << "n pose(s): " << imagePoints.size() << "\n";

        if (ready) {
            oss << "Focal lengths: " << std::setprecision(4)
                << cameraMatrix.at<double>(0, 0) << ", "
                << cameraMatrix.at<double>(1, 1) << "\n"
                << "Principal point: "
                << cameraMatrix.at<double>(0, 2) << ", "
                << cameraMatrix.at<double>(1, 2) << "\n"
                << "Distortion:";
            for (double d : distCoeffs) {
                oss << "\n" << d;
            }
        } else {
            oss << "not ready";
        }
        oss << "\n";
        return oss.str();
    }

    // ignore IMU data
    virtual void addGyro(double t, const Vector3d &val) final {
        (void)t; (void)val;
    }

    virtual void addAcc(double t, const Vector3d &val) final {
        (void)t; (void)val;
    }
};
}

std::unique_ptr<AlgorithmModule> buildCameraCalibrator(int width, int heigth) {
    return std::unique_ptr<AlgorithmModule>(new CameraCalibrator(width, heigth));
}