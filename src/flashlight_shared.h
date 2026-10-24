#ifndef UVSR_FLASHLIGHT_SHARED_H
#define UVSR_FLASHLIGHT_SHARED_H

// First-party transport for the camera flashlight's analytic beam shape.
// Donut's PBR path does not otherwise consume SpotLight::radius or
// LightConstants::shadowChannel.yzw. shadowChannel.x remains exclusively
// owned by the existing shadow-map association.
#define UVSR_FLASHLIGHT_SHAPE_RADIUS_TAG 1024.0f
#define UVSR_FLASHLIGHT_MIN_SHAPE_EXPONENT 2.0f
#define UVSR_FLASHLIGHT_MAX_SHAPE_EXPONENT 16.0f
#define UVSR_FLASHLIGHT_AXIS_QUANTIZATION 32767.0f

#endif // UVSR_FLASHLIGHT_SHARED_H
