#pragma once

#include <Eigen/Dense>

namespace ControllerModule {
    namespace Scene {
        /// Represents the camera used to render the scene.
        /// The camera can be transformed and allows access to the transforamtion matrices.
        class Camera {
        public:
            virtual ~Camera() = default;

            /// Creates camera with default values in all parameters - does not represent a usable
            /// camera object.
            Camera(float aspect);

            /// Creates a camera with the given parameters.
            /// @param position Position of the camera
            /// @param target Target point of the camera - camera will look towards this point
            /// @param up Up vector of the camera - defines orientation around the forward direction
            /// @param fov The vertical field of view of the camera specified in degrees
            /// @param aspect Aspect ration of the camera
            /// @param zNear Near clipping distance of the camera
            /// @param zFar Far clipping distance of the camera
            /// @param perspective Whether the camera is perspective(true) or orthographic(false)
            Camera(const Eigen::Vector3f& position, const Eigen::Vector3f& target, const Eigen::Vector3f& up, float fov, float aspect, float zNear, float zFar, bool perspective = true);

            /// Translates the camera by the offset given as a 3D vector.
            /// Moves both the camera itself and its corresponding target point.
            void Translate(const Eigen::Vector3f& offset);
            /// Translates the camera by the offset vector (dx, dy, dz).
            /// Moves both the camera itself and its corresponding target point.
            void Translate(float dx, float dy, float dz);

            /// Translates the camera by an offset given as 3D vector in the local frame of the camera.
            /// Moves both the camera itself and its corresponding target point.
            void TranslateLocal(const Eigen::Vector3f& offset);
            /// Translates the camera by the vector (dx,dy,dz) in the local frame of the camera.
            /// Moves both the camera itself and its corresponding target point.
            void TranslateLocal(float dx, float dy, float dz);

            /// Rotates the camera around the specified axis by an angle specified in radians.
            void Rotate(const Eigen::Vector3f& axis, float angle);
            /// Rotates the camera around the specified axis given by vector (x,y,z) by an angle
            /// specified in radians.
            void Rotate(float x, float y, float z, float angle);

            /// Rotates the camera around the specified axis by an angle specified in radians in
            /// the local frame of the camera.
            void RotateLocal(const Eigen::Vector3f& axis, float angle);
            /// Rotates the camera around the axis given as vector (x,y,z) by an angle specified in
            /// radians in the local frame of the camera.
            void RotateLocal(float x, float y, float z, float angle);

            /// Sets the position of the camera in (position, target, up) camera configuration.
            void SetPosition(const Eigen::Vector3f& position);
            /// Sets the position of the camera in (position, target, up) camera configuration.
            void SetPosition(float x, float y, float z);

            /// Sets the target point of the camera in (position, target, up) camera configuration.
            void SetTarget(const Eigen::Vector3f& target);
            /// Sets the target point of the camera in (position, target, up) camera configuration.
            void SetTarget(float x, float y, float z);

            /// Sets the up vector of the camera in (position, target, up) camera configuration.
            void SetUp(const Eigen::Vector3f& up);
            /// Sets the up vector of the camera in (position, target, up) camera configuration.
            void SetUp(float x, float y, float z);

            /// Getter for the position of the camera in the (position, target, up) camera configuration.
            const Eigen::Vector3f& GetPosition() const;

            /// Getter for the target point of the camera in (position, target, up) camera configuration.
            const Eigen::Vector3f& GetTarget() const;

            /// Getter for the up vector in the (position, target, up) camera configuration.
            const Eigen::Vector3f& GetUpNormalized() const;

            /// Getter for the forward vector of the camera.
            /// The forward vector is defined as target - position
            Eigen::Vector3f GetForward() const;

            /// Getter for the normalized version of the forward vector.
            /// The forward vector is defined as target-position
            Eigen::Vector3f GetForwardNormalized() const;

            /// Getter for the cross product of the forward vector and the up vector => (right) side vector
            Eigen::Vector3f GetAsideNormalized() const;

            /// Sets whether the camera is perspective or orthographic.
            /// @param perspective Whether the camera is perspective. True value means the camera
            ///		   should be perspective.
            void SetPerspective(bool perspective);

            /// Sets the frustum parameters of the camera.
            /// @param fov Vertical field of view of the camera in degrees.
            /// @param aspect The aspect ratio of the camera frustum
            /// @param zNear Near distance of the camera frustum.
            /// @param zFar Far distance of the camera frustum.
            void SetFrustum(float fov, float aspect, float zNear, float zFar);

            /// Sets the aspect ratio of the camera.
            void SetAspect(float aspect);

            /// Sets the near and far distances of the camera frustum.
            void SetNearFar(float zNear, float zFar);

            /// Sets the near distance of the camera frustum.
            void SetNear(float zNear);

            /// Sets the far distance of the camera frustum
            void SetFar(float zFar);

            /// Sets the field of view of the camera in degrees.
            /// @param fov Vertical field of view of the camera.
            void SetFov(float fov);

            /// Getter which returns whether the camera is perspective or orthographic.
            /// @returns True if the camera is perspective, false otherwise(orthographic)
            bool GetPerspective() const;

            /// Getter for the aspect ratio of the camera.
            float GetAspect() const;

            /// Getter for the near distance of the camera frustum.
            float GetNear() const;

            /// Getter for the far distance of the camera frustum.
            float GetFar() const;

            /// Getter for the field of view of the camera.
            /// @returns Fielf o view in degrees.
            float GetFov() const;

            /// Updates view and projection matrices based on the current camera settings.
            void UpdateViewProjectionMatrices();

            /// Returns the lookAt camera matrix.
            const Eigen::Matrix4f& GetViewMatrix() const;

            /// Returns the projection matrix of the camera computed with the cameras frustum parameters.
            const Eigen::Matrix4f& GetProjectionMatrix() const;

            /// Returns the inverse of projection and view matrix multiplication.
            Eigen::Matrix4f GetViewProjectionInverseMatrix() const;

        private:
            Eigen::Vector3f position_;
            Eigen::Vector3f target_;
            Eigen::Vector3f up_;

            float zNear_;
            float zFar_;
            float fov_;
            float aspect_;

            Eigen::Matrix4f viewMatrix_;
            Eigen::Matrix4f projectionMatrix_;

            bool viewMatrixDirty_ = true;
            bool projectionMatrixDirty_ = true;

            bool perspective_;

            Eigen::Matrix3f GetLocalToGlobalMatrix() const;

            /// Creates the lookAt matrix.
            void UpdateViewMatrix();

            /// Creates projection matrix.
            void UpdateProjectionMatrix();
        };
    } // namespace Scene
} // namespace ControllerModule
