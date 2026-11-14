// The Slang twin of lib/groundcover.glsl (task 0072, stage f): groundcover's instance rotation, wind and stomping. The
// reasons are in the GLSL; change both together. GLSL's mat4(16 numbers) fills columns and float4x4(16 numbers) rows,
// so the rotation is transposed.
#ifndef LIB_GROUNDCOVER_SLANG
#define LIB_GROUNDCOVER_SLANG

float2 groundcoverDisplacement(
    float3 worldpos, float h, float time, float3 footPos, float windSpeed, int stompMode, int stompIntensity)
{
    float2 windDirection = float2(1.0);
    float3 windVec = float3(windSpeed * windDirection, 1.0);

    float v = length(windVec);
    float2 displace = float2(2.0 * windVec.xy + 0.1);
    float2 harmonics = float2(0.0);

    harmonics += float2((1.0 - 0.10 * v) * sin(1.0 * time + worldpos.xy / 1100.0));
    harmonics += float2((1.0 - 0.04 * v) * cos(2.0 * time + worldpos.xy / 750.0));
    harmonics += float2((1.0 + 0.14 * v) * sin(3.0 * time + worldpos.xy / 500.0));
    harmonics += float2((1.0 + 0.28 * v) * sin(5.0 * time + worldpos.xy / 200.0));

    float2 stomp = float2(0.0);
    if (stompMode != 0)
    {
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

float4x4 groundcoverRotation(float3 angle)
{
    float sin_x = sin(angle.x);
    float cos_x = cos(angle.x);
    float sin_y = sin(angle.y);
    float cos_y = cos(angle.y);
    float sin_z = sin(angle.z);
    float cos_z = cos(angle.z);

    return transpose(float4x4(cos_z * cos_y + sin_x * sin_y * sin_z, -sin_z * cos_x,
        cos_z * sin_y + sin_z * sin_x * cos_y, 0.0, sin_z * cos_y + cos_z * sin_x * sin_y, cos_z * cos_x,
        sin_z * sin_y - cos_z * sin_x * cos_y, 0.0, -sin_y * cos_x, sin_x, cos_x * cos_y, 0.0, 0.0, 0.0, 0.0, 1.0));
}

// OpenEden (task 0153 follow-up 2): the neutral grass's rotation (see the GLSL twin): a yaw about Z, then the turn that
// takes +Z to the ground normal whose x and y are `tilt`. mul(M, v) is the usual M * v here, so the rows are written
// as they are (the matrix above is written column-major and transposed).
float4x4 groundcoverTiltRotation(float2 tilt, float yaw)
{
    float cy = cos(yaw);
    float sy = sin(yaw);
    float3x3 yawRotation = float3x3(cy, -sy, 0.0, sy, cy, 0.0, 0.0, 0.0, 1.0);
    float3 n = float3(tilt, sqrt(max(0.0, 1.0 - dot(tilt, tilt))));
    float3 k = float3(-n.y, n.x, 0.0);
    float3x3 K = float3x3(0.0, -k.z, k.y, k.z, 0.0, -k.x, -k.y, k.x, 0.0);
    float3x3 identity = float3x3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    float3x3 tiltRotation = identity + K + mul(K, K) / (1.0 + n.z);
    float3x3 m = mul(tiltRotation, yawRotation);
    return float4x4(float4(m[0], 0.0), float4(m[1], 0.0), float4(m[2], 0.0), float4(0.0, 0.0, 0.0, 1.0));
}

#endif
