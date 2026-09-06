struct PointLight {
    float4 positionRange;
    float4 colorIntensity;
    float4 directionOuter;
    float4 cone;
};

cbuffer FrameData : register(b0) {
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float4 eyeExposure;
    float4 sunDirectionAmbient;
    float4 sunColorIntensity;
    float4 settings;
    PointLight pointLights[16];
};

struct InstanceData {
    row_major float4x4 world;
    row_major float4x4 normalWorld;
    float4 baseColor;
    float4 material;
    float4 objectOptions;
    float4 uvTransform;
    float4 emissionColorStrength;
    float4 padding0;
    float4 padding1;
    float4 padding2;
};

cbuffer DrawData : register(b1) { uint firstInstance; };
StructuredBuffer<InstanceData> instances : register(t5);
#ifdef VELOS_RAY_SHADOWS
RaytracingAccelerationStructure shadowScene : register(t6);
#endif

Texture2D<float> shadowMap : register(t0);
SamplerComparisonState shadowSampler : register(s0);
Texture2D<float4> albedoMap : register(t1);
Texture2D<float4> normalMap : register(t2);
Texture2D<float4> ormMap : register(t3);
Texture2D<float4> emissionMap : register(t4);
SamplerState materialSampler : register(s1);

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
    nointerpolation uint instance : TEXCOORD3;
};

VertexOutput VSMain(VertexInput input, uint instanceId : SV_InstanceID) {
    VertexOutput output;
    uint index = firstInstance + instanceId;
    InstanceData data = instances[index];
    float4 worldPosition = mul(float4(input.position, 1), data.world);
    output.position = mul(worldPosition, viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul(float4(input.normal, 0), data.normalWorld).xyz;
    output.uv = input.uv * data.uvTransform.xy + data.uvTransform.zw;
    output.instance = index;
    return output;
}

VertexOutput VSShadow(VertexInput input, uint instanceId : SV_InstanceID) {
    VertexOutput output = VSMain(input, instanceId);
    output.position = mul(float4(output.worldPosition, 1), lightViewProjection);
    return output;
}

void PSShadow(VertexOutput input) {
    InstanceData data = instances[input.instance];
    if (data.objectOptions.y > 0.5) { clip(albedoMap.Sample(materialSampler, input.uv).a * data.baseColor.a - data.objectOptions.z); }
}

float3 mappedNormal(VertexOutput input, float normalStrength) {
    float3 normal = normalize(input.normal);
    float3 positionX = ddx(input.worldPosition);
    float3 positionY = ddy(input.worldPosition);
    float2 uvX = ddx(input.uv);
    float2 uvY = ddy(input.uv);
    float determinant = uvX.x * uvY.y - uvX.y * uvY.x;
    if (abs(determinant) < 0.0000001) { return normal; }
    float3 tangent = (positionX * uvY.y - positionY * uvX.y) / determinant;
    tangent -= normal * dot(normal, tangent);
    float tangentLength = dot(tangent, tangent);
    if (tangentLength < 0.0000001) { return normal; }
    tangent *= rsqrt(tangentLength);
    float3 bitangent = normalize(cross(normal, tangent));
    float3 sourceBitangent = (-positionX * uvY.x + positionY * uvX.x) / determinant;
    bitangent *= dot(bitangent, sourceBitangent) < 0 ? -1 : 1;
    float2 normalXY = (normalMap.Sample(materialSampler, input.uv).xy * 2 - 1) * normalStrength;
    float normalZ = sqrt(saturate(1 - dot(normalXY, normalXY)));
    return normalize(tangent * normalXY.x + bitangent * normalXY.y + normal * normalZ);
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

float visibility(float3 position, float normalLight, float3 geometricNormal) {
    if (settings.x < 0.5) { return 1; }
#ifdef VELOS_RAY_SHADOWS
    if (settings.w > 0.5) {
        float3 direction = normalize(-sunDirectionAmbient.xyz);
        float3 offsetNormal = normalize(geometricNormal);
        offsetNormal *= dot(offsetNormal, direction) < 0 ? -1 : 1;
        float offset = max(0.002, length(position) * 0.000002);
        RayDesc ray;
        ray.Origin = position + offsetNormal * offset;
        ray.Direction = direction;
        ray.TMin = 0.001;
        ray.TMax = 10000;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(shadowScene, RAY_FLAG_NONE, 0xff, ray);
        while (query.Proceed()) {}
        return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
    }
#endif
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
    InstanceData data = instances[input.instance];
    float4 baseColor = data.baseColor;
    float4 material = data.material;
    float4 objectOptions = data.objectOptions;
    float4 emissionColorStrength = data.emissionColorStrength;
    float4 sampledAlbedo = albedoMap.Sample(materialSampler, input.uv);
    float opacity = sampledAlbedo.a * baseColor.a;
    if (objectOptions.y > 0.5 && objectOptions.y < 1.5) { clip(opacity - objectOptions.z); }
    float3 normal = mappedNormal(input, objectOptions.w);
    float3 albedo = pow(saturate(baseColor.rgb), 2.2) * sampledAlbedo.rgb;
    float3 orm = ormMap.Sample(materialSampler, input.uv).rgb;
    float roughness = clamp(material.x * orm.g, 0.04, 1);
    float metallic = saturate(material.y * orm.b);
    float3 emission = pow(emissionColorStrength.rgb, 2.2) * emissionColorStrength.w * emissionMap.Sample(materialSampler, input.uv).rgb;
    if (objectOptions.x > 0.5) {
        float2 derivative = max(fwidth(input.worldPosition.xz), 0.0001.xx);
        float2 gridDistance = abs(frac(input.worldPosition.xz - 0.5) - 0.5) / derivative;
        float gridLine = 1 - saturate(min(gridDistance.x, gridDistance.y));
        albedo = lerp(albedo, albedo * 1.42, gridLine * 0.55);
    }
    if (material.w > 0.5) { return float4(pow(saturate(albedo), 1.0 / 2.2), opacity); }
    float3 viewDirection = normalize(eyeExposure.xyz - input.worldPosition);
    float3 sunDirection = normalize(-sunDirectionAmbient.xyz);
    float hemisphere = saturate(normal.y * 0.5 + 0.5);
    float3 ambientColor = lerp(float3(0.14, 0.16, 0.18), float3(0.65, 0.74, 0.8), hemisphere);
    float3 color = albedo * ambientColor * sunDirectionAmbient.w * orm.r + emission;
    color += evaluateLight(normal, viewDirection, sunDirection, albedo, roughness, metallic,
        sunColorIntensity.rgb * sunColorIntensity.w) * visibility(input.worldPosition, saturate(dot(normal, sunDirection)), input.normal);
    for (uint lightIndex = 0; lightIndex < (uint)settings.y; ++lightIndex) {
        PointLight light = pointLights[lightIndex];
        float3 offset = light.positionRange.xyz - input.worldPosition;
        float distanceSquared = max(dot(offset, offset), 0.01);
        float fade = saturate(1 - distanceSquared / (light.positionRange.w * light.positionRange.w));
        if (light.cone.y > 0.5) {
            float angle = dot(normalize(-offset), normalize(light.directionOuter.xyz));
            fade *= smoothstep(light.directionOuter.w, light.cone.x, angle);
        }
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * fade * fade / distanceSquared;
        color += evaluateLight(normal, viewDirection, normalize(offset), albedo, roughness, metallic, radiance);
    }
    if (material.z > 0.5) {
        float rim = pow(1 - saturate(dot(normal, viewDirection)), 3);
        color += float3(0.08, 0.65, 0.47) * (0.08 + rim * 0.7);
    }
    return float4(tonemap(color), objectOptions.y > 1.5 ? opacity : 1);
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