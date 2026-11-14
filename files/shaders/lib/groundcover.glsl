// OpenEden (task 0012): groundcover's instance rotation, wind and stomping, from OpenMW's
// files/shaders/compatibility/groundcover.vert at 442eec6e2f. The stomp mode and intensity are runtime values here
// (scene.groundcoverParams.yz) rather than OpenMW's compile-time defines.

vec2 groundcoverDisplacement(in vec3 worldpos, float h, float time, vec3 footPos, float windSpeed, int stompMode,
    int stompIntensity)
{
    vec2 windDirection = vec2(1.0);
    vec3 windVec = vec3(windSpeed * windDirection, 1.0);

    float v = length(windVec);
    vec2 displace = vec2(2.0 * windVec + 0.1);
    vec2 harmonics = vec2(0.0);

    harmonics += vec2((1.0 - 0.10*v) * sin(1.0*time + worldpos.xy / 1100.0));
    harmonics += vec2((1.0 - 0.04*v) * cos(2.0*time + worldpos.xy / 750.0));
    harmonics += vec2((1.0 + 0.14*v) * sin(3.0*time + worldpos.xy / 500.0));
    harmonics += vec2((1.0 + 0.28*v) * sin(5.0*time + worldpos.xy / 200.0));

    vec2 stomp = vec2(0.0);
    if (stompMode != 0)
    {
        // Gentle, reduced and MGE XE intensity.
        float stompRange = 50.0;
        float stompDistance = 20.0;
        if (stompIntensity == 1)
        {
            stompRange = 80.0;
            stompDistance = 40.0;
        }
        else if (stompIntensity == 2)
        {
            stompRange = 150.0;
            stompDistance = 60.0;
        }
        float d = length(worldpos.xy - footPos.xy);
        if (d < stompRange && d > 0.0)
            stomp = (stompDistance / d - stompDistance / stompRange) * (worldpos.xy - footPos.xy);
        if (stompMode == 2)
            stomp *= clamp((worldpos.z - footPos.z) / h, 0.0, 1.0);
    }

    return clamp(0.02 * h, 0.0, 1.0) * (harmonics * displace + stomp);
}

mat4 groundcoverRotation(in vec3 angle)
{
    float sin_x = sin(angle.x);
    float cos_x = cos(angle.x);
    float sin_y = sin(angle.y);
    float cos_y = cos(angle.y);
    float sin_z = sin(angle.z);
    float cos_z = cos(angle.z);

    return mat4(
        cos_z*cos_y+sin_x*sin_y*sin_z, -sin_z*cos_x, cos_z*sin_y+sin_z*sin_x*cos_y, 0.0,
        sin_z*cos_y+cos_z*sin_x*sin_y, cos_z*cos_x, sin_z*sin_y-cos_z*sin_x*cos_y, 0.0,
        -sin_y*cos_x, sin_x, cos_x*cos_y, 0.0,
        0.0, 0.0, 0.0, 1.0);
}

// OpenEden (task 0153 follow-up 2): the neutral grass's rotation: a yaw about Z, then the turn that takes +Z to the
// ground normal whose x and y are `tilt` (Rodrigues' formula; the normal's z is the rest of a unit vector).
mat4 groundcoverTiltRotation(in vec2 tilt, float yaw)
{
    float cy = cos(yaw);
    float sy = sin(yaw);
    // Column-major: columns (cy, sy, 0), (-sy, cy, 0), (0, 0, 1).
    mat3 yawRotation = mat3(cy, sy, 0.0, -sy, cy, 0.0, 0.0, 0.0, 1.0);
    vec3 n = vec3(tilt, sqrt(max(0.0, 1.0 - dot(tilt, tilt))));
    // k = z x n; the cross product matrix [k]x has columns (0, kz, -ky), (-kz, 0, kx), (ky, -kx, 0).
    vec3 k = vec3(-n.y, n.x, 0.0);
    mat3 K = mat3(0.0, k.z, -k.y, -k.z, 0.0, k.x, k.y, -k.x, 0.0);
    mat3 tiltRotation = mat3(1.0) + K + (K * K) / (1.0 + n.z);
    mat3 m = tiltRotation * yawRotation;
    return mat4(vec4(m[0], 0.0), vec4(m[1], 0.0), vec4(m[2], 0.0), vec4(0.0, 0.0, 0.0, 1.0));
}
