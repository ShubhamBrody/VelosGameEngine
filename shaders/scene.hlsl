struct PointLight {
    float4 positionRange;
    float4 colorIntensity;
};

cbuffer FrameData : register(b0) {
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float4 eyeExposure;
    float4 sunDirectionAmbient;
    float4 sunColorIntensity;
    float4 settings;
    PointLight pointLights[8];
};

cbuffer ObjectData : register(b1) {
    row_major float4x4 world;
    row_major float4x4 normalWorld;
    float4 baseColor;
    float4 material;
    float4 objectOptions;
};

Texture2D<float> shadowMap : register(t0);
SamplerComparisonState shadowSampler : register(s0);

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    float4 worldPosition = mul(float4(input.position, 1), world);
    output.position = mul(worldPosition, viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul(float4(input.normal, 0), normalWorld).xyz;
    output.uv = input.uv;
    return output;
}

float4 VSShadow(VertexInput input) : SV_POSITION {
    return mul(mul(float4(input.position, 1), world), lightViewProjection);
}

float3 fresnel(float cosine, float3 reflectance) {
    return reflectance + (1 - reflectance) * pow(1 - saturate(cosine), 5);
}

float3 evaluateLight(float3 normal, float3 viewDirection, float3 lightDirection,
    float3 albedo, float roughness, float metallic, float3 radiance) {
    float normalLight = saturate(dot(normal, lightDirection));
    float normalView = max(saturate(dot(normal, viewDirection)), 0.0001);
    float3 halfDirection = normalize(viewDirection + lightDirection);
    float normalHalf = saturate(dot(normal, halfDirection));
    float roughSquared = roughness * roughness;
    float alphaSquared = roughSquared * roughSquared;
    float denominator = normalHalf * normalHalf * (alphaSquared - 1) + 1;
    float distribution = alphaSquared / max(3.14159265 * denominator * denominator, 0.0001);
    float geometryFactor = (roughness + 1) * (roughness + 1) / 8;
    float geometryView = normalView / (normalView * (1 - geometryFactor) + geometryFactor);
    float geometryLight = normalLight / (normalLight * (1 - geometryFactor) + geometryFactor);
    float3 reflectance = fresnel(dot(halfDirection, viewDirection), lerp(0.04.xxx, albedo, metallic));
    float3 specular = distribution * geometryView * geometryLight * reflectance / max(4 * normalView * normalLight, 0.0001);
    float3 diffuse = (1 - reflectance) * (1 - metallic) * albedo / 3.14159265;
    return (diffuse + specular) * radiance * normalLight;
}

float visibility(float3 position, float normalLight) {
    if (settings.x < 0.5) { return 1; }
    float4 lightPosition = mul(float4(position, 1), lightViewProjection);
    float3 projected = lightPosition.xyz / lightPosition.w;
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) { return 1; }
    float shadow = 0;
    float bias = max(0.0005, 0.0015 * (1 - normalLight));
    [unroll] for (int vertical = -1; vertical <= 1; ++vertical) {
        [unroll] for (int horizontal = -1; horizontal <= 1; ++horizontal) {
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(horizontal, vertical) * settings.z, projected.z - bias);
        }
    }
    return shadow / 9;
}

float3 tonemap(float3 color) {
    color *= eyeExposure.w;
    color = saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
    return pow(color, 1.0 / 2.2);
}

float4 PSMain(VertexOutput input) : SV_TARGET {
    float3 normal = normalize(input.normal);
    float3 albedo = pow(saturate(baseColor.rgb), 2.2);
    if (objectOptions.x > 0.5) {
        float2 derivative = max(fwidth(input.worldPosition.xz), 0.0001.xx);
        float2 gridDistance = abs(frac(input.worldPosition.xz - 0.5) - 0.5) / derivative;
        float gridLine = 1 - saturate(min(gridDistance.x, gridDistance.y));
        albedo = lerp(albedo, albedo * 1.42, gridLine * 0.55);
    }
    if (material.w > 0.5) { return float4(baseColor.rgb, 1); }
    float3 viewDirection = normalize(eyeExposure.xyz - input.worldPosition);
    float3 sunDirection = normalize(-sunDirectionAmbient.xyz);
    float hemisphere = saturate(normal.y * 0.5 + 0.5);
    float3 ambientColor = lerp(float3(0.14, 0.16, 0.18), float3(0.65, 0.74, 0.8), hemisphere);
    float3 color = albedo * ambientColor * sunDirectionAmbient.w;
    color += evaluateLight(normal, viewDirection, sunDirection, albedo, material.x, material.y,
        sunColorIntensity.rgb * sunColorIntensity.w) * visibility(input.worldPosition, saturate(dot(normal, sunDirection)));
    for (uint lightIndex = 0; lightIndex < (uint)settings.y; ++lightIndex) {
        PointLight light = pointLights[lightIndex];
        float3 offset = light.positionRange.xyz - input.worldPosition;
        float distanceSquared = max(dot(offset, offset), 0.01);
        float fade = saturate(1 - distanceSquared / (light.positionRange.w * light.positionRange.w));
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * fade * fade / distanceSquared;
        color += evaluateLight(normal, viewDirection, normalize(offset), albedo, material.x, material.y, radiance);
    }
    if (material.z > 0.5) {
        float rim = pow(1 - saturate(dot(normal, viewDirection)), 3);
        color += float3(0.08, 0.65, 0.47) * (0.08 + rim * 0.7);
    }
    return float4(tonemap(color), 1);
}

struct BackgroundOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

BackgroundOutput VSBackground(uint vertexId : SV_VertexID) {
    BackgroundOutput output;
    float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    output.uv = position;
    output.position = float4(position * float2(2, -2) + float2(-1, 1), 0.9999, 1);
    return output;
}

float4 PSBackground(BackgroundOutput input) : SV_TARGET {
    return float4(lerp(float3(0.075, 0.085, 0.09), float3(0.18, 0.205, 0.215), saturate(input.uv.y)), 1);
}