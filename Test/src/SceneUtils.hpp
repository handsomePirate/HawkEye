#pragma once

#include <Eigen/Dense>

namespace ControllerModule {
    enum class TransformedAABBComputationType {
        /// Defines that no transformation is applied to the bounding box.
        /// It will be copied from underlying resources, but no additional computation takes place.
        NONE,
        /// Computes the new bounding box exactly - for example by transforming all vertices of the
        /// given geometry.
        EXACT,
        /// Computes the new bounding box cheaply - for instance, approximate new bounding box from
        /// trasnformation of the original boudning box.
        CHEAP
    };

    /// Static class with utility methods for handling scene objects.
    class SceneUtils {
    public:
        /// Epsilon value used to define equality for floating point numbers.
        static constexpr float FLOAT_EPSILON = 0.0001f;

        /// Creates perspective camera matrix out of the camera frustum parameters passed as arguments to
        /// the function.
        /// @param fovy Vertical field of view of the camera given in radians
        /// @param aspect Aspect ratio of the camera
        /// @param zNear Near distance of the camera frustum
        /// @param zFar Far distance of the camera frustum
        template <class T>
        static Eigen::Matrix<T, 4, 4> Perspective(
            T fovy,
            T aspect,
            T zNear,
            T zFar) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;

            assert(aspect > 0);
            assert(zFar > zNear);

            const double radf = fovy;

            const double tanHalfFovy = tan(radf / 2.0);
            Matrix4      res = Matrix4::Zero();
            res(0, 0) = T(1.0 / (aspect * tanHalfFovy));
            res(1, 1) = T(1.0 / (tanHalfFovy));
            res(2, 2) = T(-(zFar + zNear) / (zFar - zNear));
            res(3, 2) = T(-1.0);
            res(2, 3) = T(-(T(2.0) * zFar * zNear) / (zFar - zNear));
            return res;
        }

        /// Creates orthographic camera matrix out of the camera frustum parameters passed as arguments
        /// to the function.
        /// @param aspect Aspect ratio of the camera
        /// @param zNear Near distance of the camera frustum
        /// @param zFar Far distance of the camera frustum
        template <class T>
        static Eigen::Matrix<T, 4, 4> Orthographic(
            T aspect,
            T zNear,
            T zFar) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;

            const float top = 1;
            const float bottom = -1;
            const float right = top * aspect;
            const float left = bottom * aspect;
            const float scale = 7;
            Matrix4     res;
            res << scale / (right - left), 0, 0, -(right + left) / (right - left),
                0, scale / (top - bottom), 0, -(top + bottom) / (top - bottom),
                0, 0, -scale / (zFar - zNear), -(zFar + zNear) / (zFar - zNear),
                0, 0, 0, 1;
            return res;
        }

        /// Creates the lookAt camera matrix.
        /// @param eye The position of the camera, also known as the origin or the eye of the camera.
        /// @param target The target point of the camera in the (origin, target, up) camera configuration.
        /// @param up The up vector of the camera in the (origin, target, up) camera configuration.
        template <class T>
        static Eigen::Matrix<T, 4, 4> LookAt(
            const Eigen::Matrix<T, 3, 1>& eye,
            const Eigen::Matrix<T, 3, 1>& target,
            const Eigen::Matrix<T, 3, 1>& up) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;
            using Vector3 = Eigen::Matrix<T, 3, 1>;

            const Vector3 f = (target - eye).normalized();
            Vector3       u = up.normalized();
            const Vector3 s = f.cross(u).normalized();
            u = s.cross(f);

            Matrix4 res;
            res << s.x(), s.y(), s.z(), -s.dot(eye),
                u.x(), u.y(), u.z(), -u.dot(eye),
                -f.x(), -f.y(), -f.z(), f.dot(eye),
                0, 0, 0, 1;

            return res;
        }

        /// Creates a scaling matrix out of the given vector.
        /// Constructs a 4x4 matrix where the first 3 diagonal elements are given
        /// by the scale and the fourth is 1, the remaining elements are zero.
        /// @param scale The scaling vector
        template <class T>
        static Eigen::Matrix<T, 4, 4> ScaleMatrix(
            const Eigen::Matrix<T, 3, 1> scale) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;

            Matrix4 scaleMatrix;
            scaleMatrix.setZero();
            for (int32_t i = 0; i < 3; ++i) {
                scaleMatrix(i, i) = scale(i);
            }
            scaleMatrix(3, 3) = T(1.0);

            return scaleMatrix;
        }

        /// Creates a translational matrix out of the given vector.
        /// Constructs a 4x4 for matrix representing standard 3D translation in homogeneous
        /// coordinates. In multiplication, this matrix should stand on the left side of the
        /// transformed vector.
        /// @param translation The translation vector.
        template <class T>
        static Eigen::Matrix<T, 4, 4> TranslationMatrix(
            const Eigen::Matrix<T, 3, 1> translation) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;

            Matrix4 translationMatrix;
            translationMatrix.setZero();
            for (int32_t i = 0; i < 3; ++i) {
                translationMatrix(i, 3) = translation(i);
                translationMatrix(i, i) = T(1.0);
            }
            translationMatrix(3, 3) = T(1.0);

            return translationMatrix;
        }

        /// Creates a rotation matrix out three angles for the rotation around the X, Y and Z
        /// axes.
        /// Constructs a 4x4 matrix representing combined rotation around three main axes.
        /// @param rotation A vector of rotation angles.
        template <class T>
        static Eigen::Matrix<T, 4, 4> RotationMatrixEuler(
            const Eigen::Matrix<T, 3, 1>& rotation) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;

            T cosx = cos(rotation(0));
            T sinx = sin(rotation(0));
            T cosy = cos(rotation(1));
            T siny = sin(rotation(1));
            T cosz = cos(rotation(2));
            T sinz = sin(rotation(2));

            Matrix4 rotationX;
            rotationX << 1, 0, 0, 0,
                0, cosx, -sinx, 0,
                0, sinx, cosx, 0,
                0, 0, 0, 1;
            Matrix4 rotationY;
            rotationY << cosy, 0, siny, 0,
                0, 1, 0, 0,
                -siny, 0, cosy, 0,
                0, 0, 0, 1;
            Matrix4 rotationZ;
            rotationX << cosz, -sinz, 0, 0,
                sinz, cosz, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1;

            Matrix4 res;

            return rotationZ * rotationY * rotationX;
        }

        /// Converts angle given in degrees into angle in radians.
        template <class T>
        static T DegsToRads(T degrees) {
            return T((degrees / 180.0) * EIGEN_PI);
        }

        /// Converts angle given in radians into angle in degrees.
        template <class T>
        static T RadsToDegs(T radians) {
            return T((radians / EIGEN_PI) * 180.0);
        }

        /// Returns whether two float values are equal up to a margin of error.
        /// Uses SceneUtils::FLOAT_EPSILON
        static bool FloatEqual(float v1, float v2) {
            // Equality is there to complement other float comparison methods.
            return abs(v1 - v2) <= FLOAT_EPSILON;
        }

        /// Returns whether the first value is less than the second value up to a margin of error.
        /// Uses SceneUtils::FLOAT_EPSILON
        static bool FloatLessThan(float v1, float v2) {
            return (v2 - v1) > FLOAT_EPSILON;
        }

        /// Returns whether the first value is greater than the second value up to a margin of error.
        /// Uses SceneUtils::FLOAT_EPSILON
        static bool FloatGreaterThan(float v1, float v2) {
            return (v1 - v2) > FLOAT_EPSILON;
        }

        /// Creates a model matrix for transformation of a rigid body.
        /// Constructs a 4x4 matrix which first scales, then rotates and finally translates
        /// by scaled translation.
        /// This matrix should stand on the left side in multiplication.
        /// @param translation The translation vector.
        /// @param rotationQuaternion Rotation of the rigid body given by quaternion.
        /// @param scale The scaling vector.
        template <class T>
        static Eigen::Matrix<T, 4, 4> ModelMatrix(
            const Eigen::Matrix<T, 3, 1>& translation,
            const Eigen::Quaternion<T>& rotationQuaternion,
            const Eigen::Matrix<T, 3, 1>& scale) {
            using Matrix3 = Eigen::Matrix<T, 3, 3>;
            using Matrix4 = Eigen::Matrix<T, 4, 4>;
            using Vector3 = Eigen::Matrix<T, 3, 1>;

            Matrix4 rotationMatrix;
            rotationMatrix.setZero();
            Matrix3 tempRotation = rotationQuaternion.toRotationMatrix();
            for (int32_t i = 0; i < 3; ++i) {
                for (int32_t j = 0; j < 3; ++j) {
                    rotationMatrix(i, j) = tempRotation(i, j);
                }
            }
            rotationMatrix(3, 3) = T(1.0);
            Matrix4 scaleMatrix = ScaleMatrix(scale);
            Vector3 nTranslation = translation;
            /*nTranslation(0) /= scale(0);
            nTranslation(1) /= scale(1);
            nTranslation(2) /= scale(2);*/
            Matrix4 translationMatrix = TranslationMatrix(nTranslation);

            return translationMatrix * rotationMatrix * scaleMatrix;
        }

        /// Creates a model matrix for transformation of a rigid body.
        /// Constructs a 4x4 matrix which first scales, then rotates and finally translates
        /// by scaled translation.
        /// This matrix should stand on the left side in multiplication.
        /// @param translation The translation vector.
        /// @param rotation Rotation of the rigid body given by angles around the major axes.
        /// @param scale The scaling vector.
        template <class T>
        static Eigen::Matrix<T, 4, 4> ModelMatrix(
            const Eigen::Matrix<T, 3, 1>& translation,
            const Eigen::Matrix<T, 3, 1>& rotation,
            const Eigen::Matrix<T, 3, 1>& scale) {
            using Matrix4 = Eigen::Matrix<T, 4, 4>;
            using Vector3 = Eigen::Matrix<T, 3, 1>;

            Matrix4 rotationMatrix = RotationMatrix(rotation);
            Matrix4 scaleMatrix = ScaleMatrix(scale);
            Vector3 nTranslation = translation;
            /*nTranslation(0) /= scale(0);
            nTranslation(1) /= scale(1);
            nTranslation(2) /= scale(2);*/
            Matrix4 translationMatrix = TranslationMatrix(nTranslation);

            Matrix4 res;

            return translationMatrix * rotationMatrix * scaleMatrix;
        }

        /// Decomposes transformation matrix into its individual components.
        /// Decomposes only translation, rotation and scale. If the matrix contains skew
        /// then this method will have incorrect results.
        /// TODO: Extend for skew term.
        /// @param matrix Transformation matrix do decompose.
        /// @return Tuple of translation vector, rotation quaternion and scaling vector.
        template <class T>
        static std::tuple<Eigen::Matrix<T, 3, 1>, Eigen::Quaternion<T>, Eigen::Matrix<T, 3, 1>>
            DecomposeTransformation(const Eigen::Matrix<T, 4, 4>& matrix) {
            using Matrix3 = Eigen::Matrix<T, 3, 3>;
            using Vector3 = Eigen::Matrix<T, 3, 1>;

            Vector3 translation(matrix(0, 3), matrix(1, 3), matrix(2, 3));

            T scaleX = sqrt(matrix(0, 0) * matrix(0, 0) + matrix(1, 0) * matrix(1, 0) + matrix(2, 0) * matrix(2, 0));
            T scaleY = sqrt(matrix(0, 1) * matrix(0, 1) + matrix(1, 1) * matrix(1, 1) + matrix(2, 1) * matrix(2, 1));
            T scaleZ = sqrt(matrix(0, 2) * matrix(0, 2) + matrix(1, 2) * matrix(1, 2) + matrix(2, 2) * matrix(2, 2));

            Matrix3 rotMat;
            rotMat << matrix(0, 0) / scaleX, matrix(0, 1) / scaleY, matrix(0, 2) / scaleZ,
                matrix(1, 0) / scaleX, matrix(1, 1) / scaleY, matrix(1, 2) / scaleZ,
                matrix(2, 0) / scaleX, matrix(2, 1) / scaleY, matrix(2, 2) / scaleZ;

            return { translation, Eigen::Quaternion<T>(rotMat), Eigen::Matrix<T, 3, 1>(scaleX, scaleY, scaleZ) };
        }

        /// Computes the normal transforamtion matrix which is transposed inverse of the model
        /// matrix. It is possible to pass in inverted matrix for better performance - no need
        /// of computing inverse of arbitrary matrix. This function is triggered by the optional
        /// parameter.
        /// @param modelMatrix Model matrix which is the base for computation. Should be inverted if
        ///		   the second parameter is true.
        /// @param isInverted Whether the model matrix is already inverted.
        /// @return Transposed (inverse if the input is not inverted already) of the model matrix.
        template <typename T>
        static Eigen::Matrix<T, 4, 4> ComputeNormalTransformationMatrix(
            const Eigen::Matrix<T, 4, 4>& modelMatrix,
            bool                          isInverted = false) {
            if (isInverted) {
                return modelMatrix.transpose();
            }
            else {
                return modelMatrix.inverse().transpose();
            }
        }
    };
} // namespace ControllerModule
