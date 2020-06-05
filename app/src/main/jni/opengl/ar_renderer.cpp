#include "ar_renderer.hpp"
#include "util.hpp"
#include <map>
#include <vector>
#include <Eigen/Dense>

namespace {
namespace cubeModel {
    // issues emerge if using class instead of namespace
    // https://stackoverflow.com/questions/40690260/undefined-reference-error-for-static-constexpr-member

    constexpr float vertices[][3] = {
            { -0.5, -0.5, 0.5 },
            { 0.5, -0.5, 0.5 },
            { -0.5, 0.5, 0.5 },
            { 0.5, 0.5, 0.5 },
            { -0.5, 0.5, -0.5 },
            { 0.5, 0.5, -0.5 },
            { -0.5, -0.5, -0.5 },
            { 0.5, -0.5, -0.5 }
    };

    constexpr float texCoords[][2] = {
            { 0.0, 0.0 },
            { 1.0, 0.0 },
            { 0.0, 1.0 },
            { 1.0, 1.0 }
    };

    constexpr float normals[][3] = {
            { 0.0, 0.0, 1.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, -1.0 },
            { 0.0, -1.0, 0.0 },
            { 1.0, 0.0, 0.0 },
            { -1.0, 0.0, 0.0 }
    };

    constexpr int faceVertices[][3] = {
            { 0, 1, 2 },
            { 2, 1, 3 },
            { 2, 3, 4 },
            { 4, 3, 5 },
            { 4, 5, 6 },
            { 6, 5, 7 },
            { 6, 7, 0 },
            { 0, 7, 1 },
            { 1, 7, 3 },
            { 3, 7, 5 },
            { 6, 0, 4 },
            { 4, 0, 2 }
    };

    constexpr int faceTexCoords[][3] = {
            { 0, 1, 2 },
            { 2, 1, 3 },
            { 0, 1, 2 },
            { 2, 1, 3 },
            { 3, 2, 1 },
            { 1, 2, 0 },
            { 0, 1, 2 },
            { 2, 1, 3 },
            { 0, 1, 2 },
            { 2, 1, 3 },
            { 0, 1, 2 },
            { 2, 1, 3 }
    };

    constexpr int faceNormals[][3] = {
            { 0, 0, 0 },
            { 0, 0, 0 },
            { 1, 1, 1 },
            { 1, 1, 1 },
            { 2, 2, 2 },
            { 2, 2, 2 },
            { 3, 3, 3 },
            { 3, 3, 3 },
            { 4, 4, 4 },
            { 4, 4, 4 },
            { 5, 5, 5 },
            { 5, 5, 5 }
    };

    void generateData(
            const Eigen::Matrix4f &transform,
            std::vector<GLfloat> &vertexData,
            std::vector<GLfloat> &texCoordData,
            std::vector<GLfloat> &normalData) {

        for (const auto &f : faceVertices) {
            for (int j : f) {
                const auto &p0 = vertices[j];
                const Eigen::Vector4f ph(p0[0], p0[1], p0[2], 1);
                const auto p1 = transform*ph;
                for (int k = 0; k < 3; ++k) vertexData.push_back(p1(k));
            }
        }
        for (const auto &f : faceTexCoords) {
            for (int j : f) {
                const auto &p = texCoords[j];
                for (float c : p) {
                    texCoordData.push_back(c);
                }
            }
        }
        for (const auto &f : faceNormals) {
            for (int j : f) {
                const auto &p0 = normals[j];
                const Eigen::Vector4f ph(p0[0], p0[1], p0[2], 0);
                const auto p1 = (transform*ph).normalized();
                for (int k = 0; k < 3; ++k) normalData.push_back(p1(k));
            }
        }
    }
};

struct CubeGridModel {
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    float scale = 0.05;
    int gridW = 4;
    int gridH = 4;

    float gridScale = 0.25f;

    Eigen::Vector3f gridCenter = Eigen::Vector3f(0, 0.5f, -0.3f);
    Eigen::Vector3f lightDirection = Eigen::Vector3f(0.1, 0.3, 1.0);
    Eigen::Matrix4f yIsUpTransform;
    bool yIsUp = false;

    CubeGridModel() {
        yIsUpTransform <<
                1,  0,  0,  0,
                0,  0,  1,  0,
                0, -1,  0,  0,
                0,  0,  0,  1;
    }

    std::vector<GLfloat> getLightDirection() const {
        const auto l = lightDirection.normalized();
        Eigen::Vector4f dirh(l.x(), l.y(), l.z(), 0);
        if (yIsUp) {
            dirh = yIsUpTransform*dirh;
        }

        return { dirh.x(), dirh.y(), dirh.z() };
    }

    void generateData(std::vector<GLfloat> &vertexData, std::vector<GLfloat> &texCoordData, std::vector<GLfloat> &normalData) const {
        for (int ix=0; ix<gridW; ++ix) {
            for (int iy=0; iy<gridH; ++iy) {
                // Eigen quirks: do not define this as "const auto center"
                const Eigen::Vector3f center = gridCenter + gridScale * Eigen::Vector3f(
                        gridW <= 1 ? 0 : (ix/float(gridW - 1)-0.5f),
                        gridH <= 1 ? 0 : (iy/float(gridH - 1)-0.5f),
                        0);

                Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
                transform.block<3, 3>(0, 0) *= scale;
                transform.block<3, 1>(0, 3) = center;

                if (yIsUp) transform = yIsUpTransform * transform;
                cubeModel::generateData(transform, vertexData, texCoordData, normalData);
            }
        }
    }
};

class CubeGridRenderer {
private:
    std::vector<GLfloat> vertexData;
    std::vector<GLfloat> texCoordData;
    std::vector<GLfloat> normalData;

    GLuint shaderProgram = 0;

    GLuint attributeVertex = 0;
    GLuint attributeTexCoord = 0;
    GLuint attributeNormal = 0;

    GLuint uniformModelViewProjection = 0;
    GLuint uniformGridCenter = 0;
    GLuint uniformGridScale = 0;
    GLuint uniformLightDirection = 0;

    CubeGridModel model;

public:
    CubeGridRenderer(bool yIsUp)  {
        log_debug("CubeGridRenderer::CubeGridRenderer");

        const std::string vertexShaderSource = R"(
            uniform mat4 u_ModelViewProjection;
            attribute vec3 a_Position;
            attribute vec3 a_Normal;
            attribute vec2 a_TexCoord;
            varying vec3 v_Normal;
            varying vec2 v_TexCoord;
            varying vec3 v_Position;
            void main() {
                v_Normal = a_Normal;
                v_TexCoord = a_TexCoord;
                v_Position = a_Position;
                gl_Position = u_ModelViewProjection * vec4(a_Position.xyz, 1.0);
            })";

        const std::string fragmentShaderSource = R"(
            precision mediump float;
            varying vec3 v_Normal;
            varying vec2 v_TexCoord;
            varying vec3 v_Position;
            uniform vec3 u_GridCenter;
            uniform vec3 u_LightDirection;
            uniform float u_GridScale;
            void main() {
                const float ambient = 0.4;

                float lighting = max(max(dot(v_Normal, u_LightDirection), 0.0), ambient);

                vec3 relPos = (v_Position.xyz - u_GridCenter.xyz) / u_GridScale + 0.5;
                // NOTE: not the same colors with y=up vs z=up
                vec3 objColor = vec3(relPos.x, relPos.y, 1.0);

                vec2 border = abs(v_TexCoord - 0.5)*2.0;
                if (max(border.x, border.y) > 0.9) {
                    objColor = vec3(1.0);
                }

                gl_FragColor = vec4(objColor * lighting, 1.0);
            }
            )";

        shaderProgram = glUtil::createProgram(vertexShaderSource.c_str(), fragmentShaderSource.c_str());
        assert(shaderProgram && "error compiling shaders");

        // GLSL variables
        attributeVertex = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "a_Position"));
        attributeTexCoord = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "a_TexCoord"));
        attributeNormal = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "a_Normal"));

        uniformModelViewProjection = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_ModelViewProjection"));
        uniformGridCenter = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_GridCenter"));
        uniformGridScale = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_GridScale"));
        uniformLightDirection = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_LightDirection"));

        model.yIsUp = yIsUp;
        model.generateData(vertexData, texCoordData, normalData);

        glUtil::checkError("CubeGridRenderer GLSL setup");
    }

    void render(const Eigen::Matrix4f &modelViewProjection) const {
        if (vertexData.empty()) return;

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(uniformModelViewProjection, 1, GL_FALSE, modelViewProjection.data());
        glUniform3fv(uniformGridCenter, 1, model.gridCenter.data());
        glUniform3fv(uniformLightDirection, 1, model.getLightDirection().data());
        glUniform1f(uniformGridScale, model.gridScale);

        glEnableVertexAttribArray(attributeVertex);
        constexpr unsigned coordsPerVertex = 3;
        glVertexAttribPointer(attributeVertex, coordsPerVertex, GL_FLOAT, GL_FALSE, 0,
                              vertexData.data());

        glEnableVertexAttribArray(attributeTexCoord);
        constexpr unsigned coordsPerTex = 2;
        glVertexAttribPointer(attributeTexCoord, coordsPerTex, GL_FLOAT, GL_FALSE, 0,
                              texCoordData.data());

        glEnableVertexAttribArray(attributeNormal);
        glVertexAttribPointer(attributeNormal, coordsPerVertex, GL_FLOAT, GL_FALSE, 0,
                              normalData.data());

        const auto nVertices = static_cast<GLsizei>(vertexData.size() / coordsPerVertex);
        glDrawArrays(GL_TRIANGLES, 0, nVertices);

        glUseProgram(0);
        glDisableVertexAttribArray(attributeVertex);
        glDisableVertexAttribArray(attributeTexCoord);
        glDisableVertexAttribArray(attributeNormal);

        glUtil::checkError("CubeGridRenderer::render");
    }
};

class ArTrackRenderer {
private:
    std::vector<GLfloat> vertexData;

    GLuint shaderProgram = 0;
    GLuint attributeVertex = 0;

    GLuint uniformModelViewProjection = 0;
    GLuint uniformCurrentPosition = 0;

    double lastNodeTimeStamp = 0.0;
    float currentPosition[3] = { 0, 0, 0 };

public:
    ArTrackRenderer()  {
        log_debug("ArTrackRenderer::ArTrackRenderer");

        const std::string vertexShaderSource = R"(
        uniform mat4 u_ModelViewProjection;
        attribute vec3 a_Position;
        varying vec3 v_Position;
        void main() {
            v_Position = a_Position;
            gl_Position = u_ModelViewProjection * vec4(a_Position.xyz, 1.0);
        })";

        const std::string fragmentShaderSource = R"(
        precision mediump float;
        varying vec3 v_Position;
        uniform vec3 u_CurrentPosition;
        void main() {
            const vec3 COLOR = vec3(1.0, 0.0, 1.0);
            const float DIST_TO_FULL_OPACITY = 1.0;
            float dist = length(v_Position - u_CurrentPosition);
            float alpha = min(1.0,  dist / DIST_TO_FULL_OPACITY);
            gl_FragColor = vec4(COLOR, alpha);
        }
        )";

        shaderProgram = glUtil::createProgram(vertexShaderSource.c_str(), fragmentShaderSource.c_str());
        assert(shaderProgram && "error compiling shaders");

        // GLSL variables
        attributeVertex = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "a_Position"));
        uniformModelViewProjection = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_ModelViewProjection"));
        uniformCurrentPosition = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_CurrentPosition"));

        glUtil::checkError("ArTrackRenderer GLSL setup");
    }

    void setPosition(double t, const float xyz[]) {
        constexpr double NODE_INTERVAL_SEC = 0.02;

        for (int i=0; i<3; ++i) currentPosition[i] = xyz[i];
        if (t > lastNodeTimeStamp + NODE_INTERVAL_SEC) {
            for (int i=0; i<3; ++i) vertexData.push_back(xyz[i]);
            lastNodeTimeStamp = t;
        }

        const auto n = vertexData.size() / 3;
        constexpr int MAX_VERTICES = 100000;
        if (n > MAX_VERTICES) {
            const auto N_TO_ERASE = MAX_VERTICES / 10;
            vertexData.erase(vertexData.begin(), vertexData.begin() + N_TO_ERASE*3);
        }
    }

    void render(const Eigen::Matrix4f &modelViewProjection) const {
        if (vertexData.empty()) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glLineWidth(5);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uniformModelViewProjection, 1, GL_FALSE, modelViewProjection.data());
        glUniform3fv(uniformCurrentPosition, 1, currentPosition);

        glEnableVertexAttribArray(attributeVertex);
        constexpr unsigned coordsPerVertex = 3;
        glVertexAttribPointer(attributeVertex, coordsPerVertex, GL_FLOAT, GL_FALSE, 0,
                              vertexData.data());

        const auto nVertices = static_cast<GLsizei>(vertexData.size() / coordsPerVertex);
        glDrawArrays(GL_LINE_STRIP, 0, nVertices);

        glUseProgram(0);
        glDisableVertexAttribArray(attributeVertex);
        glDisable(GL_BLEND);

        glUtil::checkError("ArTrackRenderer::render");
    }
};

class PointCloudRenderer {
private:
    std::vector<GLfloat> vertexData;

    GLuint shaderProgram = 0;
    GLuint attributeVertex = 0;
    GLuint uniformModelViewProjection = 0;

public:
    PointCloudRenderer()  {
        log_debug("PointCloudRenderer::PointCloudRenderer");

        const std::string vertexShaderSource = R"(
        uniform mat4 u_ModelViewProjection;
        attribute vec3 a_Position;
        void main() {
            gl_PointSize = 5.0;
            gl_Position = u_ModelViewProjection * vec4(a_Position.xyz, 1.0);
        })";

        const std::string fragmentShaderSource = R"(
        precision mediump float;
        void main() {
            const vec3 COLOR = vec3(1.0, 1.0, 0.0);
            gl_FragColor = vec4(COLOR, 1.0);
        }
        )";

        shaderProgram = glUtil::createProgram(vertexShaderSource.c_str(), fragmentShaderSource.c_str());
        assert(shaderProgram && "error compiling shaders");

        // GLSL variables
        attributeVertex = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "a_Position"));
        uniformModelViewProjection = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "u_ModelViewProjection"));

        glUtil::checkError("PointCloudRenderer GLSL setup");
    }

    void setPointCloud(const float *flatData, std::size_t n) {
        assert(n % 3 == 0);
        vertexData = std::vector<float>(flatData, flatData + n);
    }

    void render(const Eigen::Matrix4f &modelViewProjection) const {
        if (vertexData.empty()) return;

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uniformModelViewProjection, 1, GL_FALSE, modelViewProjection.data());

        glEnableVertexAttribArray(attributeVertex);
        constexpr unsigned coordsPerVertex = 3;
        glVertexAttribPointer(attributeVertex, coordsPerVertex, GL_FLOAT, GL_FALSE, 0,
                              vertexData.data());

        const auto nVertices = static_cast<GLsizei>(vertexData.size() / coordsPerVertex);
        glDrawArrays(GL_POINTS, 0, nVertices);

        glUseProgram(0);
        glDisableVertexAttribArray(attributeVertex);

        glUtil::checkError("PointCloudRenderer::render");
    }
};

class ArRendererImplementation : public ArRenderer {
private:
    Eigen::Matrix4f modelViewMatrix;
    Eigen::Matrix4f projectionMatrix;

    const CubeGridRenderer cubeGridRenderer;
    ArTrackRenderer arTrackRenderer;
    PointCloudRenderer pointCloudRenderer;

public:
    ArRendererImplementation(bool yIsUp) : cubeGridRenderer(yIsUp) {}

    void setPose(const double t, const float xyz[], const float quaternion[]) final {
        // just to make it extra clear which element is which
        const float w = quaternion[0];
        const float x = quaternion[1];
        const float y = quaternion[2];
        const float z = quaternion[3];
        const Eigen::Quaternionf q(w, x, y, z);

        const Eigen::Vector3f p(xyz[0], xyz[1], xyz[2]);
        const Eigen::Matrix3f R = q.toRotationMatrix();

        modelViewMatrix = Eigen::Matrix4f::Zero();
        modelViewMatrix.block<3, 3>(0, 0) = R;
        modelViewMatrix.block<3, 1>(0, 3) = -R * p;
        modelViewMatrix(3, 3) = 1;

        arTrackRenderer.setPosition(t, xyz);
    }

    void setPose(const double t, const float mat[]) final {
        modelViewMatrix = Eigen::Map<Eigen::Matrix4f>(const_cast<float*>(mat));

        const Eigen::Matrix3f R = modelViewMatrix.block<3, 3>(0, 0);
        const Eigen::Vector3f p = -R.transpose() * modelViewMatrix.block<3, 1>(0, 3);
        const float xyz[3] = { p(0), p(1), p(2) };
        arTrackRenderer.setPosition(t, xyz);
    }

    void setProjection(int width, int height, float focalLength) final {
        projectionMatrix = Eigen::Matrix4f::Zero();

        // OpenGL near and far clip plane distances (meters)
        constexpr float zNear = 0.01;
        constexpr float zFar = 20;

        projectionMatrix(2, 2) = -(zFar + zNear) / (zFar - zNear);
        projectionMatrix(2, 3) = -2 * zFar * zNear / (zFar - zNear);
        projectionMatrix(3, 2) = -1;

        // normal version
        //projectionMatrix(0, 0) = 2 * focalLength / width;
        //projectionMatrix(1, 1) = 2 * focalLength / height;

        // rotated image: flip X & Y
        projectionMatrix(0, 1) = -2 * focalLength / width;
        projectionMatrix(1, 0) = 2 * focalLength / height;
    }

    void setProjectionMatrix(const float matrix[]) final {
        projectionMatrix = Eigen::Map<Eigen::Matrix4f>(const_cast<float*>(matrix));
    }

    void setPointCloud(const float *flatData, std::size_t n) final {
        pointCloudRenderer.setPointCloud(flatData, n);
    }

    void render() const final {
        const Eigen::Matrix4f modelViewProjection = projectionMatrix * modelViewMatrix;

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);

        cubeGridRenderer.render(modelViewProjection);
        arTrackRenderer.render(modelViewProjection);
        pointCloudRenderer.render(modelViewProjection);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
};
}

std::unique_ptr<ArRenderer> ArRenderer::buildWithYIsUp() {
    return std::unique_ptr<ArRenderer>(new ArRendererImplementation(true));
}

std::unique_ptr<ArRenderer> ArRenderer::buildWithZIsUp() {
    return std::unique_ptr<ArRenderer>(new ArRendererImplementation(false));
}