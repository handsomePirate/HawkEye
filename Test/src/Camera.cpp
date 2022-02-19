
#include "Camera.hpp"

#include "SceneUtils.hpp"

using namespace ControllerModule;
using namespace Scene;

Camera::Camera(const float aspect) :
	position_(0.f, 0.f, 2.f),
	target_(0.f, 0.f, 0.f),
	up_(0.f, 1.f, 0.f),
	zNear_(0.01f),
	zFar_(10000.f),
	fov_(SceneUtils::DegsToRads(60.f)),
	aspect_(aspect),
	perspective_(true) {
	UpdateViewProjectionMatrices();
}

Camera::Camera(const Eigen::Vector3f& position, const Eigen::Vector3f& target, const Eigen::Vector3f& up, const float fov, const float aspect,
	const float zNear, const float zFar, const bool perspective) :
	position_(position),
	target_(target),
	up_(up.normalized()),
	zNear_(zNear),
	zFar_(zFar),
	fov_(SceneUtils::DegsToRads(fov)),
	aspect_(aspect),
	perspective_(perspective) {
	UpdateViewProjectionMatrices();
}

void Camera::Translate(const Eigen::Vector3f& offset) {
	position_ += offset;
	target_ += offset;
	viewMatrixDirty_ = true;
}

void Camera::Translate(const float dx, const float dy, const float dz) {
	Translate(Eigen::Vector3f(dx, dy, dz));
}

void Camera::TranslateLocal(const Eigen::Vector3f& offset) {
	Translate(GetLocalToGlobalMatrix() * offset);
}

void Camera::TranslateLocal(const float dx, const float dy, const float dz) {
	TranslateLocal(Eigen::Vector3f(dx, dy, dz));
}

void Camera::Rotate(const Eigen::Vector3f& axis, const float angle) {
	if (axis.isZero() || angle == 0) { return; }

	const auto axisRotation = Eigen::AngleAxisf(angle, axis);
	target_ = axisRotation * GetForward() + position_;
	up_ = axisRotation * up_;

	viewMatrixDirty_ = true;
}

void Camera::Rotate(const float x, const float y, const float z, const float angle) {
	Rotate(Eigen::Vector3f(x, y, z), angle);
}

void Camera::RotateLocal(const Eigen::Vector3f& axis, const float angle) {
	Rotate(GetLocalToGlobalMatrix() * axis, angle);
}

void Camera::RotateLocal(const float x, const float y, const float z, const float angle) {
	RotateLocal(Eigen::Vector3f(x, y, z), angle);
}

void Camera::SetPosition(const Eigen::Vector3f& position) {
	position_ = position;
	viewMatrixDirty_ = true;
}

void Camera::SetPosition(const float x, const float y, const float z) {
	SetPosition(Eigen::Vector3f(x, y, z));
}

void Camera::SetTarget(const Eigen::Vector3f& target) {
	target_ = target;
	viewMatrixDirty_ = true;
}

void Camera::SetTarget(const float x, const float y, const float z) {
	SetTarget(Eigen::Vector3f(x, y, z));
}

void Camera::SetUp(const Eigen::Vector3f& up) {
	up_ = up.normalized();
	viewMatrixDirty_ = true;
}

void Camera::SetUp(const float x, const float y, const float z) {
	SetUp(Eigen::Vector3f(x, y, z));
}

const Eigen::Vector3f& Camera::GetPosition() const { return position_; }

const Eigen::Vector3f& Camera::GetTarget() const { return target_; }

const Eigen::Vector3f& Camera::GetUpNormalized() const { return up_; }

Eigen::Vector3f Camera::GetForward() const { return target_ - position_; }

Eigen::Vector3f Camera::GetForwardNormalized() const { return GetForward().normalized(); }

Eigen::Vector3f Camera::GetAsideNormalized() const { return GetForwardNormalized().cross(up_).normalized(); }

void Camera::SetPerspective(const bool perspective) {
	perspective_ = perspective;
	projectionMatrixDirty_ = true;
}

void Camera::SetFrustum(const float fov, const float aspect, const float zNear, const float zFar) {
	fov_ = SceneUtils::DegsToRads(fov);
	aspect_ = aspect;
	zNear_ = zNear;
	zFar_ = zFar;
	projectionMatrixDirty_ = true;
}

void Camera::SetAspect(const float aspect) {
	aspect_ = aspect;
	projectionMatrixDirty_ = true;
}

void Camera::SetNearFar(const float zNear, const float zFar) {
	zNear_ = zNear;
	zFar_ = zFar;
	projectionMatrixDirty_ = true;
}

void Camera::SetNear(const float zNear) {
	zNear_ = zNear;
	projectionMatrixDirty_ = true;
}

void Camera::SetFar(const float zFar) {
	zFar_ = zFar;
	projectionMatrixDirty_ = true;
}

void Camera::SetFov(const float fov) {
	fov_ = SceneUtils::DegsToRads(fov);
	projectionMatrixDirty_ = true;
}

bool Camera::GetPerspective() const { return perspective_; }

float Camera::GetAspect() const { return aspect_; }

float Camera::GetNear() const { return zNear_; }

float Camera::GetFar() const { return zFar_; }

float Camera::GetFov() const { return SceneUtils::RadsToDegs(fov_); }

void Camera::UpdateViewProjectionMatrices() {
	UpdateViewMatrix();
	UpdateProjectionMatrix();
}

const Eigen::Matrix4f& Camera::GetViewMatrix() const { return viewMatrix_; }

const Eigen::Matrix4f& Camera::GetProjectionMatrix() const { return projectionMatrix_; }

Eigen::Matrix4f Camera::GetViewProjectionInverseMatrix() const {
	return (projectionMatrix_ * viewMatrix_).inverse();
}

Eigen::Matrix3f Camera::GetLocalToGlobalMatrix() const {
	const auto aside = GetAsideNormalized();
	const auto up = GetUpNormalized();
	const auto forward = GetForwardNormalized();

	Eigen::Matrix3f m;
	m << aside, up, forward;

	return m;
}

void Camera::UpdateViewMatrix() {
	if (!viewMatrixDirty_) { return; }

	viewMatrix_ = SceneUtils::LookAt<float>(position_, target_, up_);
	viewMatrixDirty_ = false;
}

void Camera::UpdateProjectionMatrix() {
	if (!projectionMatrixDirty_) { return; }

	if (perspective_) {
		projectionMatrix_ = SceneUtils::Perspective<float>(fov_, aspect_, zNear_, zFar_);
	}
	else {
		projectionMatrix_ = SceneUtils::Orthographic<float>(aspect_, zNear_, zFar_);
	}

	projectionMatrixDirty_ = false;
}
